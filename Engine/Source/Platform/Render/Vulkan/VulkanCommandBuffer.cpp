#include "VulkanCommandBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/RenderPass.h"
#include "Render/Core/Texture.h" // For ImageSpec::texture access
#include "VulkanImageView.h"
#include "VulkanQueue.h"
#include "VulkanRender.h"



namespace ya
{

static void collectRenderTargetTransitions(
    IRenderTarget                              *renderTarget,
    bool                                        useInitialLayout,
    std::vector<VulkanImage::LayoutTransition> &outTransitions,
    EImageLayout::T                             colorOverrideLayout = EImageLayout::Undefined,
    EImageLayout::T                             depthOverrideLayout = EImageLayout::Undefined)
{
    if (!renderTarget) {
        return;
    }

    auto curFrameBuffer = renderTarget->getCurFrameBuffer();
    if (!curFrameBuffer) {
        return;
    }

    const auto &colorDescs    = renderTarget->getColorAttachmentDescs();
    const auto &colorTextures = curFrameBuffer->getColorTextures();
    const auto &depthDesc     = renderTarget->getDepthAttachmentDesc();

    const auto colorCount = std::min(colorTextures.size(), colorDescs.size());
    for (size_t i = 0; i < colorCount; ++i) {
        auto targetLayout = colorOverrideLayout;
        if (targetLayout == EImageLayout::Undefined) {
            targetLayout = useInitialLayout ? colorDescs[i].initialLayout : colorDescs[i].finalLayout;
        }
        if (targetLayout == EImageLayout::Undefined) {
            if (useInitialLayout) {
                targetLayout = EImageLayout::ColorAttachmentOptimal;
            }
            else {
                continue;
            }
        }
        if (auto colorImage = colorTextures[i]->getImage()) {
            if (auto vkImage = dynamic_cast<VulkanImage *>(colorImage)) {
                outTransitions.emplace_back(vkImage, targetLayout);
            }
        }
    }

    if (depthDesc.format != EFormat::Undefined) {
        auto targetLayout = depthOverrideLayout;
        if (targetLayout == EImageLayout::Undefined) {
            targetLayout = useInitialLayout ? depthDesc.initialLayout : depthDesc.finalLayout;
        }
        if (targetLayout == EImageLayout::Undefined) {
            if (useInitialLayout) {
                targetLayout = EImageLayout::DepthStencilAttachmentOptimal;
            }
            else {
                return;
            }
        }
        if (auto depthTex = curFrameBuffer->getDepthTexture()) {
            if (auto depthImage = depthTex->getImage()) {
                if (auto vkImage = dynamic_cast<VulkanImage *>(depthImage)) {
                    outTransitions.emplace_back(vkImage, targetLayout);
                }
            }
        }
    }
}

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

void VulkanCommandBuffer::executeEndRendering(const EndRenderingInfo &info)
{
    if (_currentRenderingMode == ERenderingMode::RenderPass) {
        // End traditional render pass
        vkCmdEndRenderPass(_commandBuffer);
    }
    else if (_currentRenderingMode == ERenderingMode::DynamicRendering) {
        // End dynamic rendering
        if (s_vkCmdEndRenderingKHR != nullptr) {
            s_vkCmdEndRenderingKHR(_commandBuffer);
        }
        else {
            YA_CORE_WARN("vkCmdEndRenderingKHR not available - VK_KHR_dynamic_rendering may not be enabled");
        }
    }

    if (info.renderTarget) {
        if (_currentRenderingMode == ERenderingMode::DynamicRendering) {
            std::vector<VulkanImage::LayoutTransition> transitions;
            collectRenderTargetTransitions(info.renderTarget, false, transitions);
            if (!transitions.empty()) {
                VulkanImage::transitionLayouts(_commandBuffer, transitions);
            }
        }
        info.renderTarget->endFrame(this);
    }

    // Reset rendering mode
    _currentRenderingMode = ERenderingMode::None;
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

void VulkanCommandBuffer::copyBufferToImage(IBuffer *srcBuffer,
                                            IImage *dstImage, EImageLayout::T dstImageLayout,
                                            const std::vector<BufferImageCopy> &regions)
{
    if (!srcBuffer || !dstImage || regions.empty()) return;

    std::vector<VkBufferImageCopy> vkRegions;
    vkRegions.reserve(regions.size());

    for (const auto &region : regions) {
        VkBufferImageCopy vkRegion{
            .bufferOffset      = region.bufferOffset,
            .bufferRowLength   = region.bufferRowLength,
            .bufferImageHeight = region.bufferImageHeight,
            .imageSubresource  = {
                 .aspectMask     = region.imageSubresource.aspectMask,
                 .mipLevel       = region.imageSubresource.mipLevel,
                 .baseArrayLayer = region.imageSubresource.baseArrayLayer,
                 .layerCount     = region.imageSubresource.layerCount,
            },
            .imageOffset = {
                region.imageOffsetX,
                region.imageOffsetY,
                region.imageOffsetZ,
            },
            .imageExtent = {
                region.imageExtentWidth,
                region.imageExtentHeight,
                region.imageExtentDepth,
            },
        };
        vkRegions.push_back(vkRegion);
    }

    vkCmdCopyBufferToImage(
        _commandBuffer,
        srcBuffer->getHandleAs<VkBuffer>(),
        dstImage->getHandle().as<VkImage>(),
        EImageLayout::toVk(dstImageLayout),
        static_cast<uint32_t>(vkRegions.size()),
        vkRegions.data());
}

void VulkanCommandBuffer::executeTransitionImageLayout(IImage *image, EImageLayout::T oldLayout, EImageLayout::T newLayout,
                                                       const ImageSubresourceRange *subresourceRange)
{
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
                    beginRendering(arg.info);
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
    YA_CORE_ASSERT(width != 0 && height != 0, "Viewport width and height must be greater than 0");
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

void VulkanCommandBuffer::beginRendering(const RenderingInfo &info)
{
    // === Mode 1: From RenderTarget (automatic mode selection) ===
    if (info.renderTarget != nullptr) {
        auto renderTarget = info.renderTarget;
        YA_CORE_ASSERT(renderTarget != nullptr, "RenderTarget cannot be null in RenderTarget mode");

        auto renderingMode = renderTarget->getRenderingMode();

        renderTarget->beginFrame(this);

        if (renderingMode == ERenderingMode::RenderPass) {
            beginRenderingWithRenderPass(renderTarget, info);
            return;
        }
        else if (renderingMode == ERenderingMode::DynamicRendering) {
            std::vector<VulkanImage::LayoutTransition> transitions;
            collectRenderTargetTransitions(renderTarget, true, transitions);
            if (!transitions.empty()) {
                VulkanImage::transitionLayouts(_commandBuffer, transitions);
            }
            beginDynamicRenderingFromRenderTarget(renderTarget, info);
            return;
        }
        else {
            YA_CORE_ERROR("Unknown rendering mode: {}", static_cast<int>(renderingMode));
        }
    }
    else {

        beginDynamicRenderingFromManualImages(info);
        return;
    }

    // Should never reach here
    YA_CORE_ERROR("RenderingInfo has invalid mode");
    UNREACHABLE();
}

void VulkanCommandBuffer::beginRenderingWithRenderPass(IRenderTarget *renderTarget, const RenderingInfo &info)
{
    auto renderPass  = renderTarget->getRenderPass();
    auto framebuffer = renderTarget->getCurFrameBuffer();
    auto subpass     = renderTarget->getSubpassIndex();
    YA_CORE_ASSERT(renderPass != nullptr, "RenderPass mode requires a valid renderPass");
    YA_CORE_ASSERT(framebuffer != nullptr, "RenderPass mode requires a valid framebuffer");

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext       = nullptr,
        .renderPass  = renderPass->getHandleAs<VkRenderPass>(),
        .framebuffer = framebuffer->getHandleAs<VkFramebuffer>(),
        .renderArea  = {
             .offset = {
                 .x = 0,
                 .y = 0,
            },
             .extent = {
                 .width  = renderTarget->getExtent().width,
                 .height = renderTarget->getExtent().height,
            },
        },
    };

    // Setup clear values
    std::vector<VkClearValue> vkClearValues;
    vkClearValues.reserve(info.colorClearValues.size() + 1);
    for (const auto &clearValue : info.colorClearValues) {
        VkClearValue vkClear;
        vkClear.color = {
            {
                clearValue.color.r,
                clearValue.color.g,
                clearValue.color.b,
                clearValue.color.a,
            },
        };
        vkClearValues.push_back(vkClear);
    }
    auto passColorAttachmentSize = renderPass->getSubPass(subpass).colorAttachments.size();
    if (info.colorAttachments.size() < passColorAttachmentSize) {
        for (uint32_t i = info.colorClearValues.size(); i < passColorAttachmentSize; ++i) {
            VkClearValue vkClear;
            vkClear.color = {
                .float32 = {0.0f, 0.0f, 0.0f, 0.0f},
            };
            vkClearValues.push_back(vkClear);
        }
    }

    if (renderPass->hasDepthAttachment()) {
        VkClearValue vkClear;
        vkClear.depthStencil = {
            .depth   = info.depthClearValue.depthStencil.depth,
            .stencil = info.depthClearValue.depthStencil.stencil,
        };
        vkClearValues.push_back(vkClear);
    }

    renderPassInfo.clearValueCount = static_cast<uint32_t>(vkClearValues.size());
    renderPassInfo.pClearValues    = vkClearValues.data();
    YA_CORE_ASSERT(renderPass->getAttachmentCount() == vkClearValues.size(), "Clear value count must match attachment count");

    vkCmdBeginRenderPass(_commandBuffer,
                         &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    _currentRenderingMode = ERenderingMode::RenderPass;
}

VkRenderingAttachmentInfo *VulkanCommandBuffer::buildDepthAttachmentInfo(const RenderingInfo       &info,
                                                                         VkRenderingAttachmentInfo &outDepthAttach)
{
    if (!info.depthAttachment) {
        return nullptr;
    }

    outDepthAttach = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext       = nullptr,
        .imageView   = info.depthAttachment->texture->getImageView()->getHandle().as<VkImageView>(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .loadOp      = EAttachmentLoadOp::toVk(info.depthAttachment->loadOp),
        .storeOp     = EAttachmentStoreOp::toVk(info.depthAttachment->storeOp),
        .clearValue  = {},
    };

    if (info.depthClearValue.isDepthStencil) {
        outDepthAttach.clearValue.depthStencil = {
            .depth   = info.depthClearValue.depthStencil.depth,
            .stencil = info.depthClearValue.depthStencil.stencil,
        };
    }

    return &outDepthAttach;
}

void VulkanCommandBuffer::executeDynamicRendering(std::vector<VkRenderingAttachmentInfo> &colorAttachments,
                                                  VkRenderingAttachmentInfo              *pDepthAttach,
                                                  const VkRect2D                         &renderArea,
                                                  uint32_t                                layerCount)
{
    VkRenderingInfo vkRenderingInfo{
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext                = nullptr,
        .flags                = 0,
        .renderArea           = renderArea,
        .layerCount           = layerCount,
        .viewMask             = 0,
        .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
        .pColorAttachments    = colorAttachments.data(),
        .pDepthAttachment     = pDepthAttach,
        .pStencilAttachment   = nullptr,
    };

    s_vkCmdBeginRenderingKHR(_commandBuffer, &vkRenderingInfo);
}

void VulkanCommandBuffer::beginDynamicRenderingFromRenderTarget(IRenderTarget *renderTarget, const RenderingInfo &info)
{
    _currentRenderingMode = ERenderingMode::DynamicRendering;

    if (s_vkCmdBeginRenderingKHR == nullptr) {
        YA_CORE_WARN("vkCmdBeginRenderingKHR not available - VK_KHR_dynamic_rendering may not be enabled");
        return;
    }

    transitionRenderTargetLayout(renderTarget,
                                 EImageLayout::ColorAttachmentOptimal,
                                 EImageLayout::DepthStencilAttachmentOptimal);

    auto curFrameBuffer = renderTarget->getCurFrameBuffer();

    // Build color attachments from framebuffer
    const auto                            &colorTextures = curFrameBuffer->getColorTextures();
    std::vector<VkRenderingAttachmentInfo> vkColorAttachments;
    vkColorAttachments.reserve(colorTextures.size());

    auto colorAttachmentDescs = renderTarget->getColorAttachmentDescs();

    for (uint32_t i = 0; i < colorTextures.size(); ++i) {
        VkRenderingAttachmentInfo vkAttach{
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext       = nullptr,
            .imageView   = colorTextures[i]->getImageView()->getHandle().as<VkImageView>(),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .loadOp      = EAttachmentLoadOp::toVk(colorAttachmentDescs[i].loadOp),
            .storeOp     = EAttachmentStoreOp::toVk(colorAttachmentDescs[i].storeOp),
            .clearValue  = {
                 .color = {{
                    info.colorClearValues[i].color.r,
                    info.colorClearValues[i].color.g,
                    info.colorClearValues[i].color.b,
                    info.colorClearValues[i].color.a,
                }},
            },
        };
        vkColorAttachments.push_back(vkAttach);
    }

    // Build depth attachment
    VkRenderingAttachmentInfo  vkDepthAttach{};
    auto                       depthAttachmentDesc = renderTarget->getDepthAttachmentDesc();
    VkRenderingAttachmentInfo *pVkDepthAttach      = nullptr;
    if (depthAttachmentDesc.format != EFormat::Undefined) {
        vkDepthAttach = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext       = nullptr,
            .imageView   = curFrameBuffer->getDepthTexture()->getImageView()->getHandle().as<VkImageView>(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .loadOp      = EAttachmentLoadOp::toVk(depthAttachmentDesc.loadOp),
            .storeOp     = EAttachmentStoreOp::toVk(depthAttachmentDesc.storeOp),
            .clearValue  = {
                info.depthClearValue.isDepthStencil
                     ? VkClearValue{
                           .depthStencil = {
                               .depth   = info.depthClearValue.depthStencil.depth,
                               .stencil = info.depthClearValue.depthStencil.stencil,
                          },
                      }
                     : VkClearValue{},
            },
        };
        pVkDepthAttach = &vkDepthAttach;
    }



    // Execute dynamic rendering
    VkRect2D renderArea = {
        .offset = {0, 0},
        .extent = {renderTarget->getExtent().width, renderTarget->getExtent().height},
    };
    executeDynamicRendering(vkColorAttachments, pVkDepthAttach, renderArea, 1);
}

void VulkanCommandBuffer::beginDynamicRenderingFromManualImages(const RenderingInfo &info)
{
    _currentRenderingMode = ERenderingMode::DynamicRendering;

    if (s_vkCmdBeginRenderingKHR == nullptr) {
        YA_CORE_WARN("vkCmdBeginRenderingKHR not available - VK_KHR_dynamic_rendering may not be enabled");
        return;
    }

    // Build color attachments from manual images
    std::vector<VkRenderingAttachmentInfo> vkColorAttachments;
    vkColorAttachments.reserve(info.colorAttachments.size());

    for (size_t i = 0; i < info.colorAttachments.size(); ++i) {
        VkRenderingAttachmentInfo vkAttach{
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext       = nullptr,
            .imageView   = info.colorAttachments[i].texture->getImageView()->getHandle().as<VkImageView>(),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .loadOp      = EAttachmentLoadOp::toVk(info.colorAttachments[i].loadOp),
            .storeOp     = EAttachmentStoreOp::toVk(info.colorAttachments[i].storeOp),
            .clearValue  = {
                 .color = {{
                    info.colorClearValues[i].color.r,
                    info.colorClearValues[i].color.g,
                    info.colorClearValues[i].color.b,
                    info.colorClearValues[i].color.a,
                }},
            },
        };
        vkColorAttachments.push_back(vkAttach);
    }

    // Build depth attachment using shared helper
    VkRenderingAttachmentInfo  vkDepthAttach{};
    VkRenderingAttachmentInfo *pVkDepthAttach = buildDepthAttachmentInfo(info, vkDepthAttach);

    // Execute dynamic rendering
    VkRect2D renderArea = {
        .offset = {static_cast<int32_t>(info.renderArea.pos.x), static_cast<int32_t>(info.renderArea.pos.y)},
        .extent = {static_cast<uint32_t>(info.renderArea.extent.x), static_cast<uint32_t>(info.renderArea.extent.y)},
    };
    executeDynamicRendering(vkColorAttachments, pVkDepthAttach, renderArea, info.layerCount);
}

void VulkanCommandBuffer::endRendering(const EndRenderingInfo &info)
{
    executeEndRendering(info);
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

void VulkanCommandBuffer::copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset)
{
    executeCopyBuffer(src, dst, size, srcOffset, dstOffset);
}

void VulkanCommandBuffer::transitionImageLayout(
    IImage                      *image,
    EImageLayout::T              oldLayout,
    EImageLayout::T              newLayout,
    const ImageSubresourceRange *subresourceRange)
{
    executeTransitionImageLayout(image, oldLayout, newLayout, subresourceRange);
}

void VulkanCommandBuffer::transitionImageLayoutAuto(IImage *image, EImageLayout::T newLayout, const ImageSubresourceRange *subresourceRange)
{
    // the layout changed in cmdbuf:
    // may not really layout in gpu, but will be the layout at execution
    auto curLayout = image->getLayout();
    if (curLayout != newLayout) {
        transitionImageLayout(image, curLayout, newLayout, subresourceRange);
    }
    // already in the layout after cmdbuf execute
}

void VulkanCommandBuffer::transitionRenderTargetLayout(
    IRenderTarget  *renderTarget,
    EImageLayout::T colorLayout,
    EImageLayout::T depthLayout)
{
    std::vector<VulkanImage::LayoutTransition> transitions;
    collectRenderTargetTransitions(renderTarget, true, transitions, colorLayout, depthLayout);

    if (!transitions.empty()) {
        VulkanImage::transitionLayouts(_commandBuffer, transitions);
    }
}


} // namespace ya