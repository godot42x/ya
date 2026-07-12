#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace reflects::detail
{

template <auto Value>
constexpr auto enumName()
{
#if __GNUC__ || __clang__
    constexpr std::string_view name = __PRETTY_FUNCTION__;
    constexpr int              start1 = name.find('=') + 2;
    constexpr std::size_t      start2 = name.rfind("::");
    constexpr int              end = name.size() - 1;

    if constexpr (start2 != std::string_view::npos && start2 > start1) {
        return name.substr(start2 + 2, end - start2 - 2);
    } else {
        return name.substr(start1, end - start1);
    }
#elif _MSC_VER
    constexpr std::string_view name = __FUNCSIG__;
    constexpr std::size_t      start1 = name.find('<') + 1;
    constexpr int              start2 = name.rfind("::");
    constexpr std::size_t      end = name.rfind(">(");

    if constexpr (start2 != std::string_view::npos && start2 > start1) {
        return name.substr(start2 + 2, end - start2 - 2);
    } else {
        return name.substr(start1, end - start1);
    }
#endif
}

template <typename T, auto Num, std::size_t... Indices>
constexpr auto generateNamesArray(std::index_sequence<Indices...>)
{
    return std::array<std::string_view, Num>{enumName<static_cast<T>(Indices)>()...};
}

template <int UpperBound, typename T>
    requires std::is_enum_v<T>
constexpr auto enumName(T value)
{
    constexpr auto names = generateNamesArray<T, UpperBound + 1>(std::make_index_sequence<UpperBound + 1>{});
    return names.at(static_cast<std::size_t>(value));
}

} // namespace reflects::detail

template <auto Value>
std::string enum_name()
{
    return std::string(reflects::detail::enumName<Value>());
}

template <int UpperBound, typename T>
std::string enum_name(T value)
{
    return std::string(reflects::detail::enumName<UpperBound, T>(value));
}

#define GENERATED_ENUM_MISC_WITH_RANGE(ENUM_TYPE_NAME, UPPER_BOUND)                                                                                     \
    inline std::unordered_map<ENUM_TYPE_NAME, std::string> ENUM_TYPE_NAME##2Strings;                                                                    \
    namespace __detail__##ENUM_TYPE_NAME                                                                                                                \
    {                                                                                                                                                   \
        struct Generator                                                                                                                                \
        {                                                                                                                                               \
            Generator()                                                                                                                                 \
            {                                                                                                                                           \
                constexpr int upperBound = static_cast<int>(ENUM_TYPE_NAME::UPPER_BOUND);                                                              \
                for (int i = 0; i <= upperBound; i++) {                                                                                                 \
                    ENUM_TYPE_NAME##2Strings[static_cast<ENUM_TYPE_NAME>(i)] = enum_name<upperBound, ENUM_TYPE_NAME>(static_cast<ENUM_TYPE_NAME>(i));  \
                }                                                                                                                                       \
            }                                                                                                                                           \
        };                                                                                                                                              \
        inline Generator generator;                                                                                                                     \
    }                                                                                                                                                   \
    inline const std::string &to_string(ENUM_TYPE_NAME v) { return ENUM_TYPE_NAME##2Strings.find(v)->second; }                                         \
    inline const std::string &toString(ENUM_TYPE_NAME v) { return ENUM_TYPE_NAME##2Strings.find(v)->second; }

#define GENERATED_ENUM_MISC(ENUM_TYPE_NAME) \
    GENERATED_ENUM_MISC_WITH_RANGE(ENUM_TYPE_NAME, ENUM_MAX)
