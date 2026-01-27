#pragma once

#include "Core/Base.h"


namespace ya
{

struct ReflectionHelper
{

    static bool isScalarType(type_index_t typeIdx)
    {

        static std::unordered_set<uint32_t> baseTypes = {
            ya::type_index_v<int>,
            ya::type_index_v<float>,
            ya::type_index_v<double>,
            ya::type_index_v<bool>,
            ya::type_index_v<unsigned int>,
            ya::type_index_v<std::string>,
        };


        return baseTypes.contains(typeIdx);
    }

    static bool isBaseType(uint32_t typeIdx)
    {
        return isScalarType(typeIdx) || isEnumType(typeIdx);
    }

    /**
     * Check if a type is an enum by typeIndex
     */
    static bool isEnumType(uint32_t typeIndex)
    {
        return EnumRegistry::instance().hasEnum(typeIndex);
    }
};

} // namespace ya