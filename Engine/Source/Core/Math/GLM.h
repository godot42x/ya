/**
 * @file GLMer.h
 * @brief GLM 统一包含文件
 *
 * 使用此文件代替直接包含 GLM 头文件，自动获得：
 * 1. GLM 核心功能
 * 2. 常用扩展（quaternion, matrix_transform 等）
 * 3. 反射系统支持（glm::vec2/vec3/vec4 序列化）
 *
 * @example
 * // 替代以下分散的包含：
 * // #include <glm/glm.hpp>
 * // #include <glm/gtc/matrix_transform.hpp>
 * // #include "Core/Reflection/GlmReflection.h"
 *
 * // 只需一行：
 * #include "Core/Math/GLMer.h"
 */

#pragma once

// GLM 核心
#include <glm/glm.hpp>

// 常用扩展
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Quaternion 支持
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ReflectionSerializer.h"

// GLM 类型反射支持
YA_REFLECT_BEGIN_EXTERNAL(glm::vec2)
YA_REFLECT_FIELD(x)
YA_REFLECT_FIELD(y)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(glm::vec3)
YA_REFLECT_FIELD(x)
YA_REFLECT_FIELD(y)
YA_REFLECT_FIELD(z)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(glm::vec4)
YA_REFLECT_FIELD(x)
YA_REFLECT_FIELD(y)
YA_REFLECT_FIELD(z)
YA_REFLECT_FIELD(w)
YA_REFLECT_END_EXTERNAL()


#include "nlohmann/json.hpp"

#include <stdexcept>


namespace glm
{

inline void to_json(nlohmann::json &j, const glm::vec2 &vec)
{
    j = nlohmann::json{{"x", vec.x}, {"y", vec.y}};
}
inline void from_json(const nlohmann::json &j, glm::vec2 &vec)
{
    j.at("x").get_to(vec.x);
    j.at("y").get_to(vec.y);
}

} // namespace glm

namespace ya::reflection::detail
{

inline nlohmann::json serializeGlmVec2AsArray(const glm::vec2 &value)
{
    return nlohmann::json::array({value.x, value.y});
}

inline void deserializeGlmVec2FromArray(glm::vec2 &value, const nlohmann::json &j)
{
    if (j.is_array() && j.size() == 2) {
        value.x = j[0].get<float>();
        value.y = j[1].get<float>();
        return;
    }
    if (j.is_object()) {
        value.x = j.at("x").get<float>();
        value.y = j.at("y").get<float>();
        return;
    }
    throw std::runtime_error("glm::vec2 expects [x, y] or {x, y}");
}

inline nlohmann::json serializeGlmVec3AsArray(const glm::vec3 &value)
{
    return nlohmann::json::array({value.x, value.y, value.z});
}

inline void deserializeGlmVec3FromArray(glm::vec3 &value, const nlohmann::json &j)
{
    if (j.is_array() && j.size() == 3) {
        value.x = j[0].get<float>();
        value.y = j[1].get<float>();
        value.z = j[2].get<float>();
        return;
    }
    if (j.is_object()) {
        value.x = j.at("x").get<float>();
        value.y = j.at("y").get<float>();
        value.z = j.at("z").get<float>();
        return;
    }
    throw std::runtime_error("glm::vec3 expects [x, y, z] or {x, y, z}");
}

inline nlohmann::json serializeGlmVec4AsArray(const glm::vec4 &value)
{
    return nlohmann::json::array({value.x, value.y, value.z, value.w});
}

inline void deserializeGlmVec4FromArray(glm::vec4 &value, const nlohmann::json &j)
{
    if (j.is_array() && j.size() == 4) {
        value.x = j[0].get<float>();
        value.y = j[1].get<float>();
        value.z = j[2].get<float>();
        value.w = j[3].get<float>();
        return;
    }
    if (j.is_object()) {
        value.x = j.at("x").get<float>();
        value.y = j.at("y").get<float>();
        value.z = j.at("z").get<float>();
        value.w = j.at("w").get<float>();
        return;
    }
    throw std::runtime_error("glm::vec4 expects [x, y, z, w] or {x, y, z, w}");
}

} // namespace ya::reflection::detail

YA_REGISTER_SERIALIZER_HOOK(glm::vec2, ::ya::reflection::detail::serializeGlmVec2AsArray, ::ya::reflection::detail::deserializeGlmVec2FromArray)
YA_REGISTER_SERIALIZER_HOOK(glm::vec3, ::ya::reflection::detail::serializeGlmVec3AsArray, ::ya::reflection::detail::deserializeGlmVec3FromArray)
YA_REGISTER_SERIALIZER_HOOK(glm::vec4, ::ya::reflection::detail::serializeGlmVec4AsArray, ::ya::reflection::detail::deserializeGlmVec4FromArray)
