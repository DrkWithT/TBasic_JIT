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
    // ? Create a sample program for a procedure summing 1 to N.
    /**
     * @brief Contains starting bytecode.
     * ? LET x : 1000, dud : 0;
     * ? 
     * ? WHILE x > 0:
     * ?    dud := sumN(100);
     * ?    x := x - 1;
     * ? END
     * ? 
     * ? RET 1;
     */
    runtime::Chunk main_logic {
        .bc = {
            // ! BB 0: general setup, children = (1, -1)
            // ? LET x : 400, dud : 0;
            runtime::Inst {.w = 2, .s = 0, .b = 0, .op = runtime::Op::reserve},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::set_local},
            runtime::Inst {.w = 3, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::set_local},
            // ! BB 1: LOOP STARTER, children = (2, 3)
            // ? (x > 0)
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 3, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::gt},
            runtime::Inst {.w = 9, .s = 0, .b = 0, .op = runtime::Op::jump_else},
            // ! BB 2: loop body (left), children = (-1, -1)
            // ? WHILE n > 0: ...
            // ?     dud := sumN(100);
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 1, .s = 1, .b = 0, .op = runtime::Op::call},
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::set_local},
            // ?     x := x - 1;
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 2, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::sub},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::set_local},
            // ! CONTINUE LOOP... 
            runtime::Inst {.w = -11, .s = 0, .b = 0, .op = runtime::Op::jump},
            // ? END
            // ! BB 3: LOOP ENDER / POST-LOOP (right), children = (-1, -1)
            // ? RET 1;
            runtime::Inst {.w = 2, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::ret}
        },
        .konsts = {
            runtime::Value::make_i32(1000),  // ? Constant 0
            runtime::Value::make_i32(100),   // ? Constant 1
            runtime::Value::make_i32(1),     // ? Constant 2
            runtime::Value::make_i32(0)      // ? Constant 3
        },
        .cfg_id = 0,
        .prof_id = 0
    };
    /**
     * ! IMPORTANT: Models the following TBasic procedure:
     * ? FUN sumN(n):
     * ?      LET x : 0;
     * ?      
     * ?      WHILE n > 0:
     * ?          x := x + n;
     * ?          n := n - 1;
     * ?      END
     * ?      
     * ?      RET x;
     * ? END
     */
    runtime::Chunk sum_n {
        .bc = {
            // ! BB 0: general setup, children = (1, -1)
            // ? LET x : 0;
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::reserve},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::set_local},
            // ! BB 1: LOOP STARTER, children = (2, 3)
            // ? (n > 0)
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::gt},
            runtime::Inst {.w = 10, .s = 0, .b = 0, .op = runtime::Op::jump_else},
            // ! BB 2: loop body (left), children = (-1, -1)
            // ? WHILE n > 0: ...
            // ?      x := x + n;
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::add},
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::set_local},
            // ?     n := n - 1;
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::push_k},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::sub},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::set_local},
            // ! CONTINUE LOOP... 
            runtime::Inst {.w = -12, .s = 0, .b = 0, .op = runtime::Op::jump},
            // ? END
            // ! BB 3: LOOP ENDER / POST-LOOP (right), children = (-1, -1)
            // ? RET x;
            runtime::Inst {.w = 1, .s = 0, .b = 0, .op = runtime::Op::get_local},
            runtime::Inst {.w = 0, .s = 0, .b = 0, .op = runtime::Op::ret}
        },
        .konsts = {
            runtime::Value::make_i32(0), // ? Constant 0
            runtime::Value::make_i32(1)  // ? Constant 1
        },
        .cfg_id = 1,
        .prof_id = 1
    };

    compiler::CFG sum_n_cfg {sum_n.konsts.data()};
    const auto sum_n_bb_0 = sum_n_cfg.add_bb(
        sum_n.bc.data(),
        3,
        compiler::BBTag::general
    );
    const auto sum_n_bb_1 = sum_n_cfg.add_bb(
        sum_n.bc.data() + 3,
        4,
        compiler::BBTag::start_loop
    );
    const auto sum_n_bb_2 = sum_n_cfg.add_bb(
        sum_n.bc.data() + 7,
        9,
        compiler::BBTag::general
    );
    const auto sum_n_bb_3 = sum_n_cfg.add_bb(
        sum_n.bc.data() + 16,
        2,
        compiler::BBTag::end_loop
    );
    sum_n_cfg.link_bb<'L'>(sum_n_bb_0, sum_n_bb_1);
    sum_n_cfg.link_bb<'L'>(sum_n_bb_1, sum_n_bb_2);
    sum_n_cfg.link_bb<'R'>(sum_n_bb_1, sum_n_bb_3);

    std::vector<runtime::Chunk> temp_chunks;
    temp_chunks.push_back(std::move(main_logic));
    temp_chunks.push_back(std::move(sum_n));

    std::vector<runtime::Profs> temp_profs;
    // ? Don't track heat of main for simplicity.
    temp_profs.emplace_back(0, -1);
    temp_profs.emplace_back(0, 1);

    std::vector<compiler::CFG> temp_cfgs;
    temp_cfgs.emplace_back(); // ? Note: Don't handle any main CFG as top-level code runs once.
    temp_cfgs.emplace_back(std::move(sum_n_cfg));

    runtime::JIT jit;
    runtime::Program pg {
        .chunks = std::move(temp_chunks),
        .profiles = std::move(temp_profs),
        .first_chunk_id = 0 // ? Start at top-level / main.
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