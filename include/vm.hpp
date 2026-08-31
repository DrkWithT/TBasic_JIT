#pragma once

#include <utility>
#include <array>
#include <vector>
#include <optional>
#include <unordered_map>
#include <future>

#include "bytecode.hpp"
#include "cfg.hpp"
#include "jit.hpp"



namespace toyjit::runtime {
    struct VMFrame {
        const runtime::Inst* old_rip {};
        const runtime::Value* old_cvp {};
        std::uint32_t old_bp {};
        std::uint32_t self_bp {};
        std::int32_t curr_cid {-1};
    };

    enum class VMFlags : std::uint16_t {
        vm_ok = 0x1,
        vm_running = 0x2,
        vm_initial = vm_running,
        vm_bad_op = 0x4,
        vm_bad_operand = 0x8,
        vm_bad_ref = 0x10,
    };

    struct VM;

    [[nodiscard]]
    bool run_vm(VM* vm);

    /* 2: baseline bytecode interpreter */
    struct VM {
        using StubStore = std::optional<std::future<StubResult>>;
        static constexpr std::uint32_t base_stack_max = 1280;
        static constexpr std::uint32_t base_depth_max = 64;

        std::unordered_map<int, int> stub_map;  // maps chunk IDs to trampoline IDs
        std::vector<VMFrame> frames;            // call frame
        std::vector<runtime::Value> stack;      // VM temporaries
        std::vector<StubResult> stubs;          // JIT stubs / user-natives
        std::vector<compiler::CFG> cfgs;        // CFG storage
        std::array<HelperFn, static_cast<std::size_t>(HelperID::last)> helpers; // JIT helper natives
        StubStore maybe_stub;       // none / reused stub result ID / prepared JIT stub
        JIT* jit;                   // JIT utility ptr
        runtime::Program* pg;       // program ptr
        const runtime::Inst* ip;    // instruction pointer
        const runtime::Value* cvp;  // constants pointer
        std::uint32_t bp;           // base pointer
        std::uint32_t sp;           // stack top
        std::int32_t cid;           // chunk ID
        std::uint16_t flags;
        std::uint16_t end_depth;

        constexpr VM(std::array<HelperFn, static_cast<std::size_t>(HelperID::last)> helpers_v, std::vector<compiler::CFG> cfg_list, JIT* jit_p, runtime::Program* program_p)
        : stub_map {}, frames {}, stack (base_stack_max), stubs {}, cfgs (std::move(cfg_list)), helpers (std::move(helpers_v)), maybe_stub {}, jit {jit_p}, pg {program_p}, ip {}, cvp {}, bp {}, sp {}, cid {-1}, flags {static_cast<std::uint16_t>(VMFlags::vm_running)}, end_depth {} {
            const auto program_main_chunk_id = pg->first_chunk_id;

            frames.emplace_back(nullptr, nullptr, 0, 0, -1);

            ip = pg->chunks[program_main_chunk_id].bc.data();
            cvp = pg->chunks[program_main_chunk_id].konsts.data();
            cid = program_main_chunk_id;
        }

        [[nodiscard]]
        constexpr bool running() const noexcept {
            return (flags & std::to_underlying(VMFlags::vm_running)) != 0;
        }

        // ! IMPORTANT: Only use this for trampolines which have special handling of frames, especially for JIT stubbed callers.
        [[nodiscard]]
        constexpr Value sub_call(std::int32_t chunk_id, std::uint16_t argc) {
            const auto& chunk = pg->chunks[chunk_id];
            const Value* caller_cvp = chunk.konsts.data();
            const std::int32_t callee_bp = sp - argc + 1;
            const std::int32_t caller_bp = callee_bp;
            const std::uint16_t old_end_depth = end_depth;
  
            frames.emplace_back(nullptr, caller_cvp, caller_bp, callee_bp, chunk_id);
            bp = callee_bp;
            ip = chunk.bc.data();
            cvp = caller_cvp;
            cid = chunk_id;
            end_depth = frames.size();

            // ? Note that any Op::ret will discard temporaries and the CallFrame for us, leaving the result in the trampoline's `CALLEE_BP`. Just resume the VM with the previous ending call depth and post-trampoline state.
            const auto sub_call_was_ok = run_vm(this);

            sp = callee_bp - 1; // ? Here, restore the SP before the parent stub call.
            end_depth = old_end_depth;

            Value result;

            if (sub_call_was_ok) {
                result = stack[sp + 1];
                flags |= std::to_underlying(VMFlags::vm_running);
            } else {
                result = Value::make_oops();
            }

            return result;
        }

        void jit_chunk(std::int32_t current_chunk_id, std::int32_t chunk_id, std::uint16_t argc, const Value* argv);
        void salvage_jit_trampoline(std::int32_t current_chunk_id, std::int32_t old_callee_chunk_id);
        void patch_chunk_calls(std::int32_t target_chunk_id, std::int32_t callee_chunk_id);
    };
}