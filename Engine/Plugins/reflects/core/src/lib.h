
#pragma once



#include <cstdint>
namespace refl
{

extern uint32_t _nextTypeId;

template <typename T>
struct TypeIndex
{

    static uint32_t value()
    {
        static uint32_t typeId = _nextTypeId++;
        return typeId;
    }
};

template <typename T>
static inline auto const type_index_v = refl::TypeIndex<T>::value();



} // namespace refl