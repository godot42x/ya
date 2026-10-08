#pragma once

#include "Core/Api.h"
#include "Core/TypeIndex.h"

#include <string>

struct Class;
struct Property;

namespace ya
{

struct YA_CORE_API ReflectionCopier
{
    static bool copyByRuntimeReflection(void* dstObj, const void* srcObj, type_index_t typeIndex, const std::string& className = "");

  private:
    static bool copyClassProperties(const Class* classPtr, void* dstObj, const void* srcObj);
    static bool copyPropertyValue(const Property& prop, void* dstObj, const void* srcObj);
    static bool copyAnyValue(void* dstValuePtr, const void* srcValuePtr, type_index_t typeIndex);
    static bool copyContainerValue(const Property& prop, void* dstContainerPtr, const void* srcContainerPtr);
    static bool copyScalarValue(void* dstValuePtr, const void* srcValuePtr, type_index_t typeIndex);

    static bool isBaseType(type_index_t typeIdx);

    static bool isEnumType(type_index_t typeIndex);
};

} // namespace ya
