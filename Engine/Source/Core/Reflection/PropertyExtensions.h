/**
 * @file PropertyExtensions.h
 * @brief Property 扩展 - 添加容器支持
 *
 * 为现有的 Property 系统添加容器元数据和访问器
 */

#pragma once

#include "ContainerProperty.h"
#include "ContainerTraits.h"
#include <reflects-core/lib.h>

namespace ya::reflection
{

/**
 * @brief Property 容器扩展数据
 * 附加到 Property::metadata 中
 */
struct ContainerPropertyExtension
{
    bool                                isContainer = false;
    EContainer                          containerType;
    std::unique_ptr<IContainerProperty> containerAccessor; // 类型擦除的容器访问器

    // 便捷方法
    bool hasContainer() const { return isContainer && containerAccessor != nullptr; }
};

/**
 * @brief 为 Property 添加容器支持
 */
class PropertyContainerHelper
{
  public:
    /**
     * @brief 检测并注册容器元数据
     */
    template <typename FieldType>
    static void tryRegisterContainer(Property &prop)
    {
        if constexpr (is_container_v<FieldType>) {
            auto extension               = std::make_shared<ContainerPropertyExtension>();
            extension->isContainer       = true;
            extension->containerType     = ContainerTraits<FieldType>::Type;
            extension->containerAccessor = createContainerProperty<FieldType>();

            // 存储到 Property::metadata
            prop.metadata.set("__container_extension", extension);
        }
    }

    // /**
    //  * @brief 获取容器扩展数据
    //  */
    // static ContainerPropertyExtension *getContainerExtension(const Property &prop)
    // {
    //     if (prop.metadata.hasMeta("__container_extension")) {
    //         auto ptr = prop.metadata.get<std::shared_ptr<ContainerPropertyExtension>>(
    //             "__container_extension");
    //         return ptr.get();
    //     }
    //     return nullptr;
    // }

    static const ContainerPropertyExtension *getContainerExtension(const Property &prop)
    {
        // cache to avoid search string  each time
        static std::unordered_map<uint32_t, ContainerPropertyExtension *> hasContainerExtension;
        if (hasContainerExtension.find(prop.typeIndex) != hasContainerExtension.end()) {
            return hasContainerExtension[prop.typeIndex];
        }

        if (prop.metadata.hasMeta("__container_extension")) {
            auto ptr = prop.metadata.get<std::shared_ptr<ContainerPropertyExtension>>(
                "__container_extension");
            hasContainerExtension[prop.typeIndex] = ptr.get();
            return ptr.get();
        }
        return nullptr;
    }

    /**
     * @brief 检查 Property 是否为容器
     */
    static bool isContainer(const Property &prop)
    {
        auto *ext = getContainerExtension(prop);
        return ext && ext->hasContainer();
    }

    /**
     * @brief 获取容器访问器
     */
    static IContainerProperty *getContainerAccessor(const Property &prop)
    {
        auto *ext = getContainerExtension(prop);
        // return ext ? ext->containerAccessor.get() : nullptr;
        return ext->containerAccessor.get();
    }

    /**
     * @brief 遍历容器元素 - Vector/Set 版本
     *
     * @param prop 容器属性
     * @param containerPtr 容器实例指针
     * @param visitor 访问器函数 (size_t index, void* elementPtr, uint32_t elementTypeIndex)
     */
    template <typename Visitor>
    static void iterateContainer(Property &prop, void *containerPtr, Visitor &&visitor)
    {
        auto *accessor = getContainerAccessor(prop);
        if (!accessor) {
            return;
        }

        auto   iterator = accessor->createIterator(containerPtr);
        size_t index    = 0;

        while (iterator->hasNext()) {
            void    *elementPtr       = iterator->getElementPtr();
            uint32_t elementTypeIndex = iterator->getElementTypeIndex();

            std::forward<Visitor>(visitor)(index, elementPtr, elementTypeIndex);

            iterator->next();
            ++index;
        }
    }

    /**
     * @brief 遍历 Map 容器元素
     *
     * @param prop 容器属性
     * @param containerPtr 容器实例指针
     * @param visitor 访问器函数 (void* keyPtr, uint32_t keyTypeIndex, void* valuePtr, uint32_t valueTypeIndex)
     */
    template <typename Visitor>
    static void iterateMapContainer(Property &prop, void *containerPtr, Visitor &&visitor)
    {
        auto *accessor = getContainerAccessor(prop);
        if (!accessor || !accessor->isMapLike()) {
            return;
        }

        auto iterator = accessor->createIterator(containerPtr);

        while (iterator->hasNext()) {
            void    *keyPtr         = iterator->getKeyPtr();
            uint32_t keyTypeIndex   = iterator->getKeyTypeIndex();
            void    *valuePtr       = iterator->getElementPtr();
            uint32_t valueTypeIndex = iterator->getElementTypeIndex();

            std::forward<Visitor>(visitor)(keyPtr, keyTypeIndex, valuePtr, valueTypeIndex);

            iterator->next();
        }
    }
};

} // namespace ya::reflection
