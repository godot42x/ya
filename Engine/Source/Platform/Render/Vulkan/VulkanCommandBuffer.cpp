#include "VulkanCommandBuffer.h"
#include "VulkanQueue.h"
#include "VulkanRender.h"

namespace ya
{

VulkanCommandPool::VulkanCommandPool(VulkanRender *render, VulkanQueue *queue, VkCommandPoolCreateFlags flags)
{
    _render = render;
    VkCommandPoolCreateInfo ci{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = flags,
        .queueFamilyIndex = queue->_familyIndex,
    };

    YA_CORE_ASSERT(vkCreateCommandPool(render->getDevice(), &ci, nullptr, &_handle) == VK_SUCCESS,
                   "Failed to create command pool!");
    YA_CORE_TRACE("Created command pool: {} success, queue family: {}", (uintptr_t)_handle, queue->_familyIndex);
}

bool VulkanCommandPool::allocateCommandBuffer(VkCommandBufferLevel level, VkCommandBuffer &outCommandBuffer)
{
    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = _handle,
        .level              = level,
        .commandBufferCount = 1,
    };

    YA_CORE_ASSERT(vkAllocateCommandBuffers(_render->getDevice(), &allocInfo, &outCommandBuffer) == VK_SUCCESS,
                   "Failed to allocate command buffer!");
    // YA_CORE_TRACE("Allocated command buffer success: {}", (uintptr_t)outCommandBuffer);

    return true;
}

void VulkanCommandPool::cleanup()
{
    VK_DESTROY(CommandPool, _render->getDevice(), _handle);
}

bool VulkanCommandBuffer::begin(bool oneTimeSubmit)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = oneTimeSubmit ? VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 0u,
        .pInheritanceInfo = nullptr,
    };

    VkResult result = vkBeginCommandBuffer(_commandBuffer, &beginInfo);
    if (result == VK_SUCCESS)
    {
        _isRecording = true;
        return true;
    }
    return false;
}

bool VulkanCommandBuffer::end()
{
    VkResult result = vkEndCommandBuffer(_commandBuffer);
    if (result == VK_SUCCESS)
    {
        _isRecording = false;
        return true;
    }
    return false;
}

void VulkanCommandBuffer::reset()
{
    vkResetCommandBuffer(_commandBuffer, 0);
    _isRecording = false;
}

void VulkanCommandBuffer::bindPipeline(IGraphicsPipeline *pipeline)
{
    pipeline->bind(getHandle());
}

void VulkanCommandBuffer::bindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset)
{
    if (!buffer) return;

    VkBuffer     vkBuffer = buffer->getHandleAs<VkBuffer>();
    VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(_commandBuffer, binding, 1, &vkBuffer, &vkOffset);
}

void VulkanCommandBuffer::bindIndexBuffer(IBuffer *buffer, uint64_t offset, bool use16BitIndices)
{
    if (!buffer) return;

    VkBuffer    vkBuffer  = buffer->getHandleAs<VkBuffer>();
    VkIndexType indexType = use16BitIndices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(_commandBuffer, vkBuffer, offset, indexType);
}

void VulkanCommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                               uint32_t firstVertex, uint32_t firstInstance)
{
    vkCmdDraw(_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                      uint32_t firstIndex, int32_t vertexOffset,
                                      uint32_t firstInstance)
{
    vkCmdDrawIndexed(_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandBuffer::setViewport(float x, float y, float width, float height,
                                      float minDepth, float maxDepth)
{
    VkViewport viewport{
        .x        = x,
        .y        = y,
        .width    = width,
        .height   = height,
        .minDepth = minDepth,
        .maxDepth = maxDepth,
    };
    vkCmdSetViewport(_commandBuffer, 0, 1, &viewport);
}

void VulkanCommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    VkRect2D scissor{
        .offset = {x, y},
        .extent = {width, height},
    };
    vkCmdSetScissor(_commandBuffer, 0, 1, &scissor);
}

void VulkanCommandBuffer::setCullMode(ECullMode::T cullMode)
{
    vkCmdSetCullMode(_commandBuffer, ECullMode::toVk(cullMode));
}

void VulkanCommandBuffer::bindDescriptorSets(IPipelineLayout                        *pipelineLayout,
                                             uint32_t                                firstSet,
                                             const std::vector<DescriptorSetHandle> &descriptorSets,
                                             const std::vector<uint32_t>            &dynamicOffsets)
{
    std::vector<VkDescriptorSet> vkDescriptorSets;
    vkDescriptorSets.reserve(descriptorSets.size());

    for (const auto &ds : descriptorSets)
    {
        vkDescriptorSets.push_back(ds.as<VkDescriptorSet>());
    }

    vkCmdBindDescriptorSets(
        _commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout->getHandleAs<VkPipelineLayout>(),
        firstSet,
        static_cast<uint32_t>(vkDescriptorSets.size()),
        vkDescriptorSets.data(),
        static_cast<uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.empty() ? nullptr : dynamicOffsets.data());
}

void VulkanCommandBuffer::pushConstants(IPipelineLayout *pipelineLayout,
                                        EShaderStage::T  stages,
                                        uint32_t         offset,
                                        uint32_t         size,
                                        const void      *data)
{
    VkShaderStageFlags vkStages = 0;
    if (stages & EShaderStage::Vertex) vkStages |= VK_SHADER_STAGE_VERTEX_BIT;
    if (stages & EShaderStage::Fragment) vkStages |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stages & EShaderStage::Geometry) vkStages |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (stages & EShaderStage::Compute) vkStages |= VK_SHADER_STAGE_COMPUTE_BIT;

    vkCmdPushConstants(
        _commandBuffer,
        pipelineLayout->getHandleAs<VkPipelineLayout>(),
        vkStages,
        offset,
        size,
        data);
}

void VulkanCommandBuffer::copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                                     uint64_t srcOffset, uint64_t dstOffset)
{
    if (!src || !dst) return;

    VkBufferCopy copyRegion{
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .size      = size,
    };

    vkCmdCopyBuffer(
        _commandBuffer,
        src->getHandleAs<VkBuffer>(),
        dst->getHandleAs<VkBuffer>(),
        1,
        &copyRegion);
}


} // namespace ya