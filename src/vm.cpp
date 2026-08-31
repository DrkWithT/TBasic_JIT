// #include <iostream>
#include "vm.hpp"

namespace toyjit::runtime {
    void VM::jit_chunk(std::int32_t current_chunk_id, std::int32_t chunk_id, std::uint16_t argc, const Value* argv) {
        auto& chunk_profile = pg->profiles[chunk_id];

        if (chunk_profile.chunk_id == Profs::dead_num) {
            // ? 1. Is the chunk JIT-banned? It's probably done already.
            return;
        }

        chunk_profile.heat++;

        if (chunk_profile.heat < Profs::min_heat_to_jit) {
            // ? 2. Avoid JITing cold chunks.
            return;
        }

        if (stub_map.contains(chunk_id)) {
            // ! IMPORTANT: stub already exists --> reuse its trampoline by chunk ID!
            // std::cerr << "JIT LOG: stub " << chunk_id << " exists, reuse!\n";

            salvage_jit_trampoline(current_chunk_id, chunk_id);
        } else if (!maybe_stub) {
            // std::cerr << "JIT LOG: chunk " << chunk_id << " is compiling.\n";

            // ? Generate new stub if stub doesn't exist AND if space is available.
            maybe_stub = std::async(
                std::launch::async,
                &JIT::generate,
                jit,
                cfgs.data() + chunk_id,
                chunk_id,
                argv,
                argc
            );
        } else if (maybe_stub->valid()) {
            patch_chunk_calls(current_chunk_id, chunk_id);
        }
    }

    void VM::salvage_jit_trampoline(std::int32_t current_chunk_id, std::int32_t old_callee_chunk_id) {
        const auto existing_trampoline_id = stub_map.at(old_callee_chunk_id);

        for (auto& chunk_code = pg->chunks[current_chunk_id].bc; auto& inst : chunk_code) {
            if (inst.op == Op::call && inst.w == old_callee_chunk_id) {
                inst.w = existing_trampoline_id;
            }
        }
    }

    void VM::patch_chunk_calls(std::int32_t current_chunk_id, std::int32_t old_callee_chunk_id) {
        // std::cerr << "JIT LOG: trampoline of " << old_callee_chunk_id << " is patching in.\n";
        auto jit_result = maybe_stub->get();
        std::vector<Inst> temp_trampoline_bc;

        // ! IMPORTANT: emit all type guards in the trampoline's prefixing bytecode. This practically ensures that only specializable types hit this happy-path stub.
        for (std::int32_t arg_local_idx = 0; arg_local_idx < jit_result.argc; arg_local_idx++) {
            temp_trampoline_bc.emplace_back(arg_local_idx, 0, static_cast<std::uint8_t>(jit_result.arg_types[arg_local_idx]), Op::guard_arg_type);
        }

        // ? Regular Case: retrieve freshly prepared JIT result...
        const std::int32_t next_stub_id = stubs.size();
        stubs.push_back(std::move(jit_result));
        maybe_stub.reset();

        temp_trampoline_bc.emplace_back(next_stub_id, 0, 0, Op::native_call);
        temp_trampoline_bc.emplace_back(0, 0, 0, Op::ret);

        const std::int32_t trampoline_chunk_id = pg->chunks.size();
        const std::int32_t trampoline_prof_id = pg->profiles.size();
        pg->chunks.push_back(Chunk {
            .bc = std::move(temp_trampoline_bc),
            // ! IMPORTANT: Clone the constant buffer to avoid use-after-moves for the original one.
            .konsts = std::vector {pg->chunks[old_callee_chunk_id].konsts},
            .cfg_id = -1,
            .prof_id = trampoline_prof_id
        });
        pg->profiles.emplace_back(); // ? Trampolines have dead profile metadata to avoid bogus JITs.

        for (auto& chunk_code = pg->chunks[current_chunk_id].bc; auto& inst : chunk_code) {
            if (inst.op == Op::call && inst.w == old_callee_chunk_id) {
                inst.w = trampoline_chunk_id;
            }
        }

        stub_map.insert_or_assign(old_callee_chunk_id, trampoline_chunk_id);
        pg->profiles[old_callee_chunk_id].chunk_id = Profs::dead_num;
        pg->profiles[trampoline_prof_id].chunk_id = Profs::dead_num;
    }


    void op_nop(VM* vm, Value* stack) {
        vm->ip++;
    }

    void op_reserve(VM* vm, Value* stack) {
        vm->sp += vm->ip->w;
        vm->ip++;
    }

    void op_get_local(VM* vm, Value* stack) {
        vm->sp++;
        stack[vm->sp] = stack[vm->bp + vm->ip->w];
        vm->ip++;
    }

    void op_set_local(VM* vm, Value* stack) {
        stack[vm->bp + vm->ip->w] = stack[vm->sp];
        vm->sp--;
        vm->ip++;
    }

    void op_push_k(VM* vm, Value* stack) {
        vm->sp++;
        stack[vm->sp] = vm->cvp[vm->ip->w];
        vm->ip++;
    }

    void op_dup(VM* vm, Value* stack) {
        vm->sp++;
        stack[vm->sp] = stack[vm->sp - 1];
        vm->ip++;
    }

    void op_swap(VM* vm, Value* stack) {
        std::swap(stack[vm->sp], stack[vm->sp - 1]);
        vm->ip++;
    }

    void op_pop(VM* vm, Value* stack) {
        vm->sp--;
        vm->ip++;
    }

    void op_add(VM* vm, Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = Value::make_nil();
        } else if (lhs->tag == VTag::v_i32) {
            *lhs = Value::make_i32(
                lhs->data.n + rhs.data.n
            );
        } else {
            *lhs = Value::make_nil();
        }

        vm->sp--;
        vm->ip++;
    }

    void op_sub(VM* vm, Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto rhs = stack[vm->sp];
        
        if (lhs->tag != rhs.tag) {
            *lhs = Value::make_nil();
        } else if (lhs->tag == VTag::v_i32) {
            *lhs = Value::make_i32(
                lhs->data.n - rhs.data.n
            );
        } else {
            *lhs = Value::make_nil();
        }
        
        vm->sp--;
        vm->ip++;
    }

    void op_eq(VM* vm, Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto& rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = Value::make_nil();
        } else {
            switch (rhs.tag) {
            case VTag::v_nil:
                *lhs = Value::make_bool(true);
                break;
            case VTag::v_boolean:
                *lhs = Value::make_bool(lhs->data.byte == rhs.data.byte);
                break;
            case VTag::v_i32:
            case VTag::v_str:
            case VTag::v_obj: default:
                *lhs = Value::make_bool(lhs->data.n == rhs.data.n);
                break;
            }
        }

        vm->sp--;
        vm->ip++;
    }

    void op_ne(VM* vm, Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto& rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = Value::make_nil();
        } else {
            switch (rhs.tag) {
            case VTag::v_nil:
                *lhs = Value::make_bool(false);
                break;
            case VTag::v_boolean:
                *lhs = Value::make_bool(lhs->data.byte != rhs.data.byte);
                break;
            case VTag::v_i32:
            case VTag::v_str:
            case VTag::v_obj: default:
                *lhs = Value::make_bool(lhs->data.n != rhs.data.n);
                break;
            }
        }

        vm->sp--;
        vm->ip++;
    }

    void op_lt(VM* vm, Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto& rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = Value::make_nil();
        } else {
            switch (rhs.tag) {
            case VTag::v_nil:
            case VTag::v_boolean:
                *lhs = Value::make_bool(false);
                break;
            case VTag::v_i32:
                *lhs = Value::make_bool(lhs->data.n < rhs.data.n);
                break;
            case VTag::v_str:
            case VTag::v_obj: default:
                *lhs = Value::make_bool(false);
                break;
            }
        }

        vm->sp--;
        vm->ip++;
    }

    void op_gt(VM* vm, Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto& rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = Value::make_nil();
        } else {
            switch (rhs.tag) {
            case VTag::v_nil:
            case VTag::v_boolean:
                *lhs = Value::make_bool(false);
                break;
            case VTag::v_i32:
                *lhs = Value::make_bool(lhs->data.n > rhs.data.n);
                break;
            case VTag::v_str:
            case VTag::v_obj: default:
                *lhs = Value::make_bool(false);
                break;
            }
        }

        vm->sp--;
        vm->ip++;
    }

    void op_jump_else(VM* vm, Value* stack) {
        if (const auto temp = stack[vm->sp]; temp.tag == VTag::v_boolean && temp.data.byte == 1) {
            vm->sp--;
            vm->ip++;
        } else {
            vm->sp--;
            vm->ip += vm->ip->w;
        }
    }

    void op_jump_if(VM* vm, Value* stack) {
        if (const auto temp = stack[vm->sp]; temp.tag == VTag::v_boolean && temp.data.byte == 0) {
            vm->sp--;
            vm->ip++;
        } else {
            vm->sp--;
            vm->ip += vm->ip->w;
        }
    }

    void op_jump(VM* vm, Value* stack) {
        vm->ip += vm->ip->w;
    }

    void op_call(VM* vm, Value* stack) {
        // todo: add self argument support at frames->back().self_p to emulate OO methods
        const std::int32_t curr_chunk_id = vm->cid;
        const std::int32_t callee_chunk_id = vm->ip->w;
        const std::uint16_t callee_argc = vm->ip->s;
        const std::int32_t callee_bp = vm->sp - callee_argc + 1;
        const std::int32_t caller_bp = vm->bp;
        const Inst* caller_rip = vm->ip + 1;
        const Value* caller_cvp = vm->cvp;
        
        vm->frames.emplace_back(caller_rip, caller_cvp, caller_bp, callee_bp, curr_chunk_id);
        vm->bp = callee_bp;
        vm->ip = vm->pg->chunks[callee_chunk_id].bc.data();
        vm->cvp = vm->pg->chunks[callee_chunk_id].konsts.data();
        vm->cid = callee_chunk_id;

        vm->jit_chunk(curr_chunk_id, callee_chunk_id, callee_argc, stack + callee_bp);
    }

    void op_native_call(VM* vm, Value* stack) {
        const std::int32_t native_id = vm->ip->w;
        const std::int32_t callee_bp = vm->bp; // ! BP is provided by the native trampoline which just passes the args as-is...

        Value v = vm->stubs[native_id].f(vm, stack + callee_bp, vm->cvp, vm->helpers.data());

        vm->sp++;
        stack[vm->sp] = v;
        vm->ip++;
    }

    void op_ret(VM* vm, Value* stack) {
        const auto& [old_rip, old_cvp, caller_bp, callee_bp, callee_cid] = vm->frames.back();

        stack[callee_bp] = stack[vm->sp];
        vm->sp = callee_bp;
        vm->bp = caller_bp;
        vm->ip = old_rip;
        vm->cvp = old_cvp;
        vm->cid = callee_cid;

        vm->frames.pop_back();

        if (vm->frames.size() <= vm->end_depth) {
            if ((vm->flags & std::to_underlying(VMFlags::vm_running)) != 0) {
                vm->flags = std::to_underlying(VMFlags::vm_ok);
            }
        }
    }

    void op_guard_arg_type(VM* vm, Value* stack) {
        if (const auto& arg_ref = vm->stack[vm->bp + vm->ip->w]; arg_ref.tag != static_cast<VTag>(vm->ip->b)) {
            vm->flags &= ~std::to_underlying(VMFlags::vm_running);
            vm->sp++;
            stack[vm->sp] = Value::make_oops();
            vm->frames.pop_back();

            return;
        }

        vm->ip++;
    }

    [[nodiscard]]
    bool run_vm(VM* vm) {
        auto stack_data = vm->stack.data();

        while (vm->running()) {
            switch (vm->ip->op) {
                case Op::nop: op_nop(vm, stack_data); break;
                case Op::reserve: op_reserve(vm, stack_data); break;
                case Op::get_local: op_get_local(vm, stack_data); break;
                case Op::set_local: op_set_local(vm, stack_data); break;
                case Op::push_k: op_push_k(vm, stack_data); break;
                case Op::dup: op_dup(vm, stack_data); break;
                case Op::swap: op_swap(vm, stack_data); break;
                case Op::pop: op_pop(vm, stack_data); break;
                case Op::add: op_add(vm, stack_data); break;
                case Op::sub: op_sub(vm, stack_data); break;
                case Op::eq: op_eq(vm, stack_data); break;
                case Op::ne: op_ne(vm, stack_data); break;
                case Op::lt: op_lt(vm, stack_data); break;
                case Op::gt: op_gt(vm, stack_data); break;
                case Op::jump_else: op_jump_else(vm, stack_data); break;
                case Op::jump_if: op_jump_if(vm, stack_data); break;
                case Op::jump: op_jump(vm, stack_data); break;
                case Op::call: op_call(vm, stack_data); break;
                case Op::native_call: op_native_call(vm, stack_data); break;
                case Op::ret: op_ret(vm, stack_data); break;
                case Op::guard_arg_type: op_guard_arg_type(vm, stack_data); break;
                default: return false;
            }
        }

        return (vm->flags & std::to_underlying(VMFlags::vm_ok)) != 0;
    }
}
