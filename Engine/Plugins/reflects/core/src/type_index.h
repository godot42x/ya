
#pragma once

#include <cstdint>
#include <string_view>

namespace refl
{

using type_index_t = uint64_t;

namespace detail
{

constexpr type_index_t FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr type_index_t FNV_PRIME        = 1099511628211ull;

consteval type_index_t hashTypeName(std::string_view name)
{
    type_index_t hash = FNV_OFFSET_BASIS;
    for (const char ch : name) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= FNV_PRIME;
    }
    return hash == 0 ? 1 : hash;
}

template <typename T>
consteval std::string_view compilerTypeName()
{
#if defined(_MSC_VER)
    constexpr std::string_view signature = __FUNCSIG__;
    constexpr std::string_view prefix    = "compilerTypeName<";
    const size_t               begin     = signature.find(prefix) + prefix.size();
    const size_t               end       = signature.find(">(void)", begin);
#else
    constexpr std::string_view signature = __PRETTY_FUNCTION__;
    constexpr std::string_view prefix    = "T = ";
    const size_t               begin     = signature.find(prefix) + prefix.size();
    const size_t               end       = signature.find_first_of("];", begin);
#endif
    return signature.substr(begin, end - begin);
}

} // namespace detail

template <typename T>
struct TypeIndex
{
    static consteval type_index_t value()
    {
        return detail::hashTypeName(detail::compilerTypeName<T>());
    }
};

template <typename T>
inline constexpr type_index_t type_index_v = TypeIndex<T>::value();

} // namespace refl
