#pragma once

#include "Core/Log.h"

#include <initializer_list>
#include <string_view>
#include <type_traits>

namespace ya
{

template <typename E>
struct StateTraits
{
    static constexpr bool hasFailedState    = false;
    static constexpr bool hasTerminalStates = false;
};

namespace detail_state_transition
{
template <typename E>
constexpr auto toUnderlying(E value) -> std::underlying_type_t<E>
{
    static_assert(std::is_enum_v<E>, "StateTransition requires an enum state type");
    return static_cast<std::underlying_type_t<E>>(value);
}
} // namespace detail_state_transition

template <typename E>
struct StateTransition
{
    static_assert(std::is_enum_v<E>, "StateTransition requires an enum state type");

    E&               state;
    std::string_view label;

    [[nodiscard]] bool is(E expected) const { return state == expected; }

    [[nodiscard]] bool isAny(std::initializer_list<E> states) const
    {
        for (auto candidate : states) {
            if (state == candidate) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool isTerminal() const
    {
        if constexpr (StateTraits<E>::hasTerminalStates) {
            return StateTraits<E>::isTerminal(state);
        }
        return false;
    }

    void to(E next, std::string_view reason = {})
    {
        if (state == next) {
            return;
        }

        auto previous = state;
        state         = next;
        trace(previous, next, reason);
    }

    [[nodiscard]] bool tryTransition(E from, E toState, std::string_view reason = {})
    {
        if (state != from) {
            return false;
        }
        to(toState, reason);
        return true;
    }

    void fail(std::string_view reason = {})
    {
        static_assert(StateTraits<E>::hasFailedState,
                      "StateTraits<E>::hasFailedState must be true to use fail()");
        to(StateTraits<E>::failedState(), reason);
    }

  private:
    void trace(E from, E toState, std::string_view reason) const
    {
        const auto fromValue = static_cast<long long>(detail_state_transition::toUnderlying(from));
        const auto toValue   = static_cast<long long>(detail_state_transition::toUnderlying(toState));
        if (label.empty()) {
            if (reason.empty()) {
                YA_CORE_TRACE("StateTransition {} -> {}", fromValue, toValue);
            }
            else {
                YA_CORE_TRACE("StateTransition {} -> {} ({})", fromValue, toValue, reason);
            }
            return;
        }

        if (reason.empty()) {
            YA_CORE_TRACE("StateTransition[{}] {} -> {}", label, fromValue, toValue);
        }
        else {
            YA_CORE_TRACE("StateTransition[{}] {} -> {} ({})", label, fromValue, toValue, reason);
        }
    }
};

template <typename E>
[[nodiscard]] inline StateTransition<E> makeTransition(E& state, std::string_view label = {})
{
    return StateTransition<E>{state, label};
}

} // namespace ya
