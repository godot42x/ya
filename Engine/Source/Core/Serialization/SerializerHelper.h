#pragma once

#include "Core/Base.h"
#include "reflects-core/lib.h"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>


namespace ya
{



// ============================================================================
// 序列化辅助函数
// ============================================================================

namespace SerializerHelper
{
// glm 类型序列化
inline nlohmann::json toJson(const glm::vec2 &v)
{
    return {v.x, v.y};
}

inline nlohmann::json toJson(const glm::vec3 &v)
{
    return {v.x, v.y, v.z};
}

inline nlohmann::json toJson(const glm::vec4 &v)
{
    return {v.x, v.y, v.z, v.w};
}

inline nlohmann::json toJson(const glm::mat4 &m)
{
    nlohmann::json j = nlohmann::json::array();
    for (int i = 0; i < 4; i++) {
        for (int j_col = 0; j_col < 4; j_col++) {
            j.push_back(m[i][j_col]);
        }
    }
    return j;
}

// glm 类型反序列化
inline glm::vec2 toVec2(const nlohmann::json &j)
{
    return {j[0].get<float>(), j[1].get<float>()};
}

inline glm::vec3 toVec3(const nlohmann::json &j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

inline glm::vec4 toVec4(const nlohmann::json &j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

inline glm::mat4 toMat4(const nlohmann::json &j)
{
    glm::mat4 m;
    int       idx = 0;
    for (int i = 0; i < 4; i++) {
        for (int j_col = 0; j_col < 4; j_col++) {
            m[i][j_col] = j[idx++].get<float>();
        }
    }
    return m;
}
} // namespace SerializerHelper
} // namespace ya