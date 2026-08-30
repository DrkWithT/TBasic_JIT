#pragma once

#include <vector>
#include "value.hpp"

namespace toyjit::runtime {
    enum class Op : std::uint8_t {
        nop,
        reserve,
        get_local,
        set_local,
        push_k,
        dup,
        swap,
        pop,
        add,
        sub,
        eq,
        ne,
        lt,
        gt,
        jump_else,
        jump_if,
        jump,
        call,
        native_call,
        ret,
    };

    struct Inst {
        std::int32_t w;     // wide operand
        std::uint16_t s;    // small operand
        std::uint8_t b;     // byte operand / flags
        Op op;
    };

    struct Chunk {
        std::vector<Inst> bc {};
        std::vector<Value> konsts {};
        int cfg_id {-1};
        int prof_id {-1};
    };

    struct Profs {
        static constexpr std::int32_t min_heat_to_jit = 200;
        static constexpr std::int32_t dead_num = -1;

        std::int32_t heat {dead_num};      // IF <= `dead_heat_no_jit`, treat the corresponding chunk as non-JITable.
        std::int32_t chunk_id {dead_num};  // IF >= 0, the chunk ID is valid to track.
    };

    struct Program {
        std::vector<Chunk> chunks;
        std::vector<Profs> profiles;
        std::int32_t first_chunk_id;
    };

    struct VM;

    // ? Holds a native function that is used by the JIT for complex, hard-to-compile operations e.g indexing a list. See README.
    using HelperFn = void(*)(VM* vm, Value* dest, Value* a1);

    // ? Interfaces with a piece of JITed or user-written native code. See README for usage and conventions.
    using StubFn = Value(*)(VM* vm, Value* locals, const Value* cvp, const HelperFn* helpers);

    // ? Tracks optimized JIT stub and its original, interpreted chunk ID (index). If a JIT attempts to JIT the same chunk, a quick cache lookup will find this same stub to use.
    struct StubResult {
        StubFn f {};
        std::int32_t old_cid {-1}; // originally JITed chunk's index, but the chunk remains as bytecode to deopt to.
        std::uint16_t argc {};

        explicit constexpr operator bool() const noexcept {
            return f != nullptr && old_cid != -1;
        }
    };

    // ? Named indexes to JIT glue functions (which help handle complex / generic operations with any Value).
    enum class HelperID : std::int32_t {
        add_gen,
        sub_gen,
        // mul_gen,
        // div_gen,
        eq_gen,
        ne_gen,
        lt_gen,
        gt_gen,
        // invoke_with_n,
        last
    };
}