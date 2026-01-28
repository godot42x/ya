#pragma once

#include "Core/Reflection/Reflection.h"
#include <cstdint>
#include <string>
#include <unordered_map>


struct Class;
struct Enum;

namespace ya
{

struct ReflectionCache;

// ============================================================================
// MARK: Property Render Context
// ============================================================================

/**
 * @brief 属性渲染上下文 - 缓存属性的渲染相关信息
 *
 * 职责：
 * - 缓存属性的元数据（manipulateSpec）
 * - 缓存容器访问器
 * - 提供格式化后的显示名称
 */
struct PropertyRenderContext
{
    ReflectionCache *owner = nullptr;

    bool                            isContainer       = false;
    bool                            bPointer          = false; ///< True if property is a pointer type
    uint32_t                        pointeeTypeIndex  = 0;     ///< Type index of pointee (if bPointer)
    reflection::IContainerProperty *containerAccessor = nullptr;
    std::string                     prettyName;

    // 元数据（仅对基础类型有效）
    reflection::Meta::ManipulateSpec manipulateSpec;
    bool                             bColor = false;

    // 辅助方法：从 Property 提取元数据
    static PropertyRenderContext createFrom(ReflectionCache *owner, const Property &prop, const std::string &propName);
};



// ============================================================================
// MARK: Reflection Cache
// ============================================================================

/**
 * @brief 反射缓存 - 按类型索引缓存反射信息
 *
 * 职责：
 * - 缓存 Class 指针
 * - 缓存所有属性的渲染上下文
 */
struct ReflectionCache
{
    Class   *classPtr      = nullptr;
    size_t   propertyCount = 0;
    uint32_t typeIndex     = 0;

    bool bEnum = false;
    struct EnumMisc
    {
        Enum                            *enumPtr = nullptr;
        std::unordered_map<int64_t, int> valueToPosition;
        std::unordered_map<int, int64_t> positionToValue;
        std::vector<std::string>         names;
        std::string                      imguiComboString;
    } enumMisc;


    // 属性渲染上下文缓存（按属性名索引）
    std::unordered_map<std::string, PropertyRenderContext> propertyContexts;

    bool isValid(uint32_t ti) const { return ti == typeIndex && (classPtr != nullptr || enumMisc.enumPtr != nullptr); }
};

/**
 * @brief 获取或创建反射缓存
 * @param typeIndex 类型索引
 * @return 反射缓存指针
 */
ReflectionCache *getOrCreateReflectionCache(uint32_t typeIndex);



} // namespace ya
