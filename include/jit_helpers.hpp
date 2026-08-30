#pragma once

#include "vm.hpp"



namespace toyjit::runtime {
    void jit_add_gen(VM* vm, Value* dest, Value* a1, Value* xa);

    void jit_sub_gen(VM* vm, Value* dest, Value* a1, Value* xa);

    // void jit_mul_gen(VM* vm, Value* dest, Value* a1, Value* xa) {}
    // void jit_div_gen(VM* vm, Value* dest, Value* a1, Value* xa) {}

    void jit_eq_gen(VM* vm, Value* dest, Value* a1, Value* xa);

    void jit_ne_gen(VM* vm, Value* dest, Value* a1, Value* xa);

    void jit_lt_gen(VM* vm, Value* dest, Value* a1, Value* xa);

    void jit_gt_gen(VM* vm, Value* dest, Value* a1, Value* xa);

    void jit_invoke_cid(VM* vm, Value* dest, Value* a1, Value* xa);
}