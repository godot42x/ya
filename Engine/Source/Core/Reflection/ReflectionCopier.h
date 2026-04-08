#pragma once

#include "Core/TypeIndex.h"

#include <string>
#include <unordered_set>

struct Class;
struct Property;

namespace ya
{

struct ReflectionCopier
{
    static bool copyByRuntimeReflection(void* dstObj, const void* srcObj, uint32_t typeIndex, const std::string& className = "");

  private:
    static bool copyClassProperties(const Class* classPtr, void* dstObj, const void* srcObj);
    static bool copyPropertyValue(const Property& prop, void* dstObj, const void* srcObj);
    static bool copyAnyValue(void* dstValuePtr, const void* srcValuePtr, uint32_t typeIndex);
    static bool copyContainerValue(const Property& prop, void* dstContainerPtr, const void* srcContainerPtr);
    static bool copyScalarValue(void* dstValuePtr, const void* srcValuePtr, uint32_t typeIndex);

    static bool isBaseType(uint32_t typeIdx)
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

    static bool isEnumType(uint32_t typeIndex);
};

} // namespace ya