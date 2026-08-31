#pragma once

#if defined(_WIN32)
    #error "Windows is not supported for this demo."
#endif

#include "asmjit/x86.h"

#include <utility>
#include <array>
#include <vector>
#include <variant>
#include <unordered_set>

#include "cfg.hpp"



namespace toyjit::runtime {
    // ? Converts bytecode CFGs to JITed machine code, wrapped in a special result. This result contains the function pointer to the machine code, the old & new chunk IDs, and more.
    class JIT {
        static constexpr std::int32_t sim_stack_size = 512;
        static constexpr std::int32_t value_size = sizeof(Value);
        static constexpr std::int32_t ptr_size = sizeof(void*);
        static constexpr std::int32_t value_union_size = sizeof(std::int32_t);

        struct Loop {
            asmjit::Label start_loop_label {};
            asmjit::Label exit_loop_label {};
        };

        struct Ifs {
            asmjit::Label end_tbody_label {};  // where the skip of the if body to the else's arrives
            asmjit::Label end_fbody_label {};  // where the skip of the else body arrives
        };

        using ActiveFlow = std::variant<Ifs, Loop>;

        asmjit::CodeHolder m_buf {};
        asmjit::x86::Assembler m_as {};
        asmjit::JitRuntime m_rt {};
        std::unordered_set<int> m_bbc {};           // visited BB census
        std::vector<ActiveFlow> m_flows {};         // tracking records for patching if-else statements / loops...
        std::vector<const compiler::BB*> m_bbf {};  // BB frontier (BB nodes are scheduled in RPO- parent first, T, then F childs...)
        std::vector<VTag> m_sim_stack {};           // Simulates VM stack state for types to ease choice of type specialized ASM ops.
        std::vector<std::int32_t> m_sim_bases {};   // Simulates "BP"s for the current BB's visited, offsetting into m_sim_stack / restored upon.

        const compiler::CFG* m_cfg {};
        std::array<VTag, Profs::max_stub_arity> m_arg_types {};
        std::int32_t m_old_chunk_id {};
        std::int32_t m_sim_sp {};               // Simulates SP of m_sim_stack.
        std::uint16_t m_argc {};

        void save_start_loop_label(asmjit::Label l);

        void save_exit_loop_label(asmjit::Label l);

        void save_end_tbody_label(asmjit::Label l);

        void save_end_fbody_label(asmjit::Label l);

        bool update_sim_sp_with_nonary(Op op);
        bool update_sim_sp_with_unary(Op op, std::int32_t w);
        bool update_sim_sp_with_binary(Op op, std::int32_t w, std::uint16_t s);

        // ! IMPORTANT: call this BEFORE dynamic asm emission for opcodes that initially push values. Otherwise, call this AFTER dynamic asm emission.
        [[nodiscard]]
        bool update_sim_sp(Inst i);

        void emit_prelude();

        [[nodiscard]]
        bool emit_nop([[maybe_unused]] Inst i);

        [[nodiscard]]
        bool emit_reserve(Inst i);

        [[nodiscard]]
        bool emit_get_local(Inst i);

        [[nodiscard]]
        bool emit_set_local(Inst i);

        [[nodiscard]]
        bool emit_push_k(Inst i);

        [[nodiscard]]
        bool emit_dup([[maybe_unused]] Inst i);

        [[nodiscard]]
        bool emit_swap([[maybe_unused]] Inst i);

        [[nodiscard]]
        bool emit_pop([[maybe_unused]] Inst i);

        [[nodiscard]]
        bool emit_add(Inst i);

        [[nodiscard]]
        bool emit_sub(Inst i);

        [[nodiscard]]
        bool emit_eq(Inst i);

        [[nodiscard]]
        bool emit_ne(Inst i);

        [[nodiscard]]
        bool emit_lt(Inst i);

        [[nodiscard]]
        bool emit_gt(Inst i);

        [[nodiscard]]
        bool emit_jump_else([[maybe_unused]] Inst i);

        [[nodiscard]]
        bool emit_jump_if(Inst i);

        [[nodiscard]]
        bool emit_jump([[maybe_unused]] Inst i);

        [[nodiscard]]
        bool emit_call(Inst i);

        [[nodiscard]]
        bool emit_native_call(Inst i);

        [[nodiscard]]
        bool emit_ret(Inst i);

        [[nodiscard]]
        bool emit_instructions(const Inst* bc, std::size_t n);

        [[nodiscard]]
        bool emit_general(const compiler::BB* block);

        [[nodiscard]]
        bool emit_if_starter(const compiler::BB* block);

        [[nodiscard]]
        bool emit_tbody_ifs(const compiler::BB* block);

        [[nodiscard]]
        bool emit_fbody_ifs(const compiler::BB* block);

        [[nodiscard]]
        bool emit_if_ender(const compiler::BB* block);

        [[nodiscard]]
        bool emit_loop_starter(const compiler::BB* block);

        [[nodiscard]]
        bool emit_loop_ender(const compiler::BB* block);

    public:
        constexpr JIT() {
            m_sim_stack.reserve(sim_stack_size);
            m_sim_stack.resize(sim_stack_size);
            m_sim_bases.push_back(0);
        }

        [[nodiscard]]
        StubResult generate(const compiler::CFG* cfg, std::int32_t old_chunk_id, const Value* argv, std::uint16_t argc);
    };
}