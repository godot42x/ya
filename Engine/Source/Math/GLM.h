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
 * #include "Math/GLMer.h"
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

#include "Core/Reflection/UnifiedReflection.h"


YA_REFLECT_EXTERNAL(glm::vec2,
                    PROP(x),
                    PROP(y));
YA_REFLECT_EXTERNAL(glm::vec3,
                    PROP(x),
                    PROP(y),
                    PROP(z));
YA_REFLECT_EXTERNAL(glm::vec4,
                    PROP(x),
                    PROP(y),
                    PROP(z),
                    PROP(w));