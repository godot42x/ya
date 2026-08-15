#pragma once

#include <reflects-core/lib.h>
#include <string>
#include <string_view>

namespace ya
{

using type_index_t = refl::type_index_t;

inline std::string canonical_type_name(std::string_view rawName)
{
    std::string result;
    result.reserve(rawName.size());

    for (size_t i = 0; i < rawName.size();) {
        if (rawName.compare(i, 7, "struct ") == 0) {
            i += 7;
            continue;
        }
        if (rawName.compare(i, 6, "class ") == 0) {
            i += 6;
            continue;
        }
        if (rawName.compare(i, 6, "enum ") == 0) {
            i += 6;
            continue;
        }
        result.push_back(rawName[i++]);
    }

    return result;
}

template <typename T>
inline std::string canonical_type_name()
{
    return canonical_type_name(refl::detail::compilerTypeName<T>());
}


#if NOT_USE_REFLECTS

extern uint32_t _index_counter;

template <typename T>
struct TypeIndex
{
    static type_index_t value()
    {
        static uint32_t index = _index_counter++;
        return index;
    }
};

// Keep the original approach but let TypeIndex<T>::value() handle uniqueness
template <typename T>
inline const type_index_t type_index_v = TypeIndex<T>::value();

#else

template <typename T>
struct TypeIndex
{
    static constexpr type_index_t value()
    {
        return refl::TypeIndex<T>::value();
    }
};

template <typename T>
inline constexpr type_index_t type_index_v = TypeIndex<T>::value();

#endif

} // namespace ya
