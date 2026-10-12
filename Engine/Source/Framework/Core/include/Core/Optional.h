#pragma once

#include <cstdint>
#include <type_traits>

namespace ya
{

// ────────────────────────────────────────────────────────────────────────────
// OptionalHandle<Tag> - Type-safe optional handle with zero overhead
// ────────────────────────────────────────────────────────────────────────────

/// Type-safe optional for handle-like types where ~0u represents "invalid".
/// This matches RenderGraph handle semantics (invalid = ~0u).
/// 
/// Use this instead of std::optional<uint32_t> for:
/// - Zero size overhead (sizeof == sizeof(uint32_t))
/// - Type safety (different tag types are incompatible)
/// - Clear semantics (invalid handle = ~0u, not engaged/disengaged)
///
/// Example:
///   struct TextureHandleTag {};
///   using OptionalTexture = OptionalHandle<TextureHandleTag>;
///   
///   OptionalTexture tex{5};  // valid handle
///   if (tex) { use(tex.index()); }
template <typename HandleTag>
struct OptionalHandle
{
    static_assert(std::is_empty_v<HandleTag>, "OptionalHandle tag must be empty type");

    uint32_t _index = ~0u;

    constexpr OptionalHandle() = default;
    constexpr explicit OptionalHandle(uint32_t idx) : _index(idx) {}

    [[nodiscard]] constexpr bool isValid() const { return _index != ~0u; }
    [[nodiscard]] constexpr bool isSome() const { return _index != ~0u; }
    [[nodiscard]] constexpr bool isNone() const { return _index == ~0u; }
    [[nodiscard]] constexpr explicit operator bool() const { return _index != ~0u; }

    [[nodiscard]] constexpr uint32_t index() const { return _index; }
    [[nodiscard]] constexpr uint32_t valueOr(uint32_t defaultValue) const
    {
        return _index != ~0u ? _index : defaultValue;
    }

    constexpr void reset() { _index = ~0u; }

    constexpr bool operator==(const OptionalHandle& other) const = default;
    constexpr bool operator!=(const OptionalHandle& other) const = default;
};

} // namespace ya
