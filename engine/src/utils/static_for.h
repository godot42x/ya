

#pragma once

#include <type_traits>
#include <utility>

template <int StartIndex, int UpperBound, class Fn, class... Arg>
void static_for(Fn func, Arg... args)
{
    if constexpr (StartIndex < UpperBound) {
        func(std::integral_constant<int, StartIndex>{}, std::forward<Arg>(args)...);
        static_for<StartIndex + 1, UpperBound>(func, std::forward<Arg>(args)...);
    }
}
