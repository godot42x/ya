#pragma once

#include "Core/FName.h"
#include <any>
#include <reflects-core/lib.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/fwd.hpp>



namespace ya::reflection
{



// ============================================================================
// 元数据构建器
// ============================================================================

struct Meta
{
    static inline FName const Color       = "color";
    static inline FName const Tooltip     = "tooltip";
    static inline FName const Category    = "category";
    static inline FName const DisplayName = "display_name";

    struct ManipulateSpec
    {
        enum Type
        {
            None,
            Slider,
            Drag,
            Input
        };

        Type  type = None;
        float min  = 0.0f;
        float max  = 1.0f;
        float step = 0.1f;

        static inline FName const name = "manipulator_spec";
    };
};

using ManipulatorType = Meta::ManipulateSpec::Type;


/**
 * @brief 元数据构建器
 */
template <typename T>
struct MetaBuilder
{
    Metadata meta;


    auto addFlag(FieldFlags flag)
    {
        meta.flags = (uint32_t)((FieldFlags)meta.flags | flag);
        return *this;
    }


    MetaBuilder &manipulate(float min, float max, float step = 0.1f, Meta::ManipulateSpec::Type type = Meta::ManipulateSpec::Slider)
        requires std::is_arithmetic_v<T>
    {
        meta.set(Meta::ManipulateSpec::name,
                 Meta::ManipulateSpec{
                     .type = type,
                     .min  = min,
                     .max  = max,
                     .step = step,
                 });
        return *this;
    }

    MetaBuilder &color()
        requires std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>
    {
        meta.set(Meta::Color, true);
        return *this;
    }


    MetaBuilder &tooltip(const std::string &text)
    {
        meta.set(Meta::Tooltip, text);
        return *this;
    }

    MetaBuilder &category(const std::string &category)
    {
        meta.set(Meta::Category, category);
        return *this;
    }

    MetaBuilder &displayName(const std::string &name)
    {
        meta.set(Meta::DisplayName, name);
        return *this;
    }
    MetaBuilder &transient()
    {
        addFlag(FieldFlags::Transient);
        return *this;
    }

    MetaBuilder &notSerialized()
    {
        addFlag(FieldFlags::NotSerialized);
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
