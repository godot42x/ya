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

// GLM 类型反射支持
YA_REFLECT_EXTERNAL_BEGIN(glm::vec2)
YA_REFLECT_FIELD(x, )
YA_REFLECT_FIELD(y, )
YA_REFLECT_EXTERNAL_END()

YA_REFLECT_EXTERNAL_BEGIN(glm::vec3)
YA_REFLECT_FIELD(x, )
YA_REFLECT_FIELD(y, )
YA_REFLECT_FIELD(z, )
YA_REFLECT_EXTERNAL_END()

YA_REFLECT_EXTERNAL_BEGIN(glm::vec4)
YA_REFLECT_FIELD(x, )
YA_REFLECT_FIELD(y, )
YA_REFLECT_FIELD(z, )
YA_REFLECT_FIELD(w, )
YA_REFLECT_EXTERNAL_END()


#include "nlohmann/json.hpp"


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