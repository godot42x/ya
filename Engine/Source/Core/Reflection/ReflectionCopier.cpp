#include "ReflectionCopier.h"

#include "PropertyExtensions.h"
#include "ReflectionSerializer.h"

namespace ya
{

bool ReflectionCopier::copyByRuntimeReflection(void* dstObj, const void* srcObj, uint32_t typeIndex, const std::string& className)
{
    if (!dstObj || !srcObj) {
        YA_CORE_WARN("ReflectionCopier: copyByRuntimeReflection got null object pointer");
        return false;
    }

    auto& registry = ClassRegistry::instance();
    auto* classPtr = registry.getClass(typeIndex);
    if (!classPtr && !className.empty()) {
        classPtr = registry.getClass(className);
    }

    if (!classPtr) {
        YA_CORE_WARN("ReflectionCopier: Class '{}:{}' not found in registry for copy", typeIndex, className);
        return false;
    }

    return copyClassProperties(classPtr, dstObj, srcObj);
}

bool ReflectionCopier::copyClassProperties(const Class* classPtr, void* dstObj, const void* srcObj)
{
    bool success = true;

    for (const auto parentTypeId : classPtr->parents) {
        auto* parentClass = classPtr->getClassByTypeId(parentTypeId);
        if (!parentClass) {
            continue;
        }

        void*       dstParentObj = classPtr->getParentPointer(dstObj, parentTypeId);
        const void* srcParentObj = classPtr->getParentPointer(const_cast<void*>(srcObj), parentTypeId);
        if (!dstParentObj || !srcParentObj) {
            YA_CORE_WARN("ReflectionCopier: Failed to get parent pointer while copying '{} -> {}'", classPtr->name, parentClass->name);
            success = false;
            continue;
        }

        success = copyClassProperties(parentClass, dstParentObj, srcParentObj) && success;
    }

    classPtr->visitOwnProperties([&](const std::string& propName, const Property& prop) {
        if (prop.metadata.hasFlag(FieldFlags::NotSerialized)) {
            return;
        }

        try {
            success = copyPropertyValue(prop, dstObj, srcObj) && success;
        }
        catch (const std::exception& e) {
            YA_CORE_WARN("ReflectionCopier: Failed to copy property '{}.{}': {}", classPtr->name, propName, e.what());
            success = false;
        }
    });

    return success;
}

bool ReflectionCopier::copyPropertyValue(const Property& prop, void* dstObj, const void* srcObj)
{
    if (!prop.addressGetterMutable || prop.bConst) {
        return true;
    }

    const void* srcValuePtr = prop.getAddress(srcObj);
    void*       dstValuePtr = prop.getMutableAddress(dstObj);
    if (!srcValuePtr || !dstValuePtr) {
        YA_CORE_WARN("ReflectionCopier: Cannot access property '{}' while copying", prop.name);
        return false;
    }

    if (prop.bPointer) {
        const auto jsonValue = ReflectionSerializer::serializeProperty(srcObj, prop);
        ReflectionSerializer::deserializeProperty(prop, dstObj, jsonValue);
        return true;
    }

    if (::ya::reflection::PropertyContainerHelper::isContainer(prop)) {
        return copyContainerValue(prop, dstValuePtr, srcValuePtr);
    }

    if (copyAnyValue(dstValuePtr, srcValuePtr, prop.typeIndex)) {
        return true;
    }

    const auto jsonValue = ReflectionSerializer::serializeProperty(srcObj, prop);
    ReflectionSerializer::deserializeProperty(prop, dstObj, jsonValue);
    return true;
}

bool ReflectionCopier::copyAnyValue(void* dstValuePtr, const void* srcValuePtr, uint32_t typeIndex)
{
    if (!dstValuePtr || !srcValuePtr) {
        return false;
    }

    if (copyScalarValue(dstValuePtr, srcValuePtr, typeIndex)) {
        return true;
    }

    auto& registry = ClassRegistry::instance();
    if (auto* classPtr = registry.getClass(typeIndex)) {
        return copyClassProperties(classPtr, dstValuePtr, srcValuePtr);
    }

    return false;
}

bool ReflectionCopier::copyContainerValue(const Property& prop, void* dstContainerPtr, const void* srcContainerPtr)
{
    auto* accessor = ::ya::reflection::PropertyContainerHelper::getContainerAccessor(prop);
    if (!accessor) {
        return false;
    }

    bool success = true;

    if (accessor->isMapLike()) {
        accessor->clear(dstContainerPtr);

        ::ya::reflection::PropertyContainerHelper::iterateMapContainer(
            prop,
            const_cast<void*>(srcContainerPtr),
            [&](void* keyPtr, uint32_t /*keyTypeIndex*/, void* valuePtr, uint32_t valueTypeIndex) {
                if (!keyPtr || !valuePtr) {
                    success = false;
                    return;
                }

                auto& registry   = ClassRegistry::instance();
                auto* valueClass = registry.getClass(valueTypeIndex);
                const bool useTmp = valueClass && valueClass->canCreateInstance() &&
                                     !(isBaseType(valueTypeIndex) || isEnumType(valueTypeIndex));

                if (!useTmp) {
                    accessor->insertElement(dstContainerPtr, keyPtr, valuePtr);
                    return;
                }

                void* tmpValue = valueClass->createInstance();
                if (!copyAnyValue(tmpValue, valuePtr, valueTypeIndex)) {
                    valueClass->destroyInstance(tmpValue);
                    success = false;
                    return;
                }

                accessor->insertElement(dstContainerPtr, keyPtr, tmpValue);
                valueClass->destroyInstance(tmpValue);
            });

        return success;
    }

    if (accessor->isFixedSize()) {
        const auto elementCount = accessor->getSize(const_cast<void*>(srcContainerPtr));
        for (size_t index = 0; index < elementCount; ++index) {
            void* dstElementPtr = accessor->getElementPtr(dstContainerPtr, index);
            void* srcElementPtr = accessor->getElementPtr(const_cast<void*>(srcContainerPtr), index);
            if (!dstElementPtr || !srcElementPtr) {
                success = false;
                continue;
            }

            success = copyAnyValue(dstElementPtr, srcElementPtr, accessor->getElementTypeIndex()) && success;
        }
        return success;
    }

    accessor->clear(dstContainerPtr);
    ::ya::reflection::PropertyContainerHelper::iterateContainer(
        prop,
        const_cast<void*>(srcContainerPtr),
        [&](size_t /*index*/, void* elementPtr, uint32_t elementTypeIndex) {
            if (!elementPtr) {
                success = false;
                return;
            }

            if (isBaseType(elementTypeIndex) || isEnumType(elementTypeIndex)) {
                accessor->addElement(dstContainerPtr, elementPtr);
                return;
            }

            auto& registry    = ClassRegistry::instance();
            auto* elementCls  = registry.getClass(elementTypeIndex);
            if (!elementCls || !elementCls->canCreateInstance()) {
                accessor->addElement(dstContainerPtr, elementPtr);
                return;
            }

            void* tmpElement = elementCls->createInstance();
            if (!copyAnyValue(tmpElement, elementPtr, elementTypeIndex)) {
                elementCls->destroyInstance(tmpElement);
                success = false;
                return;
            }

            accessor->addElement(dstContainerPtr, tmpElement);
            elementCls->destroyInstance(tmpElement);
        });

    return success;
}

bool ReflectionCopier::copyScalarValue(void* dstValuePtr, const void* srcValuePtr, uint32_t typeIndex)
{
    if (typeIndex == ya::type_index_v<int>) {
        *static_cast<int*>(dstValuePtr) = *static_cast<const int*>(srcValuePtr);
        return true;
    }
    if (typeIndex == ya::type_index_v<float>) {
        *static_cast<float*>(dstValuePtr) = *static_cast<const float*>(srcValuePtr);
        return true;
    }
    if (typeIndex == ya::type_index_v<double>) {
        *static_cast<double*>(dstValuePtr) = *static_cast<const double*>(srcValuePtr);
        return true;
    }
    if (typeIndex == ya::type_index_v<bool>) {
        *static_cast<bool*>(dstValuePtr) = *static_cast<const bool*>(srcValuePtr);
        return true;
    }
    if (typeIndex == ya::type_index_v<std::string>) {
        *static_cast<std::string*>(dstValuePtr) = *static_cast<const std::string*>(srcValuePtr);
        return true;
    }
    if (typeIndex == ya::type_index_v<unsigned int>) {
        *static_cast<unsigned int*>(dstValuePtr) = *static_cast<const unsigned int*>(srcValuePtr);
        return true;
    }

    if (isEnumType(typeIndex)) {
        if (auto* enumInfo = EnumRegistry::instance().getEnum(typeIndex)) {
            switch (enumInfo->underlyingSize) {
            case 1:
                *static_cast<uint8_t*>(dstValuePtr) = *static_cast<const uint8_t*>(srcValuePtr);
                return true;
            case 2:
                *static_cast<uint16_t*>(dstValuePtr) = *static_cast<const uint16_t*>(srcValuePtr);
                return true;
            case 4:
                *static_cast<int32_t*>(dstValuePtr) = *static_cast<const int32_t*>(srcValuePtr);
                return true;
            case 8:
                *static_cast<int64_t*>(dstValuePtr) = *static_cast<const int64_t*>(srcValuePtr);
                return true;
            default:
                *static_cast<int*>(dstValuePtr) = *static_cast<const int*>(srcValuePtr);
                return true;
            }
        }
    }

    return false;
}

} // namespace ya
