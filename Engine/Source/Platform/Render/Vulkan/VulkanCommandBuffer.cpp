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
        .pNext              = nullptr,
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
    recordedCommands.clear();
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
    recordedCommands.clear();
}

void VulkanCommandBuffer::executeBindPipeline(IGraphicsPipeline *pipeline)
{
    pipeline->bind(getHandle());
}

void VulkanCommandBuffer::executeBindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset)
{
    if (!buffer) return;

    VkBuffer     vkBuffer = buffer->getHandleAs<VkBuffer>();
    VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(_commandBuffer, binding, 1, &vkBuffer, &vkOffset);
}

void VulkanCommandBuffer::executeBindIndexBuffer(IBuffer *buffer, uint64_t offset, bool use16BitIndices)
{
    if (!buffer) return;

    VkBuffer    vkBuffer  = buffer->getHandleAs<VkBuffer>();
    VkIndexType indexType = use16BitIndices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(_commandBuffer, vkBuffer, offset, indexType);
}

void VulkanCommandBuffer::executeDraw(uint32_t vertexCount, uint32_t instanceCount,
                                      uint32_t firstVertex, uint32_t firstInstance)
{
    vkCmdDraw(_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandBuffer::executeDrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                             uint32_t firstIndex, int32_t vertexOffset,
                                             uint32_t firstInstance)
{
    vkCmdDrawIndexed(_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandBuffer::executeSetViewport(float x, float y, float width, float height,
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

void VulkanCommandBuffer::executeSetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    VkRect2D scissor{
        .offset = {x, y},
        .extent = {width, height},
    };
    vkCmdSetScissor(_commandBuffer, 0, 1, &scissor);
}

void VulkanCommandBuffer::executeSetCullMode(ECullMode::T cullMode)
{
    vkCmdSetCullMode(_commandBuffer, ECullMode::toVk(cullMode));
}

void VulkanCommandBuffer::executeBindDescriptorSets(IPipelineLayout                        *pipelineLayout,
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

void VulkanCommandBuffer::executePushConstants(IPipelineLayout *pipelineLayout,
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

void VulkanCommandBuffer::executeCopyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
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

void VulkanCommandBuffer::executeAll()
{
    for (const auto &cmd : recordedCommands) {
        std::visit(
            [&](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, RenderCommand::BindPipeline>) {
                    executeBindPipeline(arg.pipeline);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BindVertexBuffer>) {
                    executeBindVertexBuffer(arg.binding, arg.buffer, arg.offset);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BindIndexBuffer>) {
                    executeBindIndexBuffer(arg.buffer, arg.offset, arg.use16BitIndices);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::Draw>) {
                    executeDraw(arg.vertexCount, arg.instanceCount, arg.firstVertex, arg.firstInstance);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::DrawIndexed>) {
                    executeDrawIndexed(arg.indexCount, arg.instanceCount, arg.firstIndex, arg.vertexOffset, arg.firstInstance);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::SetViewPort>) {
                    executeSetViewport(arg.x, arg.y, arg.width, arg.height, arg.minDepth, arg.maxDepth);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::SetScissor>) {
                    executeSetScissor(arg.x, arg.y, arg.width, arg.height);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::SetCullMode>) {
                    executeSetCullMode(arg.cullMode);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BindDescriptorSets>) {
                    executeBindDescriptorSets(arg.pipelineLayout, arg.firstSet, arg.descriptorSets, arg.dynamicOffsets);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::PushConstants>) {
                    executePushConstants(arg.pipelineLayout, arg.stages, arg.offset, static_cast<uint32_t>(arg.data.size()), arg.data.data());
                }
                else if constexpr (std::is_same_v<T, RenderCommand::CopyBuffer>) {
                    executeCopyBuffer(arg.src, arg.dst, arg.size, arg.srcOffset, arg.dstOffset);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BeginRendering> ||
                                   std::is_same_v<T, RenderCommand::EndRendering> ||
                                   std::is_same_v<T, RenderCommand::TransitionImageLayout>) {
                    (void)arg; // TODO: implement dynamic rendering/layout transitions
                }
            },
            cmd.data);
    }
    ICommandBuffer::executeAll();
}


} // namespace ya