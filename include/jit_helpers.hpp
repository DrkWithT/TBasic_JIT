#pragma once

#include "vm.hpp"



namespace toyjit::runtime {
    void jit_add_gen(VM* vm, Value* dest, Value* a1, Value* xa);
    void jit_sub_gen(VM* vm, Value* dest, Value* a1, Value* xa);

    // void jit_mul_gen(VM* vm, Value* dest, Value* a1, Value* xa) {} // todo: check todos for these helpers.
    // void jit_div_gen(VM* vm, Value* dest, Value* a1, Value* xa) {} // todo: see above note.

    void jit_eq_gen(VM* vm, Value* dest, Value* a1, Value* xa);
    void jit_ne_gen(VM* vm, Value* dest, Value* a1, Value* xa);
    void jit_lt_gen(VM* vm, Value* dest, Value* a1, Value* xa);
    void jit_gt_gen(VM* vm, Value* dest, Value* a1, Value* xa);

    // ! IMPORTANT: Only use this for stub -> VM trampoline calls.
    void jit_try_sub_call(VM* vm, Value* dest, Value* a1, Value* xa);

    // ! IMPORTANT: Only use this for calling in the deoptimization section of every stub's native code.
    void jit_bailout_stub(VM* vm, Value* dest, Value* a1, Value* xa);
}