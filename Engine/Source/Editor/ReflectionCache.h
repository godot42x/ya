#pragma once

#include "TypeRenderer.h"
#include <cstdint>
#include <string>
#include <unordered_map>

struct Class;

namespace ya
{

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
    Class   *componentClassPtr = nullptr;
    size_t   propertyCount     = 0;
    uint32_t typeIndex         = 0;

    // 属性渲染上下文缓存（按属性名索引）
    std::unordered_map<std::string, PropertyRenderContext> propertyContexts;

    bool isValid(uint32_t ti) const { return ti == typeIndex && componentClassPtr != nullptr; }
};

/**
 * @brief 获取或创建反射缓存
 * @param typeIndex 类型索引
 * @return 反射缓存指针
 */
ReflectionCache *getOrCreateReflectionCache(uint32_t typeIndex);

} // namespace ya
