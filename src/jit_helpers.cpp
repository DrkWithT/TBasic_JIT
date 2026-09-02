#include "jit_helpers.hpp"



namespace toyjit::runtime {
    void jit_add_gen(VM* vm, Value* dest, Value* a1, Value* xa) {
        if (const auto lhs_tag = dest->tag; lhs_tag != a1->tag) {
            *dest = Value::make_nil();
        } else if (lhs_tag == VTag::v_i32) {   
            dest->data.n += a1->data.n;
        } else {
            *dest = Value::make_nil();
        }
    }

    void jit_sub_gen(VM* vm, Value* dest, Value* a1, Value* xa) {
        if (const auto lhs_tag = dest->tag; lhs_tag != a1->tag) {
            *dest = Value::make_nil();
        } else if (lhs_tag == VTag::v_i32) {   
            dest->data.n -= a1->data.n;
        } else {
            *dest = Value::make_nil();
        }
    }

    // void jit_mul_gen(VM* vm, Value* dest, Value* a1, Value* xa) {} // todo: check todos for these helpers.
    // void jit_div_gen(VM* vm, Value* dest, Value* a1, Value* xa) {} // todo: see above note.

    void jit_eq_gen(VM* vm, Value* dest, Value* a1, Value* xa) {
        if (dest->tag != a1->tag) {
            *dest = runtime::Value::make_nil();
        } else {
            switch (a1->tag) {
            case runtime::VTag::v_nil:
                *dest = runtime::Value::make_bool(true);
                break;
            case runtime::VTag::v_boolean:
                *dest = runtime::Value::make_bool(dest->data.byte == a1->data.byte);
                break;
            default:
                *dest = runtime::Value::make_bool(dest->data.n == a1->data.n);
                break;
            }
        }
    }

    void jit_ne_gen(VM* vm, Value* dest, Value* a1, Value* xa) {
        if (dest->tag != a1->tag) {
            *dest = runtime::Value::make_nil();
        } else {
            switch (a1->tag) {
            case runtime::VTag::v_nil:
                *dest = runtime::Value::make_bool(false);
                break;
            case runtime::VTag::v_boolean:
                *dest = runtime::Value::make_bool(dest->data.byte != a1->data.byte);
                break;
            default:
                *dest = runtime::Value::make_bool(dest->data.n != a1->data.n);
                break;
            }
        }
    }

    void jit_lt_gen(VM* vm, Value* dest, Value* a1, Value* xa) {
        if (dest->tag != a1->tag) {
            *dest = Value::make_bool(false);
        } else {
            switch (a1->tag) {
            case VTag::v_i32:
                *dest = Value::make_bool(dest->data.n < a1->data.n);
                break;
            default:
                *dest = Value::make_bool(false);
                break;
            }
        }
    }

    void jit_gt_gen(VM* vm, Value* dest, Value* a1, Value* xa) {
        if (dest->tag != a1->tag) {
            *dest = Value::make_bool(false);
        } else {
            switch (a1->tag) {
            case VTag::v_i32:
                *dest = Value::make_bool(dest->data.n > a1->data.n);
                break;
            default:
                *dest = Value::make_bool(false);
                break;
            }
        }
    }

    void jit_try_sub_call(VM* vm, Value* dest, Value* a1, Value* xa) {
        const std::int32_t chunk_id = xa[0].data.n;
        const std::uint16_t callee_argc = xa[1].data.n;

        for (std::int32_t load_arg_i = 0; load_arg_i < callee_argc; load_arg_i++) {
            vm->sp++;
            // ? NOTE: a1 points to the native stack which grows downwards, so the Values must be copied in a descending direction. Very cursed looking but is ok in practice. >:)
            vm->stack[vm->sp] = a1[-load_arg_i];
        }

        *dest = vm->sub_call(chunk_id, callee_argc);
    }

    void jit_bailout_stub(VM* vm, Value* dest, Value* a1, Value* xa) {
        std::int32_t lowest_trampoline_id = -1;
        // ? NOTE: Only trampolines can call native stubs, so deopt must remove the VM trampoline frame(s) first. Then the snapshot ptr can be found.
        for ( ; !vm->frames.back().snapshot; vm->frames.pop_back()) {
            lowest_trampoline_id = vm->frames.back().curr_cid;
        }

        // ! Check for an unwind count of 0 which means that bailout already happened from another failed trampoline stub long ago...
        if (lowest_trampoline_id == -1) {
            *dest = Value::make_oops();
            return;
        }

        const auto [fallback_rip, fallback_cvp, fallback_cid, fallback_bp, fallback_sp] = *vm->frames.back().snapshot;
        const std::int32_t trampoline_stub_pos = vm->pg->chunks[lowest_trampoline_id].bc[0].w;
        const auto fallback_generic_cid = vm->stubs[lowest_trampoline_id].old_cid;

        vm->ip = fallback_rip;
        vm->cvp = fallback_cvp;
        vm->bp = fallback_bp;
        vm->sp = fallback_sp;
        vm->cid = fallback_cid;

        // ! IMPORTANT: revert all call sites and delete the trampoline & its stub.
        for (auto& fallback_chunk_bc = vm->pg->chunks[fallback_cid].bc; auto& inst : fallback_chunk_bc) {
            if (inst.op == Op::call && inst.w == lowest_trampoline_id) {
                inst.w = fallback_generic_cid;
            }
        }

        // ? Destroy stub to reclaim machine code buffer's memory.
        // ! FIXME: The stub struct should be recycled, so the stub_map entry MUST REMAIN and BE REUSED via checking for NULLPTR `<stub>.f`.
        vm->jit->runtime().release(vm->stubs[trampoline_stub_pos].f);
        vm->stubs[trampoline_stub_pos].f = nullptr;
        vm->stubs[trampoline_stub_pos].arg_types = {};
        vm->stub_map.erase(fallback_generic_cid);

        // ? Finish deopt by a simple & naive policy: banning the function from JITing ever again.
        vm->pg->profiles[fallback_generic_cid].heat = Profs::dead_num;
        vm->pg->profiles[fallback_generic_cid].chunk_id = Profs::dead_num;
        *dest = Value::make_oops();
    }
}