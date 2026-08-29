#pragma once

#include <type_traits>

namespace toyjit::runtime {
    struct ValStrOpt {};
    struct ValObjOpt {};

    enum class VTag : std::uint8_t {
        v_nil,
        v_boolean,
        v_i32,
        v_str,
        v_obj,
    };

    struct Value {
        union {
            std::int32_t n;
            std::uint8_t byte;
        } data;
        VTag tag;

        [[nodiscard]]
        static constexpr Value make_nil() noexcept {
            return {
                .data = {.byte = {}},
                .tag = VTag::v_nil
            };
        }

        [[nodiscard]]
        static constexpr Value make_bool(bool b) noexcept {
            return {
                .data = {.byte = static_cast<std::uint8_t>(b)},
                .tag = VTag::v_boolean
            };
        }

        [[nodiscard]]
        static constexpr Value make_i32(int i) noexcept {
            return {
                .data = {.n = i},
                .tag = VTag::v_i32
            };
        }

        template <typename Opt>
        [[nodiscard]]
        static constexpr Value make_eid(std::int32_t id, [[maybe_unused]] Opt opt) noexcept {
            VTag temp_tag = VTag::v_nil;

            if constexpr (std::is_same_v<Opt, ValStrOpt>) {
                temp_tag = VTag::v_str;
            } else if constexpr (std::is_same_v<Opt, ValObjOpt>) {
                temp_tag = VTag::v_obj;
            }

            return {
                .data = {.n = id},
                .tag = temp_tag
            };
        }
    };
}
