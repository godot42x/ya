#include "VulkanCommandBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "VulkanImageView.h"
#include "VulkanQueue.h"
#include "VulkanRender.h"


namespace ya
{

// Define the static function pointers for VK_KHR_dynamic_rendering and VK_EXT_extended_dynamic_state3
PFN_vkCmdBeginRenderingKHR VulkanCommandBuffer::s_vkCmdBeginRenderingKHR = nullptr;
PFN_vkCmdEndRenderingKHR   VulkanCommandBuffer::s_vkCmdEndRenderingKHR   = nullptr;
PFN_vkCmdSetPolygonModeEXT VulkanCommandBuffer::s_vkCmdSetPolygonModeEXT = nullptr;

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
#if YA_CMDBUF_RECORD_MODE
    recordedCommands.clear();
#endif
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
#if YA_CMDBUF_RECORD_MODE
    recordedCommands.clear();
#endif
}

// ========== Recording Mode: Internal execute implementations ==========

void VulkanCommandBuffer::executeBindPipeline(IGraphicsPipeline *pipeline)
{
    vkCmdBindPipeline(_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getHandleAs<VkPipeline>());
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

void VulkanCommandBuffer::executeSetPolygonMode(EPolygonMode::T polygonMode)
{
    // Use function pointer if available (requires VK_EXT_extended_dynamic_state3)
    if (s_vkCmdSetPolygonModeEXT != nullptr) {
        s_vkCmdSetPolygonModeEXT(_commandBuffer, EPolygonMode::toVk(polygonMode));
    }
    else {
        YA_CORE_WARN("vkCmdSetPolygonModeEXT not available - VK_EXT_extended_dynamic_state3 may not be enabled");
    }
}

void VulkanCommandBuffer::executeBeginRendering(const DynamicRenderingInfo &info)
{
    if (s_vkCmdBeginRenderingKHR == nullptr) {
        YA_CORE_WARN("vkCmdBeginRenderingKHR not available - VK_KHR_dynamic_rendering may not be enabled");
        return;
    }

    // Convert color attachments
    std::vector<VkRenderingAttachmentInfo> vkColorAttachments;
    vkColorAttachments.reserve(info.colorAttachments.size());

    for (const auto &colorAttach : info.colorAttachments)
    {
        VkRenderingAttachmentInfo vkAttach{
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext       = nullptr,
            .imageView   = colorAttach.imageView->getHandle().as<VkImageView>(),
            .imageLayout = EImageLayout::toVk(colorAttach.imageLayout),
            .resolveMode = VK_RESOLVE_MODE_NONE, // TODO: support resolve modes
            .loadOp      = EAttachmentLoadOp::toVk(colorAttach.loadOp),
            .storeOp     = EAttachmentStoreOp::toVk(colorAttach.storeOp),
            .clearValue  = {},
        };

        if (colorAttach.loadOp == EAttachmentLoadOp::Clear)
        {
            if (colorAttach.clearValue.isDepthStencil)
            {
                vkAttach.clearValue.depthStencil = {
                    .depth   = colorAttach.clearValue.depthStencil.depth,
                    .stencil = colorAttach.clearValue.depthStencil.stencil,
                };
            }
            else
            {
                vkAttach.clearValue.color = {
                    {
                        colorAttach.clearValue.color.r,
                        colorAttach.clearValue.color.g,
                        colorAttach.clearValue.color.b,
                        colorAttach.clearValue.color.a,
                    },
                };
            }
        }

        if (colorAttach.resolveMode != EResolveMode::None)
        {
            vkAttach.resolveMode        = static_cast<VkResolveModeFlagBits>(colorAttach.resolveMode);
            vkAttach.resolveImageView   = static_cast<VkImageView>(colorAttach.resolveImageView);
            vkAttach.resolveImageLayout = EImageLayout::toVk(colorAttach.resolveLayout);
        }

        vkColorAttachments.push_back(vkAttach);
    }

    // Convert depth attachment
    VkRenderingAttachmentInfo vkDepthAttach{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
    };
    VkRenderingAttachmentInfo *pVkDepthAttach = nullptr;

    if (info.pDepthAttachment.imageView != nullptr)
    {
        vkDepthAttach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        vkDepthAttach.imageView   = info.pDepthAttachment.imageView->getHandle().as<VkImageView>();
        vkDepthAttach.imageLayout = EImageLayout::toVk(info.pDepthAttachment.imageLayout);
        vkDepthAttach.loadOp      = EAttachmentLoadOp::toVk(info.pDepthAttachment.loadOp);
        vkDepthAttach.storeOp     = EAttachmentStoreOp::toVk(info.pDepthAttachment.storeOp);

        if (info.pDepthAttachment.loadOp == EAttachmentLoadOp::Clear && info.pDepthAttachment.clearValue.isDepthStencil)
        {
            vkDepthAttach.clearValue.depthStencil = {
                .depth   = info.pDepthAttachment.clearValue.depthStencil.depth,
                .stencil = info.pDepthAttachment.clearValue.depthStencil.stencil,
            };
        }
        pVkDepthAttach = &vkDepthAttach;
    }

    // Convert stencil attachment
    VkRenderingAttachmentInfo vkStencilAttach{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    };
    VkRenderingAttachmentInfo *pVkStencilAttach = nullptr;

    if (info.pStencilAttachment.imageView != nullptr)
    {
        vkStencilAttach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        vkStencilAttach.imageView   = info.pStencilAttachment.imageView->getHandle().as<VkImageView>();
        vkStencilAttach.imageLayout = EImageLayout::toVk(info.pStencilAttachment.imageLayout);
        vkStencilAttach.loadOp      = EAttachmentLoadOp::toVk(info.pStencilAttachment.loadOp);
        vkStencilAttach.storeOp     = EAttachmentStoreOp::toVk(info.pStencilAttachment.storeOp);

        if (info.pStencilAttachment.loadOp == EAttachmentLoadOp::Clear && info.pStencilAttachment.clearValue.isDepthStencil)
        {
            vkStencilAttach.clearValue.depthStencil = {
                .depth   = info.pStencilAttachment.clearValue.depthStencil.depth,
                .stencil = info.pStencilAttachment.clearValue.depthStencil.stencil,
            };
        }
        pVkStencilAttach = &vkStencilAttach;
    }

    // Setup rendering info
    VkRenderingInfo vkRenderingInfo{
        .sType      = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext      = nullptr,
        .flags      = 0,
        .renderArea = {
            .offset = {
                static_cast<int32_t>(info.renderArea.pos.x),
                static_cast<int32_t>(info.renderArea.pos.y),
            },
            .extent = {
                static_cast<uint32_t>(info.renderArea.extent.x),
                static_cast<uint32_t>(info.renderArea.extent.y),
            },
        },
        .layerCount           = info.layerCount,
        .viewMask             = 0,
        .colorAttachmentCount = static_cast<uint32_t>(vkColorAttachments.size()),
        .pColorAttachments    = vkColorAttachments.data(),
        .pDepthAttachment     = pVkDepthAttach,
        .pStencilAttachment   = pVkStencilAttach,
    };

    s_vkCmdBeginRenderingKHR(_commandBuffer, &vkRenderingInfo);
}

void VulkanCommandBuffer::executeEndRendering()
{
    if (s_vkCmdEndRenderingKHR != nullptr) {
        s_vkCmdEndRenderingKHR(_commandBuffer);
    }
    else {
        YA_CORE_WARN("vkCmdEndRenderingKHR not available - VK_KHR_dynamic_rendering may not be enabled");
    }
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

#if YA_CMDBUF_RECORD_MODE
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
                else if constexpr (std::is_same_v<T, RenderCommand::SetPolygonMode>) {
                    executeSetPolygonMode(arg.polygonMode);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BeginRendering>) {
                    executeBeginRendering(arg.info);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::EndRendering>) {
                    executeEndRendering();
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
                else if constexpr (std::is_same_v<T, RenderCommand::TransitionImageLayout>) {
                    (void)arg; // TODO: implement dynamic layout transitions
                }
            },
            cmd.data);
    }
    ICommandBuffer::executeAll();
}
#endif


// ========== Virtual Mode: Direct vkCmd* implementations ==========

void VulkanCommandBuffer::bindPipeline(IGraphicsPipeline *pipeline)
{
    executeBindPipeline(pipeline);
}

void VulkanCommandBuffer::bindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset)
{
    executeBindVertexBuffer(binding, buffer, offset);
}

void VulkanCommandBuffer::bindIndexBuffer(IBuffer *buffer, uint64_t offset, bool use16BitIndices)
{
    executeBindIndexBuffer(buffer, offset, use16BitIndices);
}

void VulkanCommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                               uint32_t firstVertex, uint32_t firstInstance)
{
    executeDraw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                      uint32_t firstIndex, int32_t vertexOffset,
                                      uint32_t firstInstance)
{
    executeDrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandBuffer::setViewport(float x, float y, float width, float height,
                                      float minDepth, float maxDepth)
{
    executeSetViewport(x, y, width, height, minDepth, maxDepth);
}

void VulkanCommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    executeSetScissor(x, y, width, height);
}

void VulkanCommandBuffer::setCullMode(ECullMode::T cullMode)
{
    executeSetCullMode(cullMode);
}

void VulkanCommandBuffer::setPolygonMode(EPolygonMode::T polygonMode)
{
    executeSetPolygonMode(polygonMode);
}

void VulkanCommandBuffer::beginRendering(const DynamicRenderingInfo &info)
{
    executeBeginRendering(info);
}

void VulkanCommandBuffer::endRendering()
{
    executeEndRendering();
}

void VulkanCommandBuffer::bindDescriptorSets(
    IPipelineLayout                        *pipelineLayout,
    uint32_t                                firstSet,
    const std::vector<DescriptorSetHandle> &descriptorSets,
    const std::vector<uint32_t>            &dynamicOffsets)
{
    executeBindDescriptorSets(pipelineLayout, firstSet, descriptorSets, dynamicOffsets);
}

void VulkanCommandBuffer::pushConstants(
    IPipelineLayout *pipelineLayout,
    EShaderStage::T  stages,
    uint32_t         offset,
    uint32_t         size,
    const void      *data)
{
    executePushConstants(pipelineLayout, stages, offset, size, data);
}

void VulkanCommandBuffer::copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                                     uint64_t srcOffset, uint64_t dstOffset)
{
    executeCopyBuffer(src, dst, size, srcOffset, dstOffset);
}

void VulkanCommandBuffer::transitionImageLayout(
    IImage                      *image,
    EImageLayout::T              oldLayout,
    EImageLayout::T              newLayout,
    const ImageSubresourceRange *subresourceRange)
{
    // TODO: precheck the image's layout == oldLayout and != newLayout
    VkImageSubresourceRange range;
    if (subresourceRange) {
        range.aspectMask     = subresourceRange->aspectMask;
        range.baseMipLevel   = subresourceRange->baseMipLevel;
        range.levelCount     = subresourceRange->levelCount;
        range.baseArrayLayer = subresourceRange->baseArrayLayer;
        range.layerCount     = subresourceRange->layerCount;
    }

    VulkanImage::transitionLayout(_commandBuffer,
                                  image->as<VulkanImage>(),
                                  toVk(oldLayout),
                                  toVk(newLayout),
                                  subresourceRange ? &range : nullptr);
}


} // namespace ya