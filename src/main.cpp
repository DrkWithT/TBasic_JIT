#include <utility>
#include <iostream>

#include "bytecode.hpp"
#include "cfg.hpp"
// #include "vm.hpp"
#include "jit.hpp"

using namespace toyjit;

int main() {
    // todo: Form demo program that calls a naive adder function from 1 to 100- over 1000 iterations. Then invoke the JITed intepreter.
    // ? Create a sample chunk for a procedure adding two VM values. It'll be chunk 1 for the sample program.
    runtime::Chunk add_two {
        .bc = {
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::add},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::ret}
        },
        .konsts = {},
        .cfg_id = 1,
        .prof_id = 1
    };

    compiler::CFG add_two_cfg {};

    [[maybe_unused]]
    const auto add_two_bb_id = add_two_cfg.add_bb(add_two.bc.data(), 4, compiler::BBTag::general);

    runtime::JIT jit;

    // ? JIT the function `addTwo(a, b)`.
    auto stub_result = jit.generate(&add_two_cfg, 1, 2);

    [[maybe_unused]]
    auto stub_fn = stub_result.f;
}