#pragma once

#include <cstdint>
#include <type_traits>

namespace ya
{

// ────────────────────────────────────────────────────────────────────────────
// Optional<T> - Rust-style optional with explicit validity tracking
// ────────────────────────────────────────────────────────────────────────────

/// Lightweight optional for value types (POD-like), where invalid state is ~0u.
/// Use std::optional<T> for complex types with destructors.
template <typename T>
struct Optional
{
    static_assert(std::is_trivially_copyable_v<T>, "Optional<T> requires trivially copyable T");
    static_assert(sizeof(T) <= 16, "Optional<T> should be small; use std::optional for large types");

    T _value = {};
    bool _valid = false;

    constexpr Optional() = default;
    constexpr explicit Optional(const T& value) : _value(value), _valid(true) {}
    constexpr explicit Optional(T&& value) : _value(std::move(value)), _valid(true) {}

    [[nodiscard]] constexpr bool isValid() const { return _valid; }
    [[nodiscard]] constexpr bool isSome() const { return _valid; }
    [[nodiscard]] constexpr bool isNone() const { return !_valid; }
    [[nodiscard]] constexpr explicit operator bool() const { return _valid; }

    [[nodiscard]] constexpr const T& value() const { return _value; }
    [[nodiscard]] constexpr T& value() { return _value; }
    
    [[nodiscard]] constexpr const T& operator*() const { return _value; }
    [[nodiscard]] constexpr T& operator*() { return _value; }
    
    [[nodiscard]] constexpr const T* operator->() const { return &_value; }
    [[nodiscard]] constexpr T* operator->() { return &_value; }

    template <typename U>
    [[nodiscard]] constexpr T valueOr(U&& defaultValue) const
    {
        return _valid ? _value : static_cast<T>(std::forward<U>(defaultValue));
    }

    constexpr void reset() { _valid = false; }

    constexpr bool operator==(const Optional& other) const
    {
        if (_valid != other._valid) return false;
        if (!_valid) return true; // both invalid
        return _value == other._value;
    }

    constexpr bool operator!=(const Optional& other) const
    {
        return !(*this == other);
    }
};

// ────────────────────────────────────────────────────────────────────────────
// OptionalHandle<T> - Optional for handle-like types (uint32_t index with ~0u invalid)
// ────────────────────────────────────────────────────────────────────────────

/// Specialized optional for handle types where ~0u represents "invalid".
/// This matches RenderGraph handle semantics (invalid = ~0u).
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

// ────────────────────────────────────────────────────────────────────────────
// Helper to construct Optional from value
// ────────────────────────────────────────────────────────────────────────────

template <typename T>
constexpr Optional<T> Some(T value)
{
    return Optional<T>{std::move(value)};
}

template <typename T>
constexpr Optional<T> None()
{
    return Optional<T>{};
}

} // namespace ya
