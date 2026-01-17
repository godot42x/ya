#pragma once

#include <reflects-core/lib.h>

namespace ya
{

using type_index_t = uint32_t;


#if NOT_USE_REFLECTS

extern uint32_t _index_counter;

template <typename T>
struct TypeIndex
{
    static uint32_t value()
    {
        static uint32_t index = _index_counter++;
        return index;
    }
};

// Keep the original approach but let TypeIndex<T>::value() handle uniqueness
template <typename T>
inline const uint32_t type_index_v = TypeIndex<T>::value();

#else

template <typename T>
struct TypeIndex
{
    static uint32_t value()
    {
        return refl::TypeIndex<T>::value();
    }
};

template <typename T>
inline static auto type_index_v = TypeIndex<T>::value();

#endif

} // namespace ya
