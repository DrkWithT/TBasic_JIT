#include <utility>
#include <iostream>
#include <array>
#include <chrono>

#include "bytecode.hpp"
#include "cfg.hpp"
#include "jit.hpp"
#include "vm.hpp"
#include "jit_helpers.hpp"

using namespace toyjit;

constexpr std::array<runtime::HelperFn, static_cast<std::size_t>(runtime::HelperID::last)> helper_table = {
    runtime::jit_add_gen,
    runtime::jit_sub_gen,
    // runtime::jit_mul_gen,
    // runtime::jit_div_gen,
    runtime::jit_eq_gen,
    runtime::jit_ne_gen,
    runtime::jit_lt_gen,
    runtime::jit_gt_gen,
    runtime::jit_try_sub_call
};

int main() {
    // ? Create a recursive Fibonacci 25 program.
    /**
     * @brief Contains starting bytecode.
     * ? RET fibRec(25);
     */
    runtime::Chunk main_logic {
        .bc = {
            // ! BB 0: general setup, children = (-1, -1)
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::reserve},
            // ? <call fibRec(25)>
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 1, .s = 1, .b = 0, .op = runtime::Op::call},
            // ? RET <temp>;
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::ret}
        },
        .konsts = {
            runtime::Value::make_i32(25),  // ? Constant 0
            runtime::Value::make_i32(1)    // ? Constant 1
        },
        .cfg_id = 0,
        .prof_id = 0
    };
    /**
     * ! IMPORTANT: Models the naive Fibonacci TBasic procedure:
     * ? FUN fibRec(n):
     * ?    IF n < 2:
     * ?        RET n;
     * ?    END
     * ? 
     * ?    RET fibRec(n - 1) + fibRec(n - 2);
     * ? END
     */
    runtime::Chunk fib_rec {
        .bc = {
            // ! BB 0: general for setup, children = (1, -1)
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::reserve},
            // ! BB 1: IF STARTER, children = (2, 3)
            // ? compare (n < 2)
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::lt},
            runtime::Inst {.w = 3, .s = 0, .b = 0, .op = runtime::Op::jump_else},
            // ! BB 2: truthy body (left), children = (-1, -1)
            // ? IF n < 2: ...
            // ?    RET n;
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::ret},
            // ? END
            // ! BB 3: falsy body (right), children = (-1, -1)
            // ? <call fibRec(n - 1)>
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::sub},
            runtime::Inst {.w = 1, .s = 1, .b = 0, .op = runtime::Op::call},
            // ? <call fibRec(n - 2)>
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::sub},
            runtime::Inst {.w = 1, .s = 1, .b = 0, .op = runtime::Op::call},
            // ? ... + ...
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::add},
            // ? RET <temp>;
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::ret}
            // ? END
        },
        .konsts = {
            runtime::Value::make_i32(2),    // ? Constant 0
            runtime::Value::make_i32(1)     // ? Constant 1
        },
        .cfg_id = 1,
        .prof_id = 1
    };

    compiler::CFG fib_rec_cfg {fib_rec.konsts.data()};
    const auto fib_rec_bb_0 = fib_rec_cfg.add_bb(
        fib_rec.bc.data(),
        1,
        compiler::BBTag::general
    );
    const auto fib_rec_bb_1 = fib_rec_cfg.add_bb(
        fib_rec.bc.data() + 1,
        4,
        compiler::BBTag::start_ifs
    );
    const auto fib_rec_bb_2 = fib_rec_cfg.add_bb(
        fib_rec.bc.data() + 5,
        2,
        compiler::BBTag::tbody_ifs
    );
    const auto fib_rec_bb_3 = fib_rec_cfg.add_bb(
        fib_rec.bc.data() + 7,
        10,
        compiler::BBTag::fbody_ifs
    );
    fib_rec_cfg.link_bb<'L'>(fib_rec_bb_0, fib_rec_bb_1);
    fib_rec_cfg.link_bb<'L'>(fib_rec_bb_1, fib_rec_bb_2);
    fib_rec_cfg.link_bb<'R'>(fib_rec_bb_1, fib_rec_bb_3);

    std::vector<runtime::Chunk> temp_chunks;
    temp_chunks.push_back(std::move(main_logic));
    temp_chunks.push_back(std::move(fib_rec));

    std::vector<runtime::Profs> temp_profs;
    temp_profs.emplace_back(0, -1);     // ? Don't track heat of main for simplicity.
    temp_profs.emplace_back(0, 1);

    std::vector<compiler::CFG> temp_cfgs;
    temp_cfgs.emplace_back(nullptr);    // ? Note: Don't track top-level code's CFG- it should not be JITed since it runs once.
    temp_cfgs.emplace_back(std::move(fib_rec_cfg));

    runtime::JIT jit;
    runtime::Program pg {
        .chunks = std::move(temp_chunks),
        .profiles = std::move(temp_profs),
        .first_chunk_id = 0 // ? Start at top-level code.
    };
    runtime::VM engine {
        helper_table,
        std::move(temp_cfgs),
        &jit,
        &pg
    };

    // auto stub_result = jit.generate(&sum_n_cfg, 1, 1);
    // [[maybe_unused]] auto stub_fn = stub_result.f;

    auto start_time = std::chrono::steady_clock::now();
    const auto eval_ok = runtime::run_vm(&engine);
    auto running_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);

    std::cerr << running_time.count() << "ms\n";

    return eval_ok ? 0 : 1;
}