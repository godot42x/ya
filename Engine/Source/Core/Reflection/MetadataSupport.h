#pragma once

#include <any>
#include <reflects-core/lib.h>
#include <string>
#include <unordered_map>
#include <vector>


namespace ya::reflection
{

/**
 * @brief 属性元数据 - 类似 UE 的 UPROPERTY
 *
 * 支持的方案对比：
 *
 * 1. **纯宏方案**（当前实现）
 *    - 优点：无需外部工具，编译期完成
 *    - 缺点：语法稍显复杂
 *
 * 2. **代码生成器方案**（如 UE UHT）
 *    - 优点：最灵活，语法最优雅
 *    - 缺点：需要额外构建步骤，增加复杂度
 *
 * 3. **混合方案**（推荐）
 *    - 宏定义 metadata
 *    - 模板元编程处理
 *    - 运行时可查询
 */

// ============================================================================
// 方案1: 简单的元数据标记（编译期）
// ============================================================================

/**
 * @brief 属性元数据标记
 *
 * 使用示例：
 * ```cpp
 * struct MyComponent {
 *     YA_REFLECT_WITH_META(MyComponent,
 *         (health, Meta(EditAnywhere, Range(0, 100), Tooltip("生命值"))),
 *         (speed, Meta(EditAnywhere, Range(0, 10))),
 *         (cachedValue, Meta(NotSerialized))
 *     )
 *
 *     float health = 100.0f;
 *     float speed = 5.0f;
 *     float cachedValue;  // 不序列化
 * };
 * ```
 */



// ============================================================================
// 元数据构建器
// ============================================================================

/**
 * @brief 元数据构建器
 */
struct MetaBuilder
{
    Metadata meta;


    auto addFlag(FieldFlags flag)
    {
        meta.flags = (uint32_t)((FieldFlags)meta.flags | flag);
        return *this;
    }

    MetaBuilder &range(float min, float max)
    {
        // TODO: check is a number type
        meta.set("range_min", min);
        meta.set("range_max", max);
        return *this;
    }

    MetaBuilder &tooltip(const std::string &text)
    {
        meta.set("tooltip", text);
        return *this;
    }

    MetaBuilder &category(const std::string &category)
    {
        meta.set("category", category);
        return *this;
    }

    MetaBuilder &displayName(const std::string &name)
    {
        meta.set("display_name", name);
        return *this;
    }
    MetaBuilder &transient()
    {
        addFlag(FieldFlags::Transient);
        return *this;
    }


    operator Metadata() const { return meta; }
};

// ============================================================================
// 方案3: 简化的宏定义（推荐使用）
// ============================================================================

/**
 * @brief 带元数据的属性定义
 *
 * 使用示例：
 * ```cpp
 * struct PlayerComponent {
 *     YA_PROPERTY(EditAnywhere, Range(0, 100))
 *     float health = 100.0f;
 *
 *     YA_PROPERTY(EditReadOnly)
 *     int level = 1;
 *
 *     YA_PROPERTY(NotSerialized)
 *     glm::vec3 cachedPosition;
 * };
 * ```
 */

// 辅助宏：将标记转换为元数据
#define YA_PROPERTY_META(...)                                                                   \
    [[maybe_unused]] static inline const ::ya::reflection::PropertyMetadata __meta_##__LINE__ = \
        ::ya::reflection::MetaBuilder().__VA_ARGS__;

// 属性标记（供未来扩展）
#define YA_PROPERTY(...) [[maybe_unused]]

} // namespace ya::reflection
