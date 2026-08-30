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

    void JIT::emit_prelude() {
        // ; set up stack frame
        // push rbp
        m_as.push(x86::regs::rbp);
        // mov rbp, rsp
        m_as.mov(x86::regs::rbp, x86::regs::rsp);
        // ! Here, preserve copies of the parameters' register data for this NativeFn stub: VM* vm, Value* locals, const Value* cvp, const HelperFn* helpers are RDI, RSI, RDX, RCX. RDI is left the same anyways.
        m_as.push(x86::regs::rsi);
        m_as.push(x86::regs::rdx);
        m_as.push(x86::regs::rcx);
    }

    [[nodiscard]]
    bool JIT::emit_nop([[maybe_unused]] Inst i) {
        return true;
    }

    [[nodiscard]]
    bool JIT::emit_reserve(Inst i) {
        const auto non_arg_locals_count = i.w;

        // ; Reserve N slots for local Value values- These are set by initializer values from other computations via `set_local` code.
        m_as.sub(x86::regs::rsp, non_arg_locals_count * value_size);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_get_local(Inst i) {
        // ! The raw offset here is actually relative to stack[CALLEE_BP] where local 0 is. This means that the stub should offset directly from `locals` which points back to `stack[CALLEE_BP]`.
        const auto local_offset = i.w;

        // sub rsp, 8
        m_as.sub(x86::regs::rsp, value_size);

        
        // ! Handle case of argument locals (Locals 0 to ARGC - 1).
        if (local_offset < m_argc) {
            // mov r8, [rsi + local_offset * 8] ... Base.Index.Scale
            m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rsi, local_offset * value_size));
        } else {
            const auto non_arg_local_offset = local_offset - m_argc;

            m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rbp, -(non_arg_local_offset + 5) * value_size)); // ! IMPORTANT: Use RBP + 5 to start the native non-arg locals above the 4 preserved parameters.
        }

        // mov [rsp], r8
        m_as.mov(x86::ptr(x86::regs::rsp), x86::regs::r8);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_set_local(Inst i) {
        const auto local_offset = i.w;

        // mov r8, [rsp]
        m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rsp, 0));

        // add rsp, 8
        m_as.add(x86::regs::rsp, value_size);

        if (local_offset < m_argc) {
            // mov [rsi + local_offset * 8], r8 ... Base.Index.Scale
            m_as.mov(x86::ptr(x86::regs::rsi, local_offset * value_size), x86::regs::r8);
        } else {
            const auto non_arg_local_offset = local_offset - m_argc;

            // mov [rbp + (-non_arg_local_offset) * 8], r8
            m_as.mov(x86::ptr(x86::regs::rbp, -(non_arg_local_offset + 5) * value_size), x86::regs::r8);
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_push_k(Inst i) {
        const auto konst_offset = i.w;

        // mov r8, [rdx + konst_offset * 8] ... Base.Index.Scale
        m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rdx, konst_offset * value_size));
        // sub rsp, 8
        m_as.sub(x86::regs::rsp, value_size);
        // mov [rsp], r8
        m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r8);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_dup([[maybe_unused]] Inst i) {
        // mov r8, [rsp]
        m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rsp, 0));

        // sub rsp, 8
        m_as.sub(x86::regs::rsp, value_size);

        // mov [rsp], r8
        m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r8);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_swap([[maybe_unused]] Inst i) {
        // mov r9, [rsp]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rsp, 0));
        // mov r8, [rsp + 8]
        m_as.mov(x86::regs::r8, x86::ptr(x86::regs::rsp, value_size));
        // mov [rsp + 8], r9
        m_as.mov(x86::ptr(x86::regs::rsp, value_size), x86::regs::r9);
        // mov [rsp], r8
        m_as.mov(x86::ptr(x86::regs::rsp, 0), x86::regs::r8);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_pop([[maybe_unused]] Inst i) {
        m_as.add(x86::regs::rsp, value_size);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_add(Inst i) {
        // ! NOTE: call generic helper here, as this JIT is too naive to know if all possible temporaries are of the same type from the bytecode instructions alone.
        const auto helper_id = std::to_underlying(HelperID::add_gen);

        // ; store `Value* target` (dest)
        // mov r8, rsp
        m_as.mov(x86::regs::r8, x86::regs::rsp);

        // add r8, 8
        m_as.add(x86::regs::r8, value_size);

        // mov rsi, r8
        m_as.mov(x86::regs::rsi, x86::regs::r8);

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_sub(Inst i) {
        const auto helper_id = std::to_underlying(HelperID::sub_gen);

        // ; store `Value* target` (dest)
        // mov r8, rsp
        m_as.mov(x86::regs::r8, x86::regs::rsp);

        // add r8, 8
        m_as.add(x86::regs::r8, value_size);

        // mov rsi, r8
        m_as.mov(x86::regs::rsi, x86::regs::r8);

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_eq(Inst i) {
        const auto helper_id = std::to_underlying(HelperID::eq_gen);

        // ; store `Value* target` (dest)
        // mov r8, rsp
        m_as.mov(x86::regs::r8, x86::regs::rsp);

        // add r8, 8
        m_as.add(x86::regs::r8, value_size);

        // mov rsi, r8
        m_as.mov(x86::regs::rsi, x86::regs::r8);

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_ne(Inst i) {
        const auto helper_id = std::to_underlying(HelperID::ne_gen);

        // ; Register rdi is the same (VM* vm), but save RSI, RDX to preserve the arg-local ptr and cvp ptr
        // ; store `Value* target` (dest)
        // mov r8, rsp
        m_as.mov(x86::regs::r8, x86::regs::rsp);

        // add r8, 8
        m_as.add(x86::regs::r8, value_size);

        // mov rsi, r8
        m_as.mov(x86::regs::rsi, x86::regs::r8);

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_lt(Inst i) {
        const auto helper_id = std::to_underlying(HelperID::lt_gen);

        // ; store `Value* target` (dest)
        // mov r8, rsp
        m_as.mov(x86::regs::r8, x86::regs::rsp);

        // add r8, 8
        m_as.add(x86::regs::r8, value_size);

        // mov rsi, r8
        m_as.mov(x86::regs::rsi, x86::regs::r8);

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_gt(Inst i) {
        const auto helper_id = std::to_underlying(HelperID::gt_gen);

        // ; store `Value* target` (dest)
        // mov r8, rsp
        m_as.mov(x86::regs::r8, x86::regs::rsp);

        // add r8, 8
        m_as.add(x86::regs::r8, value_size);

        // mov rsi, r8
        m_as.mov(x86::regs::rsi, x86::regs::r8);

        // ; store `Value* a1` (src) Value ptr
        // mov rdx, rsp
        m_as.mov(x86::regs::rdx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [rcx + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::rcx, helper_id * ptr_size));

        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, restore this callee's arg-local ptr and cvp ptr in RSI, RDX, RCX respectively. However, the `VM* vm` ptr in RDI stays the same.
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -3 * value_size));
        m_as.add(x86::regs::rsp, value_size);

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_jump_else([[maybe_unused]] Inst i) {
        // ; Check if <temp>.data.byte is not zero, popping it and jumping to an unresolved label if so.
        m_as.mov(x86::regs::r8d, x86::ptr(x86::regs::rsp, 0));

        m_as.add(x86::regs::rsp, value_size);

        // ; If the byte for boolean values is 0, expect it as `FALSE` to jump ahead.
        m_as.cmp(x86::regs::r8d, 0);

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
        // ...

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_jump_if(Inst i) {
        std::cerr << "JIT of VM jump-if-true is unsupported :(\n";
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
        const auto helper_id = std::to_underlying(HelperID::invoke_cid);
        const std::int32_t callee_chunk_id = i.w;
        const std::int32_t callee_argc = i.s;

        // ? Form a pointer to N Values to feed the helper via `Value* a1`.
        m_as.mov(x86::regs::r9, x86::regs::rsp);
        if (callee_argc > 0) {
            m_as.add(x86::regs::r9, value_size * (callee_argc - 1));
        }

        // ! Here, store `Value* target` (dest) which is equal to `Value* a1` in this specifc case since the invoke_cid helper needs N natively-passed args to push onto the VM stack for the cid's trampoline call with arg-locals.
        // mov rsi, r9
        m_as.mov(x86::regs::rsi, x86::regs::r9);
        // mov rdx, r9
        m_as.mov(x86::regs::rdx, x86::regs::r9);

        // ! Here, store a Value[2] behind `Value* xa` (src) Value ptr: chunk-ID and argc.
        // ! BUT, the stack is LIFO and grows lower, so push argc before chunk-ID for the helper, allowing chunk-ID to reside in xa[0].
        // mov BYTE PTR [rsp], 2     ; put Value {.data.n = <argc>, .tag = VTag::v_i32}
        m_as.mov(x86::regs::r8, 2);
        m_as.sub(x86::regs::rsp, value_union_size);
        m_as.mov(x86::regs::ptr(x86::regs::rsp, 0), x86::regs::r8b);
        // ; mov DWORD PTR [rsp], <argc>
        m_as.mov(x86::regs::r8, callee_argc);
        m_as.sub(x86::regs::rsp, value_union_size);
        m_as.mov(x86::regs::ptr(x86::regs::rsp, 0), x86::regs::r8d);

        // mov BYTE PTR [rsp], 2     ; put Value {.data.n = <chunk-ID>, .tag = VTag::v_i32}
        m_as.mov(x86::regs::r8, 2);
        m_as.sub(x86::regs::rsp, value_union_size);
        m_as.mov(x86::regs::ptr(x86::regs::rsp, 0), x86::regs::r8b);
        // ; mov DWORD PTR [rsp], <chunk-ID>
        m_as.mov(x86::regs::r8, callee_chunk_id);
        m_as.sub(x86::regs::rsp, value_union_size);
        m_as.mov(x86::regs::ptr(x86::regs::rsp, 0), x86::regs::r8d);

        // ! Here, prepare `Value* xa` of Value[2].
        // mov r9, rcx
        m_as.mov(x86::regs::r9, x86::regs::rcx); // ? Copy RCX ptr to helper functions ptr.
        // mov rcx, rsp
        m_as.mov(x86::regs::rcx, x86::regs::rsp);

        // ; call helper on these arguments...
        // mov r9, [r9 + helper_id * 8]
        m_as.mov(x86::regs::r9, x86::ptr(x86::regs::r9, helper_id * ptr_size));

        // ! Here, the invoke_cid() helper must take the chunk-ID and argc to properly call the chunk, 
        // call r9
        m_as.call(x86::regs::r9);

        // ! Here, remove the used temporaries behind `Value* a1`.
        m_as.add(x86::regs::rsp, 2 * value_size);

        if (callee_argc > 0) {
            m_as.add(x86::regs::rsp, value_size * (callee_argc - 1));
        }

        // ! Here, restore the native stub's original arguments.
        m_as.mov(x86::regs::rsi, x86::ptr(x86::regs::rbp, -value_size));
        m_as.mov(x86::regs::rdx, x86::ptr(x86::regs::rbp, -2 * value_size));
        m_as.mov(x86::regs::rcx, x86::ptr(x86::regs::rbp, -3 * value_size));

        return true; 
    }

    [[nodiscard]]
    bool JIT::emit_native_call(Inst i) {
        std::cerr << "JIT of VM native_call is unsupported :(\n";
        return false; // todo
    }

    [[nodiscard]]
    bool JIT::emit_ret(Inst i) {
        // ; prepare return of temporary result in RAX...
        // mov rax, [rsp]
        m_as.mov(x86::regs::rax, x86::ptr(x86::regs::rsp, 0));

        // ; collapse stack frame...
        // mov rsp, rbp
        m_as.mov(x86::regs::rsp, x86::regs::rbp);

        // ; restore caller RBP
        // pop rbp
        m_as.pop(x86::regs::rbp);
        m_as.ret();

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

        if (const auto child_falsy = block->right_child; !m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (const auto child_truthy = block->left_child; !m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_if_starter(const compiler::BB* block) {
        m_flows.emplace_back(JIT::Ifs {});

        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        if (const auto child_falsy = block->right_child; !m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
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

        if (const auto child_falsy = block->right_child; !m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (const auto child_truthy = block->left_child; !m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_tbody_ifs(const compiler::BB* block) {
        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        // m_as.jump(std::get<JIT::Ifs>(m_flows.back).end_fbody_label); // ! Handle via emit_jump...

        if (const auto child_falsy = block->right_child; !m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (const auto child_truthy = block->left_child; !m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
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

        if (const auto child_falsy = block->right_child; !m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (const auto child_truthy = block->left_child; !m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
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

        if (const auto child_falsy = block->right_child; !m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (const auto child_truthy = block->left_child; !m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        return true;
    }

    [[nodiscard]]
    bool JIT::emit_loop_ender(const compiler::BB* block) {
        m_as.bind(std::get<JIT::Loop>(m_flows.back()).exit_loop_label);
        m_flows.pop_back();

        if (!emit_instructions(block->data, block->n)) {
            return false;
        }

        if (const auto child_falsy = block->right_child; m_bbc.contains(child_falsy) && child_falsy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_falsy));
        }

        if (const auto child_truthy = block->left_child; m_bbc.contains(child_truthy) && child_truthy != compiler::BB::dud_id) {
            m_bbf.push_back(m_cfg->get_bb(child_truthy));
        }

        return true;
    }

    [[nodiscard]]
    StubResult JIT::generate(const compiler::CFG* cfg, std::int32_t old_chunk_id, std::uint16_t argc) {
        m_cfg = cfg;
        m_old_chunk_id = old_chunk_id;
        m_argc = argc;

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

        StubFn temp_f;

        if (auto gen_error = m_rt.add(&temp_f, &m_buf); gen_error != Error::kOk || !ok) {
            return {};
        }

        m_buf.reset();

        return StubResult {
            .f = temp_f,
            .old_cid = m_old_chunk_id,
            .argc = m_argc
        };
    }
}
