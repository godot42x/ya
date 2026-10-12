#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace ya
{

// ────────────────────────────────────────────────────────────────────────────
// Result<T, E> - Rust-style error handling for C++20
// ────────────────────────────────────────────────────────────────────────────

template <typename T, typename E>
class Result
{
public:
    // Constructors
    Result(const T& value) : _data(value) {}
    Result(T&& value) : _data(std::move(value)) {}
    Result(const E& error) : _data(error) {}
    Result(E&& error) : _data(std::move(error)) {}

    // Query
    [[nodiscard]] bool isOk() const { return std::holds_alternative<T>(_data); }
    [[nodiscard]] bool isErr() const { return std::holds_alternative<E>(_data); }
    [[nodiscard]] explicit operator bool() const { return isOk(); }

    // Access (checked)
    [[nodiscard]] const T& value() const& { return std::get<T>(_data); }
    [[nodiscard]] T& value() & { return std::get<T>(_data); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(_data)); }
    
    [[nodiscard]] const E& error() const& { return std::get<E>(_data); }
    [[nodiscard]] E& error() & { return std::get<E>(_data); }
    [[nodiscard]] E&& error() && { return std::get<E>(std::move(_data)); }

    // Access (unchecked, for when you've already checked)
    [[nodiscard]] const T& operator*() const& { return std::get<T>(_data); }
    [[nodiscard]] T& operator*() & { return std::get<T>(_data); }
    [[nodiscard]] T&& operator*() && { return std::get<T>(std::move(_data)); }
    
    [[nodiscard]] const T* operator->() const { return &std::get<T>(_data); }
    [[nodiscard]] T* operator->() { return &std::get<T>(_data); }

    // Monadic operations (Rust-style)
    template <typename F>
    auto map(F&& f) const& -> Result<std::invoke_result_t<F, const T&>, E>
    {
        if (isOk()) {
            return f(value());
        }
        return error();
    }

    template <typename F>
    auto map(F&& f) && -> Result<std::invoke_result_t<F, T&&>, E>
    {
        if (isOk()) {
            return f(std::move(*this).value());
        }
        return std::move(*this).error();
    }

    template <typename F>
    auto andThen(F&& f) const& -> std::invoke_result_t<F, const T&>
    {
        if (isOk()) {
            return f(value());
        }
        return error();
    }

    template <typename F>
    auto andThen(F&& f) && -> std::invoke_result_t<F, T&&>
    {
        if (isOk()) {
            return f(std::move(*this).value());
        }
        return std::move(*this).error();
    }

    // Get value or default
    template <typename U>
    [[nodiscard]] T valueOr(U&& defaultValue) const&
    {
        return isOk() ? value() : static_cast<T>(std::forward<U>(defaultValue));
    }

    template <typename U>
    [[nodiscard]] T valueOr(U&& defaultValue) &&
    {
        return isOk() ? std::move(*this).value() : static_cast<T>(std::forward<U>(defaultValue));
    }

private:
    std::variant<T, E> _data;
};

// Helper for constructing Ok/Err explicitly
template <typename T>
struct Ok
{
    T value;
    explicit Ok(T v) : value(std::move(v)) {}
    explicit Ok(const T& v) : value(v) {}
};

template <typename E>
struct Err
{
    E error;
    explicit Err(E e) : error(std::move(e)) {}
    explicit Err(const E& e) : error(e) {}
};

// Deduction guides
template <typename T>
Ok(T) -> Ok<T>;

template <typename E>
Err(E) -> Err<E>;

} // namespace ya
