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
}