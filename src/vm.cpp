#include "vm.hpp"

namespace toyjit::runtime {
    void VM::jit_chunk(std::int32_t chunk_id, std::uint16_t argc) {
        // ? 1. Is the bytecode chunk able to track profiling information properly?
        if (const auto& chunk = pg->chunks[chunk_id]; chunk.cfg_id == -1 || chunk.prof_id == -1) {
            return;
        }

        // ? 2. Is the chunk able to JIT at all & is hot enough?
        if (auto& chunk_profile = pg->profiles[chunk_id]; chunk_profile.chunk_id == -1) {
            return;
        } else if (chunk_profile.heat < Profs::min_heat_to_jit) {
            // ? Increase heat anyways, as this function is attempted during every VM call.
            chunk_profile.heat++;
            return;
        }

        if (stub_map.contains(chunk_id)) {
            // ! IMPORTANT: stub already exists --> reuse its trampoline by chunk ID!
            maybe_stub = stub_map.at(chunk_id);
        } else if (maybe_stub.index() == 0) {
            // ? Generate new stub if stub doesn't exist AND if space is available.
            maybe_stub = std::async(
                std::launch::async,
                &JIT::generate,
                jit,
                cfgs.data() + chunk_id,
                chunk_id,
                argc
            );
        }
    }

    void VM::patch_chunk_calls(std::int32_t current_chunk_id, std::int32_t old_callee_chunk_id) {
        if (auto& profile_info = pg->profiles[old_callee_chunk_id]; profile_info.chunk_id == Profs::dead_num || maybe_stub.index() == 0) {
            // ? Early case 1: None stub result case.
            return;
        } else if (maybe_stub.index() == 1) {
            // ? Early case 2: Existing trampoline chunk is available by ID.
            const auto existing_trampoline_id = stub_map.at(old_callee_chunk_id);

            for (auto& chunk_code = pg->chunks[current_chunk_id].bc; auto& inst : chunk_code) {
                if (inst.op == Op::call && inst.w == old_callee_chunk_id) {
                    inst.w = existing_trampoline_id;
                }
            }

            return;
        }

        // ? Regular Case: retrieve freshly prepared JIT result...
        const std::int32_t next_stub_id = stubs.size();
        auto prepared_stub_result = std::get<std::future<StubResult>>(maybe_stub).get();
        stubs.push_back(prepared_stub_result);
        maybe_stub = {};

        const std::int32_t trampoline_chunk_id = pg->chunks.size();
        pg->chunks.push_back(Chunk {
            .bc = {
                Inst {.w = next_stub_id, .s = 0, .b = 0, .op = Op::native_call},
                Inst {.w = 0, .s = 0, .b = 0, .op = Op::ret}
            },
            .konsts = {},
            .cfg_id = -1,
            .prof_id = -1
        });

        for (auto& chunk_code = pg->chunks[current_chunk_id].bc; auto& inst : chunk_code) {
            if (inst.op == Op::call && inst.w == old_callee_chunk_id) {
                inst.w = trampoline_chunk_id;
            }
        }

        stub_map.insert_or_assign(old_callee_chunk_id, trampoline_chunk_id);
        pg->profiles[old_callee_chunk_id].chunk_id = Profs::dead_num;
        pg->profiles[old_callee_chunk_id].heat = 0;
    }


    void op_nop(VM* vm, runtime::Value* stack) {
        vm->ip++;
    }

    void op_reserve(VM* vm, runtime::Value* stack) {
        vm->sp += vm->ip->w;
    }

    void op_get_local(VM* vm, runtime::Value* stack) {
        vm->sp++;
        stack[vm->sp] = stack[vm->bp + vm->ip->w];
        vm->ip++;
    }

    void op_set_local(VM* vm, runtime::Value* stack) {
        stack[vm->bp + vm->ip->w] = stack[vm->sp];
        vm->sp--;
        vm->ip++;
    }

    void op_push_k(VM* vm, runtime::Value* stack) {
        vm->sp++;
        stack[vm->sp] = vm->cvp[vm->ip->w];
        vm->ip++;
    }

    void op_dup(VM* vm, runtime::Value* stack) {
        vm->sp++;
        stack[vm->sp] = stack[vm->sp - 1];
        vm->ip++;
    }

    void op_swap(VM* vm, runtime::Value* stack) {
        std::swap(stack[vm->sp], stack[vm->sp - 1]);
        vm->ip++;
    }

    void op_pop(VM* vm, runtime::Value* stack) {
        vm->sp--;
        vm->ip++;
    }

    void op_add(VM* vm, runtime::Value* stack) {
        vm->sp--;
        auto lhs = stack + vm->sp;
        const auto rhs = stack[vm->sp + 1];

        if (lhs->tag != rhs.tag) {
            *lhs = runtime::Value::make_nil();
        } else if (lhs->tag == runtime::VTag::v_i32) {
            *lhs = runtime::Value::make_i32(
                lhs->data.n + rhs.data.n
            );
        } else {
            *lhs = runtime::Value::make_nil();
        }

        vm->ip++;
    }

    void op_sub(VM* vm, runtime::Value* stack) {
        vm->sp--;
        auto lhs = stack + vm->sp;
        const auto rhs = stack[vm->sp + 1];

        if (lhs->tag != rhs.tag) {
            *lhs = runtime::Value::make_nil();
        } else if (lhs->tag == runtime::VTag::v_i32) {
            *lhs = runtime::Value::make_i32(
                lhs->data.n - rhs.data.n
            );
        } else {
            *lhs = runtime::Value::make_nil();
        }

        vm->ip++;
    }

    void op_eq(VM* vm, runtime::Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto& rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = runtime::Value::make_nil();
        } else {
            switch (rhs.tag) {
            case runtime::VTag::v_nil:
                *lhs = runtime::Value::make_bool(true);
                break;
            case runtime::VTag::v_boolean:
                *lhs = runtime::Value::make_bool(lhs->data.byte == rhs.data.byte);
                break;
            case runtime::VTag::v_i32:
            case runtime::VTag::v_str:
            case runtime::VTag::v_obj: default:
                *lhs = runtime::Value::make_bool(lhs->data.n == rhs.data.n);
                break;
            }
        }

        vm->ip++;
    }

    void op_ne(VM* vm, runtime::Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto& rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = runtime::Value::make_nil();
        } else {
            switch (rhs.tag) {
            case runtime::VTag::v_nil:
                *lhs = runtime::Value::make_bool(false);
                break;
            case runtime::VTag::v_boolean:
                *lhs = runtime::Value::make_bool(lhs->data.byte != rhs.data.byte);
                break;
            case runtime::VTag::v_i32:
            case runtime::VTag::v_str:
            case runtime::VTag::v_obj: default:
                *lhs = runtime::Value::make_bool(lhs->data.n != rhs.data.n);
                break;
            }
        }

        vm->ip++;
    }

    void op_lt(VM* vm, runtime::Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto& rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = runtime::Value::make_nil();
        } else {
            switch (rhs.tag) {
            case runtime::VTag::v_nil:
            case runtime::VTag::v_boolean:
                *lhs = runtime::Value::make_bool(false);
                break;
            case runtime::VTag::v_i32:
                *lhs = runtime::Value::make_bool(lhs->data.n < rhs.data.n);
                break;
            case runtime::VTag::v_str:
            case runtime::VTag::v_obj: default:
                *lhs = runtime::Value::make_bool(false);
                break;
            }
        }

        vm->ip++;
    }

    void op_gt(VM* vm, runtime::Value* stack) {
        auto lhs = stack + vm->sp - 1;
        const auto& rhs = stack[vm->sp];

        if (lhs->tag != rhs.tag) {
            *lhs = runtime::Value::make_nil();
        } else {
            switch (rhs.tag) {
            case runtime::VTag::v_nil:
            case runtime::VTag::v_boolean:
                *lhs = runtime::Value::make_bool(false);
                break;
            case runtime::VTag::v_i32:
                *lhs = runtime::Value::make_bool(lhs->data.n > rhs.data.n);
                break;
            case runtime::VTag::v_str:
            case runtime::VTag::v_obj: default:
                *lhs = runtime::Value::make_bool(false);
                break;
            }
        }

        vm->ip++;
    }

    void op_jump_else(VM* vm, runtime::Value* stack) {
        if (const auto temp = stack[vm->sp]; temp.tag == runtime::VTag::v_boolean && temp.data.byte == 1) {
            vm->sp--;
            vm->ip++;
        } else {
            vm->sp--;
            vm->ip += vm->ip->w;
        }
    }

    void op_jump_if(VM* vm, runtime::Value* stack) {
        if (const auto temp = stack[vm->sp]; temp.tag == runtime::VTag::v_boolean && temp.data.byte == 0) {
            vm->sp--;
            vm->ip++;
        } else {
            vm->sp--;
            vm->ip += vm->ip->w;
        }
    }

    void op_jump(VM* vm, runtime::Value* stack) {
        vm->ip += vm->ip->w;
    }

    void op_call(VM* vm, runtime::Value* stack) {
        const std::int32_t curr_chunk_id = vm->cid;
        const std::int32_t chunk_id = vm->ip->w;
        const std::uint16_t callee_argc = vm->ip->s;
        const std::int32_t callee_bp = vm->sp - callee_argc;
        const std::int32_t caller_bp = vm->bp;
        const runtime::Inst* caller_rip = vm->ip + 1;
        const runtime::Value* caller_cvp = vm->cvp;
        
        vm->frames.emplace_back(caller_rip, caller_cvp, caller_bp, callee_bp, curr_chunk_id);
        vm->bp = callee_bp;
        vm->ip = vm->pg->chunks[chunk_id].bc.data();
        vm->cvp = vm->pg->chunks[chunk_id].konsts.data();
        vm->cid = chunk_id;

        vm->jit_chunk(chunk_id, callee_argc);
        vm->patch_chunk_calls(curr_chunk_id, chunk_id);
    }

    void op_native_call(VM* vm, runtime::Value* stack) {
        const std::int32_t native_id = vm->ip->w;
        const std::uint16_t callee_argc = vm->ip->s;
        const std::int32_t callee_bp = vm->bp; // ! BP is provided by the native trampoline which just passes the args as-is...

        runtime::Value v = vm->stubs[native_id].f(vm, stack + callee_bp, vm->cvp, vm->helpers.data());

        vm->sp++;
        stack[vm->sp] = v;
        vm->ip++;
    }

    void op_ret(VM* vm, runtime::Value* stack) {
        const auto& [old_rip, old_cvp, caller_bp, callee_bp, callee_cid] = vm->frames.back();

        stack[callee_bp] = stack[vm->sp];
        vm->sp = callee_bp;
        vm->bp = caller_bp;
        vm->ip = old_rip;
        vm->cvp = old_cvp;
        vm->cid = callee_cid;

        vm->frames.pop_back();

        if (vm->frames.empty()) {
            if ((vm->flags & std::to_underlying(VMFlags::vm_running)) != 0) {
                vm->flags = std::to_underlying(VMFlags::vm_ok);
            }
        }
    }

    [[nodiscard]]
    bool run_vm(VM* vm) {
        auto stack_data = vm->stack.data();

        while (vm->running()) {
            switch (vm->ip->op) {
                case runtime::Op::nop: op_nop(vm, stack_data); break;
                case runtime::Op::reserve: op_reserve(vm, stack_data); break;
                case runtime::Op::get_local: op_get_local(vm, stack_data); break;
                case runtime::Op::set_local: op_set_local(vm, stack_data); break;
                case runtime::Op::push_k: op_push_k(vm, stack_data); break;
                case runtime::Op::dup: op_dup(vm, stack_data); break;
                case runtime::Op::swap: op_swap(vm, stack_data); break;
                case runtime::Op::pop: op_pop(vm, stack_data); break;
                case runtime::Op::add: op_add(vm, stack_data); break;
                case runtime::Op::sub: op_sub(vm, stack_data); break;
                case runtime::Op::eq: op_eq(vm, stack_data); break;
                case runtime::Op::ne: op_ne(vm, stack_data); break;
                case runtime::Op::lt: op_lt(vm, stack_data); break;
                case runtime::Op::gt: op_gt(vm, stack_data); break;
                case runtime::Op::jump_else: op_jump_else(vm, stack_data); break;
                case runtime::Op::jump_if: op_jump_if(vm, stack_data); break;
                case runtime::Op::jump: op_jump(vm, stack_data); break;
                case runtime::Op::call: op_call(vm, stack_data); break;
                case runtime::Op::native_call: op_native_call(vm, stack_data); break;
                case runtime::Op::ret: op_ret(vm, stack_data); break;
                default: return false;
            }
        }

        return (vm->flags & std::to_underlying(VMFlags::vm_ok)) != 0;
    }
}
