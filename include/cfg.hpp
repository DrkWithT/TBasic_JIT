#pragma once

#include <vector>
#include "bytecode.hpp"


namespace toyjit::compiler {
    enum class BBTag : std::uint8_t {
        general,       // ? non-control-flow related BB
        start_ifs,     // ? if/else starting BB before T/F children
        tbody_ifs,
        fbody_ifs,
        end_ifs,       // ? if/else ending & falsy BB
        start_loop,    // ? while loop starting BB before it's body
        end_loop       // ? while loop ending BB after it's body
    };

    /**
     * @brief Models a CFG over emitted bytecode, following these ideas:
     * 1. A BB terminates a RPO traversal path if no children exist.
     * 2. A BB's truthy child is on the left.
     * 3. Ifs have a conditional block, T-block & F-block children, and a link from T-block to post-if BB's.
     * 4. Loops have a conditional block, general-T-block (body), and a link from body to post-loop BB's. No link back is needed for simplicity (keeping the traversal and jump patching simple).
     */
    struct BB {
        static constexpr int dud_id = -1;

        const runtime::Inst* data {};
        std::size_t n {};

        int left_child {-1};
        int right_child {-1};
        BBTag tag {BBTag::general};

        constexpr BB() noexcept = default;

        constexpr BB(const runtime::Inst* data_p, std::size_t n_v, int left, int right, BBTag tag_v) noexcept
        : data {data_p}, n {n_v}, left_child {left}, right_child {right}, tag {tag_v} {}

        [[nodiscard]]
        constexpr bool is_terminator() const noexcept {
            return left_child == -1 && right_child == -1;
        }
    };

    class CFG {
        std::vector<BB> m_blocks;
        const runtime::Value* m_konsts {};
        int m_entry_id {};

    public:
        constexpr CFG(const runtime::Value* konsts_ptr)
        : m_blocks {}, m_konsts {konsts_ptr}, m_entry_id {} {}

        constexpr int add_bb(const runtime::Inst* bc, std::size_t n, BBTag tag) {
            const auto next_bb_id = m_blocks.size();

            m_blocks.emplace_back(bc, n, -1, -1, tag);

            return next_bb_id;
        }

        constexpr const BB* get_bb(int id) const noexcept {
            return (id < 0 || id >= static_cast<int>(m_blocks.size())) ? nullptr : m_blocks.data() + id;
        }

        constexpr BB* get_bb(int id) noexcept {
            return (id < 0 || id >= static_cast<int>(m_blocks.size())) ? nullptr : m_blocks.data() + id;
        }

        template <char Dir>
        constexpr void link_bb(int target, int dest) {
            if (target < 0 || target >= static_cast<int>(m_blocks.size())) {
                return;
            }

            auto& bb = m_blocks[target];

            if constexpr (Dir == 'L') {
                bb.left_child = dest;
            } else if constexpr (Dir == 'R') {
                bb.right_child = dest;
            }
        }

        [[nodiscard]]
        constexpr const runtime::Value* peek_konst(std::int32_t id) const noexcept {
            return m_konsts + id;
        }
    };
}
