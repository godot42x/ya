#pragma once

#include "core/base.h"

#include "vulkan/vulkan.h"
#include <cstdint>
#include <vector>


extern uint32_t VkFormat2Size(VkFormat format);

struct VertexInput
{
    std::vector<VkVertexInputAttributeDescription> m_VkDescriptions;
    uint32_t                                       m_Offset         = 0;
    uint32_t                                       m_AttributeCount = 0;

    uint32_t m_Binding;


    VertexInput(uint32_t binding)
    {
        m_Binding = binding;
    }


    uint32_t GetStride() const
    {
        return m_Offset; // after last AddAttribute
    }

    VertexInput &AddAttribute(VkFormat format, const std::string &alt)
    {
        m_VkDescriptions.emplace_back(VkVertexInputAttributeDescription{
            .location = m_AttributeCount,
            .binding  = m_Binding,
            .format   = format,
            .offset   = m_Offset,
        });

        ++m_AttributeCount;
        m_Offset += VkFormat2Size(format);

        return *this;
    }

    VkVertexInputBindingDescription GetBindingDescription()
    {
        return {
            .binding   = m_Binding,
            .stride    = GetStride(),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            /*
            VK_VERTEX_INPUT_RATE_VERTEX: 移动到每个顶点后的下一个数据条目
            VK_VERTEX_INPUT_RATE_INSTANCE : 在每个instance之后移动到下一个数据条目
                我们不会使用instancing渲染，所以坚持使用per - vertex data方式。
            */
        };
    }

    std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions() const
    {
        return m_VkDescriptions;
    }
};