#include "asmjit/x86.h"

#include <iostream>

#include "jit.hpp"



using namespace asmjit;



namespace toyjit::runtime {
    void JIT::save_start_loop_label(Label l) {
        auto& flow = m_flows.back();

        if (auto loop_data = std::get_if<JIT::Loop>(&flow); loop_data) {
            loop_data->start_loop_label = l;
        }
    }

    void JIT::save_exit_loop_label(Label l) {
        auto& flow = m_flows.back();

        if (auto loop_data = std::get_if<JIT::Loop>(&flow); loop_data) {
            loop_data->exit_loop_label = l;
        }
    }

    void JIT::save_end_tbody_label(Label l) {
        auto& flow = m_flows.back();

        if (auto ifs_data = std::get_if<JIT::Ifs>(&flow); ifs_data) {
            ifs_data->end_tbody_label = l;
        }
    }

    void JIT::save_end_fbody_label(Label l) {
        auto& flow = m_flows.back();

        if (auto ifs_data = std::get_if<JIT::Ifs>(&flow); ifs_data) {
            ifs_data->end_fbody_label = l;
        }
    }

    bool JIT::update_sim_sp_with_nonary(Op op) {
        switch (op) {
        case Op::nop:
            break;
        case Op::dup:
            m_sim_sp++;
            m_sim_stack[m_sim_sp] = m_sim_stack[m_sim_sp - 1];

            break;
        case Op::swap:
            std::swap(m_sim_stack[m_sim_sp], m_sim_stack[m_sim_sp - 1]);

            break;
        case Op::pop:
            m_sim_sp--;

            break;
        case Op::add:
        case Op::sub:
            m_sim_sp--;

            if (m_sim_stack[m_sim_sp] != m_sim_stack[m_sim_sp + 1]) {
                m_sim_stack[m_sim_sp] = VTag::v_oops;
            } else if (m_sim_stack[m_sim_sp] != VTag::v_i32) {
                m_sim_stack[m_sim_sp] = VTag::v_oops;
            }

            break;
        case Op::eq:
        case Op::ne:
        case Op::lt:
        case Op::gt:
            m_sim_sp--;
            m_sim_stack[m_sim_sp] = VTag::v_boolean;

            break;
        default: return false;
        }

        return true;
    }

    bool JIT::update_sim_sp_with_unary(Op op, std::int32_t w) {
        switch (op) {
        case Op::reserve:
            m_sim_sp += w;

            break;
        case Op::get_local:
            m_sim_sp++;
            m_sim_stack[m_sim_sp] = m_sim_stack[w];

            break;
        case Op::set_local:
            m_sim_stack[w] = m_sim_stack[m_sim_sp];
            m_sim_sp--;

            break;
        case Op::push_k:
            m_sim_sp++;
            m_sim_stack[m_sim_sp] = m_cfg->peek_konst(w)->tag;

            break;
        case Op::jump_else:
        case Op::jump_if:
            m_sim_sp--;

            break;
        case Op::jump: break;
        default: return false;
        }

        return true;
    }

    bool JIT::update_sim_sp_with_binary(Op op, std::int32_t w, std::uint16_t s) {
        switch (op) {
            case Op::call:
            case Op::native_call:
                m_sim_sp -= s;
                break;
            default: return false;
        }

        return true;
    }

    bool JIT::update_sim_sp(Inst i) {
        const auto [w, s, b, op] = i;

        if (update_sim_sp_with_nonary(op)) {
            ;
        } else if (update_sim_sp_with_unary(op, w)) {
            ;
        } else if (update_sim_sp_with_binary(op, w, s)) {
            ;
        } else if (op == Op::ret) {
            ;
        } else {
            return false;
        }

        return true;
    }

    void JIT::emit_prelude() {
        // ; mark function start in case of self-recursion
        Label start_fn_label = m_as.new_label();
        m_self_label = start_fn_label;
        m_as.bind(start_fn_label);

        // ; save unresolved deopt label...
        m_deopt_label = m_as.new_label();

        // ; set up stack frame
        m_as.push(x86::regs::rbp);
        m_as.mov(x86::regs::rbp, x86::regs::rsp);

        // ! Here, preserve copies of the parameters' register data for this NativeFn stub: VM* vm, Value* locals, const Value* cvp, const HelperFn* helpers are RDI, RSI, RDX, RCX. RDI is left the same anyways.
        m_as.push(x86::regs::rdi);
        m_as.push(x86::regs::rsi);
        m_as.push(x86::regs::rdx);
        m_as.push(x86::regs::rcx);
    }

    void JIT::emit_deopt_area() {
        m_as.bind(m_deopt_label);

        const auto helper_id = std::to_underlying(HelperID::bailout_stub);

        // ? Note: dest has no-init but will be an "oops" Value
        m_as.sub(x86::regs::rsp, value_size);
        m_as.mov(x86::regs::rsi, x86::regs::rsp); // <-- `Value* dest`

        // ? RDI is preserved and restored, so it's ok to access `VM* vm` from the helper.
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));
        m_as.call(x86::regs::r9);

        // ? Return "oops" Value to signal deopt to callers if any.
        m_as.bind(m_fast_ret_label); // ! Go here if an "oops" from deopt already began propogation.
        m_as.pop(x86::regs::rax);

        // ? Restore argument registers in case.
        m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));

        // ; collapse stack frame...
        m_as.mov(x86::regs::rsp, x86::regs::rbp);
        m_as.pop(x86::regs::rbp);
        m_as.ret();
    }

    [[nodiscard]]
    bool JIT::emit_nop([[maybe_unused]] Inst i) {
        return true;
    }

    [[nodiscard]]
    bool JIT::emit_reserve(Inst i) {
        const auto non_arg_locals_count = i.w;

        update_sim_sp(i);

        // ; Reserve N slots for local Value values- These are set by initializer values from other computations via `set_local` code.
        if (non_arg_locals_count > 0) {
            m_as.sub(x86::regs::rsp, non_arg_locals_count * value_size);
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_get_local(Inst i) {
        // ! The raw offset here is actually relative to stack[CALLEE_BP] where local 0 is. This means that the stub should offset directly from `locals` which points back to `stack[CALLEE_BP]`.
        const auto local_offset = i.w;

        update_sim_sp(i);

        // ! Handle case of argument locals (Locals 0 to ARGC - 1).
        if (local_offset < m_argc) {
            // mov r8, [rsi + local_offset * 8] ... Base.Index.Scale
            m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rsi, local_offset * value_size));
        } else {
            const auto non_arg_local_offset = local_offset - m_argc;

            m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rbp, -(non_arg_local_offset + 5) * value_size)); // ! IMPORTANT: Use RBP + 5 to start the native non-arg locals above the 4 preserved parameters.
        }

        m_as.push(x86::regs::r8);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_set_local(Inst i) {
        const auto local_offset = i.w;

        m_as.pop(x86::regs::r8);

        if (local_offset < m_argc) {
            // mov [rsi + local_offset * 8], r8 ... Base.Index.Scale
            m_as.mov(x86::ptr(x86::regs::rsi, local_offset * value_size), x86::regs::r8);
        } else {
            const auto non_arg_local_offset = local_offset - m_argc;

            m_as.mov(x86::ptr(x86::regs::rbp, -(non_arg_local_offset + 5) * value_size), x86::regs::r8);
        }

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_push_k(Inst i) {
        const auto konst_offset = i.w;

        update_sim_sp(i);

        // mov r8, [rdx + konst_offset * 8] ... Base.Index.Scale
        m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rdx, konst_offset * value_size));
        m_as.push(x86::regs::r8);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_dup([[maybe_unused]] Inst i) {
        update_sim_sp(i);

        m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rsp, 0));
        m_as.push(x86::regs::r8);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_swap([[maybe_unused]] Inst i) {
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rsp, 0));
        m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rsp, value_size));
        m_as.mov(x86::ptr(x86::regs::rsp, value_size), x86::regs::r9);
        m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r8);

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_pop([[maybe_unused]] Inst i) {
        m_as.add(x86::regs::rsp, value_size);

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_add(Inst i) {
        // ? Here, handle the i32 specialization if both simulated operand types are i32. This is well known at runtime because the JIT was fed 0-4 Value args with type tagging & the CFG that refers to tagged chunk constants. Still, emit type guards with deopt jumps anyways.
        if (m_sim_stack[m_sim_sp - 1] == VTag::v_i32 && m_sim_stack[m_sim_sp] == VTag::v_i32) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 2);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 2);
            m_as.jne(m_deopt_label);

            m_as.pop(x86::regs::r8);
            m_as.add(x86::ptr(x86::regs::rsp, 0), x86::regs::r8d); // lhs->data.n += rhs->data.n;

            update_sim_sp(i);

            return true;
        }

        // ! NOTE: call generic helper here, as this JIT is too naive to know if all possible temporaries are of the same type from the bytecode instructions alone.
        const auto helper_id = std::to_underlying(HelperID::add_gen);

        // ; store `Value* target` (dest)
        m_as.lea(x86::regs::rsi, x86::ptr(x86::regs::rsp, value_size));

        // ; store `Value* a1` (src) Value ptr
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_sub(Inst i) {
        // ? Here, handle the i32 specialization if both simulated operand types are i32. This is well known at runtime because the JIT was fed 0-4 Value args with type tagging & the CFG that refers to tagged chunk constants.
        if (m_sim_stack[m_sim_sp - 1] == VTag::v_i32 && m_sim_stack[m_sim_sp] == VTag::v_i32) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 2);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 2);
            m_as.jne(m_deopt_label);

            m_as.pop(x86::regs::r8);
            m_as.sub(x86::ptr(x86::regs::rsp, 0), x86::regs::r8d); // lhs->data.n -= rhs->data.n;

            update_sim_sp(i);

            return true;
        }

        const auto helper_id = std::to_underlying(HelperID::sub_gen);

        // ; store `Value* target` (dest)
        m_as.lea(x86::regs::rsi, x86::ptr(x86::regs::rsp, value_size));

        // ; store `Value* a1` (src) Value ptr
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_eq(Inst i) {
        // ? Here, handle the i32 specialization if both simulated operand types are i32. This is well known at runtime because the JIT was fed 0-4 Value args with type tagging & the CFG that refers to tagged chunk constants.
        if (m_sim_stack[m_sim_sp - 1] == VTag::v_i32 && m_sim_stack[m_sim_sp] == VTag::v_i32) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 2);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 2);
            m_as.jne(m_deopt_label);
            
            m_as.mov(x86::regs::r9, 0); // ! temp_flag = false; until proven true
            m_as.pop(x86::regs::r8);
            m_as.cmp(x86::ptr(x86::regs::rsp, 0), x86::regs::r8d); // lhs->data.n == rhs->data.n

            asmjit::Label post_set_true_label = m_as.new_label();
            m_as.jne(post_set_true_label);
            m_as.mov(x86::regs::r9, 1);
            m_as.bind(post_set_true_label);
            m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r9b); // temp.data.byte = temp_flag;
            m_as.mov(x86::regs::r8, 1);
            m_as.mov(x86::ptr(x86::regs::rsp, value_union_size), x86::regs::r8b); // temp.tag = v_boolean;

            update_sim_sp(i);

            return true;
        } else if (m_sim_stack[m_sim_sp - 1] == VTag::v_boolean && m_sim_stack[m_sim_sp] == VTag::v_boolean) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 1);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 1);
            m_as.jne(m_deopt_label);

            m_as.mov(x86::regs::r9, 0);
            m_as.pop(x86::regs::r8);
            m_as.cmp(x86::ptr(x86::regs::rsp, 0), x86::regs::r8b);

            asmjit::Label post_set_true_label = m_as.new_label();
            m_as.jne(post_set_true_label);
            m_as.mov(x86::regs::r9, 1);
            m_as.bind(post_set_true_label);
            m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r9b); // temp.data.byte = temp_flag;
            m_as.mov(x86::regs::r8, 1);
            m_as.mov(x86::ptr(x86::regs::rsp, value_union_size), x86::regs::r8b); // temp.tag = v_boolean;

            update_sim_sp(i);

            return true;
        }

        const auto helper_id = std::to_underlying(HelperID::eq_gen);

        // ; store `Value* target` (dest)
        m_as.lea(x86::regs::rsi, x86::ptr(x86::regs::rsp, value_size));

        // ; store `Value* a1` (src) Value ptr
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_ne(Inst i) {
        // ? Here, handle the i32 specialization if both simulated operand types are i32. This is well known at runtime because the JIT was fed 0-4 Value args with type tagging & the CFG that refers to tagged chunk constants.
        if (m_sim_stack[m_sim_sp - 1] == VTag::v_i32 && m_sim_stack[m_sim_sp] == VTag::v_i32) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 2);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 2);
            m_as.jne(m_deopt_label);

            m_as.mov(x86::regs::r9, 0); // ! temp_flag = false; until proven true
            m_as.pop(x86::regs::r8);
            m_as.cmp(x86::ptr(x86::regs::rsp, 0), x86::regs::r8d); // lhs->data.n == rhs->data.n

            asmjit::Label post_set_true_label = m_as.new_label();
            m_as.je(post_set_true_label);
            m_as.mov(x86::regs::r9, 1);
            m_as.bind(post_set_true_label);
            m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r9b); // temp.data.byte = temp_flag;
            m_as.mov(x86::regs::r8, 1);
            m_as.mov(x86::ptr(x86::regs::rsp, value_union_size), x86::regs::r8b); // temp.tag = v_boolean;

            update_sim_sp(i);

            return true;
        } else if (m_sim_stack[m_sim_sp - 1] == VTag::v_boolean && m_sim_stack[m_sim_sp] == VTag::v_boolean) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 1);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 1);
            m_as.jne(m_deopt_label);

            m_as.mov(x86::regs::r9, 0);
            m_as.pop(x86::regs::r8);
            m_as.cmp(x86::ptr(x86::regs::rsp, 0), x86::regs::r8b);

            asmjit::Label post_set_true_label = m_as.new_label();
            m_as.je(post_set_true_label);
            m_as.mov(x86::regs::r9, 1);
            m_as.bind(post_set_true_label);
            m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r9b); // temp.data.byte = temp_flag;
            m_as.mov(x86::regs::r8, 1);
            m_as.mov(x86::ptr(x86::regs::rsp, value_union_size), x86::regs::r8b); // temp.tag = v_boolean;

            update_sim_sp(i);

            return true;
        }

        const auto helper_id = std::to_underlying(HelperID::ne_gen);

        // ; Register rdi is the same (VM* vm), but save RSI, RDX to preserve the arg-local ptr and cvp ptr
        // ; store `Value* target` (dest)
        // lea rsi, [rsp + 8]
        m_as.lea(x86::regs::rsi, x86::ptr(x86::regs::rsp, value_size));

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_lt(Inst i) {
        // ? Here, handle the i32 specialization if both simulated operand types are i32. This is well known at runtime because the JIT was fed 0-4 Value args with type tagging & the CFG that refers to tagged chunk constants.
        if (m_sim_stack[m_sim_sp - 1] == VTag::v_i32 && m_sim_stack[m_sim_sp] == VTag::v_i32) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 2);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 2);
            m_as.jne(m_deopt_label);

            m_as.mov(x86::regs::r9, 0); // ! temp_flag = false; until proven true
            m_as.pop(x86::regs::r8);
            m_as.cmp(x86::ptr(x86::regs::rsp, 0), x86::regs::r8d); // lhs->data.n == rhs->data.n

            asmjit::Label post_set_true_label = m_as.new_label();
            m_as.jge(post_set_true_label);
            m_as.mov(x86::regs::r9b, 1);
            m_as.bind(post_set_true_label);
            m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r9b); // temp.data.byte = temp_flag;
            m_as.mov(x86::regs::r8, 1);
            m_as.mov(x86::ptr(x86::regs::rsp, value_union_size), x86::regs::r8b); // temp.tag = v_boolean;

            update_sim_sp(i);

            return true;
        } else if (m_sim_stack[m_sim_sp - 1] == VTag::v_boolean && m_sim_stack[m_sim_sp] == VTag::v_boolean) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 1);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 1);
            m_as.jne(m_deopt_label);

            m_as.mov(x86::regs::r9, 0);
            m_as.pop(x86::regs::r8);
            m_as.cmp(x86::ptr(x86::regs::rsp, 0), x86::regs::r8b);

            asmjit::Label post_set_true_label = m_as.new_label();
            m_as.jge(post_set_true_label);
            m_as.mov(x86::regs::r9, 1);
            m_as.bind(post_set_true_label);
            m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r9b); // temp.data.byte = temp_flag;
            m_as.mov(x86::regs::r8, 1);
            m_as.mov(x86::ptr(x86::regs::rsp, value_union_size), x86::regs::r8b); // temp.tag = v_boolean;

            update_sim_sp(i);

            return true;
        }

        const auto helper_id = std::to_underlying(HelperID::lt_gen);

        // ; store `Value* target` (dest)
        // lea rsi, [rsp + 8]
        m_as.lea(x86::regs::rsi, x86::ptr(x86::regs::rsp, value_size));

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_gt(Inst i) {
        // ? Here, handle the i32 specialization if both simulated operand types are i32. This is well known at runtime because the JIT was fed 0-4 Value args with type tagging & the CFG that refers to tagged chunk constants.
        if (m_sim_stack[m_sim_sp - 1] == VTag::v_i32 && m_sim_stack[m_sim_sp] == VTag::v_i32) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 2);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 2);
            m_as.jne(m_deopt_label);

            m_as.mov(x86::regs::r9, 0); // ! temp_flag = false; until proven true
            m_as.pop(x86::regs::r8);
            m_as.cmp(x86::ptr(x86::regs::rsp, 0), x86::regs::r8d); // lhs->data.n == rhs->data.n

            asmjit::Label post_set_true_label = m_as.new_label();
            m_as.jle(post_set_true_label);
            m_as.mov(x86::regs::r9, 1);
            m_as.bind(post_set_true_label);
            m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r9b); // temp.data.byte = temp_flag;
            m_as.mov(x86::regs::r8, 1);
            m_as.mov(x86::ptr(x86::regs::rsp, value_union_size), x86::regs::r8b); // temp.tag = v_boolean;

            update_sim_sp(i);

            return true;
        } else if (m_sim_stack[m_sim_sp - 1] == VTag::v_boolean && m_sim_stack[m_sim_sp] == VTag::v_boolean) {
            // ! CHECK: <temp-rhs>.tag == v_i32
            m_as.mov(x86::regs::r11b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r11b, 1);
            m_as.jne(m_deopt_label);

            // ! CHECK: <temp-lhs>.tag == vi32
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_size + value_union_size));
            m_as.cmp(x86::regs::r10b, 1);
            m_as.jne(m_deopt_label);

            m_as.mov(x86::regs::r9, 0);
            m_as.pop(x86::regs::r8);
            m_as.cmp(x86::ptr(x86::regs::rsp, 0), x86::regs::r8b);

            asmjit::Label post_set_true_label = m_as.new_label();
            m_as.jle(post_set_true_label);
            m_as.mov(x86::regs::r9, 1);
            m_as.bind(post_set_true_label);
            m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r9b); // temp.data.byte = temp_flag;
            m_as.mov(x86::regs::r8, 1);
            m_as.mov(x86::ptr(x86::regs::rsp, value_union_size), x86::regs::r8b); // temp.tag = v_boolean;

            update_sim_sp(i);

            return true;
        }

        const auto helper_id = std::to_underlying(HelperID::gt_gen);

        // ; store `Value* target` (dest)
        // lea rsi, [rsp + 8]
        m_as.lea(x86::regs::rsi, x86::ptr(x86::regs::rsp, value_size));

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_jump_else([[maybe_unused]] Inst i) {
        // ; Check if <temp>.data.byte is not zero, popping it and jumping to an unresolved label if so.
        m_as.pop(x86::regs::r8);

        // ; If the byte for boolean values is 0, expect it as `FALSE` to jump ahead.
        m_as.cmp(x86::regs::r8b, 0);

        if (std::holds_alternative<JIT::Ifs>(m_flows.back())) {
            Label temp_end_tbody_label = m_as.new_label();

            m_as.jz(temp_end_tbody_label);
            save_end_tbody_label(temp_end_tbody_label);
        } else if (std::holds_alternative<JIT::Loop>(m_flows.back())) {
            Label temp_exit_loop_label = m_as.new_label();

            m_as.jz(temp_exit_loop_label);
            save_exit_loop_label(temp_exit_loop_label);
        } else {
            return false;
        }
        // ; Otherwise, let control flow fall through by 1 instruction

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_jump_if(Inst i) {
        std::cerr << "JIT of VM jump-if-true is unsupported :(\n";

        update_sim_sp(i);

        return false; // no-op, control flow is handled natively
    }

    [[nodiscard]]
    bool JIT::emit_jump([[maybe_unused]] Inst i) {
        auto& back_flow = m_flows.back();

        if (auto ifs = std::get_if<JIT::Ifs>(&back_flow); ifs) {
            m_as.jmp(ifs->end_fbody_label);
        } else if (auto loop = std::get_if<JIT::Loop>(&back_flow); loop) {
            m_as.jmp(loop->start_loop_label);
        } else {
            return false;
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_call(Inst i) {
        const std::int32_t callee_chunk_id = i.w;
        const std::int32_t callee_argc = i.s;

        if (callee_chunk_id == m_old_chunk_id) {
            if (callee_argc > 1) {
                // ! If 2+ args exist, reverse the argument temporaries since the native stack grows downwards. Thus, the Value* locals ptr must point to the current RSP, specifically the 1st of the reversed args.
                for (
                    // ? In the spirit of Leetcode 2-pointer problems, we can compile-time calculate and generate the swaps per Low-High index pair.
                    std::int32_t higher_swap_off = callee_argc - 1, lower_swap_off = 0;
                    lower_swap_off < higher_swap_off;
                    higher_swap_off--, lower_swap_off++
                ) {
                    m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rsp, value_size * higher_swap_off));
                    m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rsp, value_size * lower_swap_off));
                    m_as.mov(x86::ptr(x86::regs::rsp, value_size * higher_swap_off), x86::regs::r9);
                    m_as.mov(x86::ptr(x86::regs::rsp, value_size * lower_swap_off), x86::regs::r8);
                }
            }

            // ? In this special case, rdx and rcx are untouched. Value* cvp and Value* helpers don't need special handling here and will be restored when the sub-call completes.
            m_as.mov(x86::regs::rsi, x86::regs::rsp);

            // ! Call and then get resulting RAX of the caller (the same stub here).
            m_as.call(m_self_label);

            if (callee_argc > 0) {
                m_as.add(x86::regs::rsp, callee_argc * value_size);
            }

            // ? Check for oops indicators from any deopts!
            m_as.push(x86::regs::rax); // ? Re-push callee stub's result for correctness.
            m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_union_size));
            m_as.cmp(x86::regs::r10b, 5); // GUARD <temp>.tag != VTag::v_oops
            m_as.je(m_fast_ret_label);

            // ? Finally, restore the caller stub's original arguments after they've been dirtied by the callee.
            m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
            m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
            m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
            m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));

            return true;
        }

        const auto helper_id = std::to_underlying(HelperID::try_sub_call);

        // ? Form a pointer to N Values to feed the helper via `Value* a1`.
        m_as.lea(x86::regs::r9, x86::ptr(x86::regs::rsp, value_size * (callee_argc - 1)));

        // ! Here, store `Value* target` (dest) which is equal to `Value* a1` in this specifc case since the invoke_cid helper needs N natively-passed args to push onto the VM stack for the cid's trampoline call with arg-locals.
        m_as.mov(x86::regs::rsi, x86::regs::r9);
        m_as.mov(x86::regs::rdx, x86::regs::r9);

        // ! Here, store a Value[2] behind `Value* xa` (src) Value ptr: chunk-ID and argc.
        // ! BUT, the stack is LIFO and grows lower, so push argc before chunk-ID for the helper, allowing chunk-ID to reside in xa[0].
        // mov BYTE PTR [rsp], 2     ; put Value {.data.n = <argc>, .tag = VTag::v_i32}
        m_as.mov(x86::regs::r8, 2);
        m_as.push(x86::regs::r8d);
        // ; mov DWORD PTR [rsp], <argc>
        m_as.mov(x86::regs::r8, callee_argc);
        m_as.push(x86::regs::r8d);

        // mov BYTE PTR [rsp], 2     ; put Value {.data.n = <chunk-ID>, .tag = VTag::v_i32}
        m_as.mov(x86::regs::r8, 2);
        m_as.push(x86::regs::r8d);
        // ; mov DWORD PTR [rsp], <chunk-ID>
        m_as.mov(x86::regs::r8, callee_chunk_id);
        m_as.push(x86::regs::r8d);

        // ! Here, prepare `Value* xa` of Value[2].
        m_as.mov(x86::regs::r9, x86::regs::rcx); // ? Copy RCX ptr to helper functions ptr.
        m_as.mov(x86::regs::rcx, x86::regs::rsp);

        // ; call helper on these arguments...
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::r9, helper_id * ptr_size));

        // ! Here, the invoke_cid() helper must take the chunk-ID and argc to properly call the chunk, 
        m_as.call(x86::regs::r9);

        // ! Here, remove the used temporaries behind `Value* a1`: the chunk-ID, argc, and N arguments.
        m_as.add(x86::regs::rsp, value_size * (callee_argc + 1));

        // ? Check for oops indicators from any deopts!
        m_as.push(x86::regs::rax); // ? Re-push callee stub's result for correctness.
        m_as.mov(x86::regs::r10b, x86::ptr(x86::regs::rsp, value_union_size));
        m_as.cmp(x86::regs::r10b, 5); // GUARD <temp>.tag != VTag::v_oops
        m_as.mov(x86::regs::rax, x86::ptr(x86::regs::rsi)); // ? Save result to RAX in case a fast-return of an "oops" is needed.
        m_as.je(m_fast_ret_label);

        // ! Here, restore the native stub's original arguments.
        m_as.mov(x86::regs::rdi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -4 * value_size));

        update_sim_sp(i);

        return true; 
    }

    [[nodiscard]]
    bool JIT::emit_native_call(Inst i) {
        std::cerr << "JIT of VM native_call is unsupported :(\n";

        update_sim_sp(i);

        return false; // todo
    }

    [[nodiscard]]
    bool JIT::emit_ret(Inst i) {
        // ; prepare return of temporary result in RAX...
        m_as.pop(x86::regs::rax);

        // ; collapse stack frame...
        m_as.mov(x86::regs::rsp, x86::regs::rbp);
        m_as.pop(x86::regs::rbp);
        m_as.ret();

        update_sim_sp(i);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_instructions(const Inst* bc, std::size_t n) {
        auto ok = true;

        for (int pc = 0; pc < n && ok; pc++) {
            switch (const auto inst = bc[pc]; inst.op) {
                case Op::nop:
                    ok = emit_nop(inst); break;
                case Op::reserve:
                    ok = emit_reserve(inst); break;
                case Op::get_local:
                    ok = emit_get_local(inst); break;
                case Op::set_local:
                    ok = emit_set_local(inst); break;
                case Op::push_k:
                    ok = emit_push_k(inst); break;
                case Op::dup:
                    ok = emit_dup(inst); break;
                case Op::swap:
                    ok = emit_swap(inst); break;
                case Op::pop:
                    ok = emit_pop(inst); break;
                case Op::add:
                    ok = emit_add(inst); break;
                case Op::sub:
                    ok = emit_sub(inst); break;
                case Op::eq:
                    ok = emit_eq(inst); break;
                case Op::ne:
                    ok = emit_ne(inst); break;
                case Op::lt:
                    ok = emit_lt(inst); break;
                case Op::gt:
                    ok = emit_gt(inst); break;
                case Op::jump_else:
                    ok = emit_jump_else(inst); break;
                case Op::jump_if:
                    ok = emit_jump_if(inst); break;
                case Op::jump:
                    ok = emit_jump(inst); break;
                case Op::call:
                    ok = emit_call(inst); break;
                case Op::native_call:
                    ok = emit_native_call(inst); break;
                case Op::ret:
                    ok = emit_ret(inst); break;
                default:
                    std::cerr << "Invalid VM instruction in JIT at " << pc << '\n';
                    ok = false;
                    break;
            }
        }

        return ok;
    }

    [[nodiscard]]
    bool JIT::emit_general(const compiler::BB* block) {
        // ? 1. Emit code
        // ? 2. Schedule children in respect to RPO: push Falsy then Truthy BB children, but only the present ones. Let the next visitation / iteration do its helper function.
        // ? NOTE: other helpers will handle patching of jumps differently.
        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        const auto child_falsy = block->right_child;
        const auto child_truthy = block->left_child;

        if (!m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (!m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        if (child_falsy == compiler::BB::dud_id && child_truthy == compiler::BB::dud_id) {
            // ? Reset the checkpoint position of the simulated value type stack since this childless, general BB might be concluding 1 of 2 control flow branches.
            m_sim_sp = m_sim_bases.back();
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_if_starter(const compiler::BB* block) {
        m_flows.emplace_back(JIT::Ifs {});
        
        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        m_sim_bases.push_back(m_sim_sp);

        if (const auto child_falsy = block->right_child; !m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
            // ? Here, reset the simulated SP to properly check the latter runtime path. Otherwise, the truthy path leads to a direct successor which is executed, generating more stack temporaries.
            m_sim_sp = m_sim_bases.back();
        }

        if (const auto child_truthy = block->left_child; !m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_if_ender(const compiler::BB* block) {
        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        m_flows.pop_back();
        m_sim_bases.pop_back();

        const auto child_falsy = block->right_child;
        const auto child_truthy = block->left_child;

        if (!m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (!m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        if (child_falsy == compiler::BB::dud_id && child_truthy == compiler::BB::dud_id) {
            // ? Reset the checkpoint position of the simulated value type stack since this childless, general BB might be concluding 1 of 2 control flow branches.
            m_sim_sp = m_sim_bases.back();
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_tbody_ifs(const compiler::BB* block) {
        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        // m_as.jump(std::get<JIT::Ifs>(m_flows.back).end_fbody_label); // ! Handle via emit_jump...
        const auto child_falsy = block->right_child;
        const auto child_truthy = block->left_child; 

        if (!m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (!m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        if (child_falsy == compiler::BB::dud_id && child_truthy == compiler::BB::dud_id) {
            // ? Reset the checkpoint position of the simulated value type stack since this childless, general BB might be concluding 1 of 2 control flow branches.
            m_sim_sp = m_sim_bases.back();
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_fbody_ifs(const compiler::BB* block) {
        m_as.bind(std::get<JIT::Ifs>(m_flows.back()).end_tbody_label); // ? NOTE: maybe a NOP is required before this if this is 1-instruction off.

        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        m_as.bind(std::get<JIT::Ifs>(m_flows.back()).end_fbody_label);

        const auto child_falsy = block->right_child;
        const auto child_truthy = block->left_child;

        if (!m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (!m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            // ? Only a truthy successor would be generated by the bytecode compiler, so resetting the sim stack is unneeded.
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        if (child_falsy == compiler::BB::dud_id && child_truthy == compiler::BB::dud_id) {
            m_sim_sp = m_sim_bases.back();
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_loop_starter(const compiler::BB* block) {
        m_flows.emplace_back(JIT::Loop {});

        auto start_loop_label = m_as.new_label();
        save_start_loop_label(start_loop_label);
        m_as.bind(start_loop_label);

        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        m_sim_bases.push_back(m_sim_sp);

        if (const auto child_falsy = block->right_child; !m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (const auto child_truthy = block->left_child; !m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        // ? Starts of loops will have the body at T-child, mandated by the bytecode compiler.
        return true;
    }

    [[nodiscard]]
    bool JIT::emit_loop_ender(const compiler::BB* block) {
        m_as.bind(std::get<JIT::Loop>(m_flows.back()).exit_loop_label);
        m_flows.pop_back();

        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        const auto child_falsy = block->right_child;
        const auto child_truthy = block->left_child;

        if (m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            // ? Loop enders can only have a T-child if so. No need to reset simulated SP.
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        if (child_falsy == compiler::BB::dud_id && child_truthy == compiler::BB::dud_id) {
            m_sim_sp = m_sim_bases.back();
        }

        return true;
    }

    [[nodiscard]]
    StubResult JIT::generate(const compiler::CFG* cfg, std::int32_t old_chunk_id, const Value* argv, std::uint16_t argc) {
        m_cfg = cfg;

        if (argc > Profs::max_stub_arity) {
            return {};
        }

        m_sim_stack.clear();
        m_sim_bases.clear();
        m_old_chunk_id = old_chunk_id;
        m_sim_sp = 0;
        m_argc = argc;

        for (std::int32_t argv_offset = 0; argv_offset < m_argc; argv_offset++, m_sim_sp++) {
            const auto v_tag = argv[argv_offset].tag;
            m_arg_types[argv_offset] = v_tag;
            m_sim_stack[m_sim_sp] = v_tag;
        }

        m_bbc.clear();
        m_flows.clear();
        m_bbf.clear();
        m_bbf.push_back(m_cfg->get_bb(0));

        m_buf.init(m_rt.environment());
        m_buf.attach(&m_as);

        auto ok = true;

        emit_prelude();

        // ! IMPORTANT: RPO traversal is done in this loop through the CFG... The basic principle is that the parent BB is processed before the Truthy-path BB and finally Falsy-path BB. A BB must exist (link isn't `-1`) to be processed.
        // ? Note: See above helper functions for BB emitting, as they handle each tagged case and properly schedule children BB's. These helpers each save to the visited BB set.
        while (!m_bbf.empty() && ok) {
            auto bb_ptr = m_bbf.back();
            m_bbf.pop_back();

            if (!bb_ptr) {
                continue;
            }

            if (const auto bb_tag = bb_ptr->tag; bb_tag == compiler::BBTag::general) {
                ok = emit_general(bb_ptr);
            } else if (bb_tag == compiler::BBTag::start_ifs) {
                ok = emit_if_starter(bb_ptr);
            } else if (bb_tag == compiler::BBTag::tbody_ifs) {
                ok = emit_tbody_ifs(bb_ptr);
            } else if (bb_tag == compiler::BBTag::fbody_ifs) {
                ok = emit_fbody_ifs(bb_ptr);
            } else if (bb_tag == compiler::BBTag::end_ifs) {
                ok = emit_if_ender(bb_ptr);
            } else if (bb_tag == compiler::BBTag::start_loop) {
                ok = emit_loop_starter(bb_ptr);
            } else if (bb_tag == compiler::BBTag::end_loop) {
                ok = emit_loop_ender(bb_ptr);
            } else {
                ok = false;
            }
        }

        emit_deopt_area();

        StubFn temp_f;

        if (auto gen_error = m_rt.add(&temp_f, &m_buf); gen_error != Error::kOk || !ok) {
            m_rt.release(temp_f);
            return {};
        }

        m_buf.reset();

        return StubResult {
            .f = temp_f,
            .old_cid = m_old_chunk_id,
            .argc = m_argc,
            .arg_types = m_arg_types
        };
    }
}
