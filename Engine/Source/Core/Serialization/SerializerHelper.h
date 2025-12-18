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

struct SerializerHelper
{
    // glm 类型序列化
    static nlohmann::json toJson(const glm::vec2 &v)
    {
        return {v.x, v.y};
    }

    static nlohmann::json toJson(const glm::vec3 &v)
    {
        return {v.x, v.y, v.z};
    }

    static nlohmann::json toJson(const glm::vec4 &v)
    {
        return {v.x, v.y, v.z, v.w};
    }

    static nlohmann::json toJson(const glm::mat4 &m)
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
    static glm::vec2 toVec2(const nlohmann::json &j)
    {
        return {j[0].get<float>(), j[1].get<float>()};
    }

    static glm::vec3 toVec3(const nlohmann::json &j)
    {
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    }

    static glm::vec4 toVec4(const nlohmann::json &j)
    {
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    }

    static glm::mat4 toMat4(const nlohmann::json &j)
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

    /**
     * 将 std::any 转换为 JSON
     * 根据类型索引判断类型
     */
    static nlohmann::json anyToJson(const std::any &value, uint32_t typeIndex)
    {
        // 基础类型
        if (typeIndex == type_index_v<int>) {
            return std::any_cast<int>(value);
        }
        else if (typeIndex == type_index_v<float>) {
            return std::any_cast<float>(value);
        }
        else if (typeIndex == type_index_v<double>) {
            return std::any_cast<double>(value);
        }
        else if (typeIndex == type_index_v<bool>) {
            return std::any_cast<bool>(value);
        }
        else if (typeIndex == type_index_v<std::string>) {
            return std::any_cast<std::string>(value);
        }
        // glm 类型
        else if (typeIndex == type_index_v<glm::vec2>) {
            return SerializerHelper::toJson(std::any_cast<glm::vec2>(value));
        }
        else if (typeIndex == type_index_v<glm::vec3>) {
            return SerializerHelper::toJson(std::any_cast<glm::vec3>(value));
        }
        else if (typeIndex == type_index_v<glm::vec4>) {
            return SerializerHelper::toJson(std::any_cast<glm::vec4>(value));
        }
        else if (typeIndex == type_index_v<glm::mat4>) {
            return SerializerHelper::toJson(std::any_cast<glm::mat4>(value));
        }

        YA_CORE_WARN("Unknown type index for serialization: {}", typeIndex);
        return nullptr;
    }

    /**
     * 将 JSON 转换为 std::any
     */
    static std::any jsonToAny(const nlohmann::json &j, size_t typeHash)
    {
        // 基础类型
        if (typeHash == typeid(int).hash_code()) {
            return std::any(j.get<int>());
        }
        else if (typeHash == typeid(float).hash_code()) {
            return std::any(j.get<float>());
        }
        else if (typeHash == typeid(double).hash_code()) {
            return std::any(j.get<double>());
        }
        else if (typeHash == typeid(bool).hash_code()) {
            return std::any(j.get<bool>());
        }
        else if (typeHash == typeid(std::string).hash_code()) {
            return std::any(j.get<std::string>());
        }
        // glm 类型
        else if (typeHash == typeid(glm::vec2).hash_code()) {
            return std::any(SerializerHelper::toVec2(j));
        }
        else if (typeHash == typeid(glm::vec3).hash_code()) {
            return std::any(SerializerHelper::toVec3(j));
        }
        else if (typeHash == typeid(glm::vec4).hash_code()) {
            return std::any(SerializerHelper::toVec4(j));
        }
        else if (typeHash == typeid(glm::mat4).hash_code()) {
            return std::any(SerializerHelper::toMat4(j));
        }

        YA_CORE_ERROR("Unknown type hash for deserialization: {}", typeHash);
        return std::any();
    }
}; // namespace SerializerHelper

} // namespace ya