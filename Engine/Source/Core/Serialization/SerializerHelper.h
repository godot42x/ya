#pragma once

#include "Core/Base.h"
#include "reflects-core/lib.h"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace ya
{

// 前置声明，避免循环依赖
class TypeRegistry;

// ============================================================================
// 序列化辅助函数 - 使用 TypeRegistry 统一管理
// ============================================================================

struct SerializerHelper
{
    /**
     * 将 std::any 转换为 JSON
     * 根据类型索引判断类型
     */
    static nlohmann::json anyToJson(const std::any &value, uint32_t typeIndex);

    /**
     * 将 JSON 转换为 std::any
     */
    static std::any jsonToAny(const nlohmann::json &j, size_t typeHash);
};

} // namespace ya
