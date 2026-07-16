#include "VulkanCommandBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/RenderPass.h"
#include "Render/Core/RenderingInfoUtils.h"
#include "VulkanImageView.h"
#include "VulkanQueue.h"
#include "VulkanRender.h"



namespace ya
{

namespace
{

const char* toDebugString(EImageLayout::T layout)
{
    switch (layout) {
        case EImageLayout::Undefined:
            return "Undefined";
        case EImageLayout::ColorAttachmentOptimal:
            return "ColorAttachmentOptimal";
        case EImageLayout::DepthStencilAttachmentOptimal:
            return "DepthStencilAttachmentOptimal";
        case EImageLayout::ShaderReadOnlyOptimal:
            return "ShaderReadOnlyOptimal";
        case EImageLayout::TransferSrc:
            return "TransferSrc";
        case EImageLayout::TransferDst:
            return "TransferDst";
        case EImageLayout::PresentSrcKHR:
            return "PresentSrcKHR";
        case EImageLayout::General:
            return "General";
        default:
            return "Unknown";
    }
}

VkResolveModeFlagBits toVkResolveMode(EResolveMode::T mode)
{
    switch (mode) {
        case EResolveMode::None:
            return VK_RESOLVE_MODE_NONE;
        case EResolveMode::Average:
            return VK_RESOLVE_MODE_AVERAGE_BIT;
        case EResolveMode::Min:
            return VK_RESOLVE_MODE_MIN_BIT;
        case EResolveMode::Max:
            return VK_RESOLVE_MODE_MAX_BIT;
        default:
            return VK_RESOLVE_MODE_NONE;
    }
}

std::string formatSubresourceRange(const ImageSubresourceRange* range)
{
    if (!range) {
        return "whole-image";
    }

    return std::format(
        "aspect=0x{:x}, mip=[{}, {}), layer=[{}, {})",
        range->aspectMask,
        range->baseMipLevel,
        range->baseMipLevel + range->levelCount,
        range->baseArrayLayer,
        range->baseArrayLayer + range->layerCount);
}

void retainRenderImageResources(ICommandBuffer& cmdBuf, const RenderImage& image)
{
    cmdBuf.retainResource(image.getImageShared());
    cmdBuf.retainResource(image.getImageViewShared());
    cmdBuf.retainResources(image.getRetainedResources());
}

bool hasExplicitRenderAttachments(const RenderAttachmentSet& attachments)
{
    return !attachments.colors.empty() || attachments.depth.has_value();
}

void validateRenderAttachment(const RenderAttachment& attachment, const std::string& renderingLabel, const std::string& attachmentLabel)
{
    YA_CORE_ASSERT(attachment.image != nullptr,
                   "RenderingInfo '{}' {} attachment is missing image",
                   renderingLabel,
                   attachmentLabel);
    YA_CORE_ASSERT(attachment.imageView != nullptr,
                   "RenderingInfo '{}' {} attachment is missing image view",
                   renderingLabel,
                   attachmentLabel);
    YA_CORE_ASSERT(renderAttachmentMatchesImageViewImage(attachment),
                   "RenderingInfo '{}' {} attachment image/view mismatch: image={}, imageViewImage={}, imageView={}",
                   renderingLabel,
                   attachmentLabel,
                   reinterpret_cast<uintptr_t>(attachment.image),
                   reinterpret_cast<uintptr_t>(attachment.imageView ? attachment.imageView->getImage() : nullptr),
                   reinterpret_cast<uintptr_t>(attachment.imageView));

    if (attachment.resolveImage || attachment.resolveImageView) {
        YA_CORE_ASSERT(attachment.resolveImage != nullptr && attachment.resolveImageView != nullptr,
                       "RenderingInfo '{}' {} resolve attachment must provide both image and image view",
                       renderingLabel,
                       attachmentLabel);
        YA_CORE_ASSERT(attachment.resolveImageView->getImage() == attachment.resolveImage,
                       "RenderingInfo '{}' {} resolve attachment image/view mismatch: image={}, imageViewImage={}, imageView={}",
                       renderingLabel,
                       attachmentLabel,
                       reinterpret_cast<uintptr_t>(attachment.resolveImage),
                       reinterpret_cast<uintptr_t>(attachment.resolveImageView ? attachment.resolveImageView->getImage() : nullptr),
                       reinterpret_cast<uintptr_t>(attachment.resolveImageView));
    }

    if (attachment.bHasSubresourceRange) {
        const auto& viewRange = attachment.imageView->getSubresourceRange();
        const ImageSubresourceRange attachmentRange{
            .aspectMask     = static_cast<EImageAspect::T>(attachment.subresourceAspectMask),
            .baseMipLevel   = attachment.subresourceBaseMipLevel,
            .levelCount     = attachment.subresourceLevelCount,
            .baseArrayLayer = attachment.subresourceBaseArrayLayer,
            .layerCount     = attachment.subresourceLayerCount,
        };
        YA_CORE_ASSERT(viewRange.aspectMask == static_cast<EImageAspect::T>(attachment.subresourceAspectMask) &&
                           viewRange.baseMipLevel == attachment.subresourceBaseMipLevel &&
                           viewRange.levelCount == attachment.subresourceLevelCount &&
                           viewRange.baseArrayLayer == attachment.subresourceBaseArrayLayer &&
                           viewRange.layerCount == attachment.subresourceLayerCount,
                       "RenderingInfo '{}' {} attachment subresource mismatch: spec={}, view={}",
                       renderingLabel,
                       attachmentLabel,
                       formatSubresourceRange(&attachmentRange),
                       formatSubresourceRange(&viewRange));
    }
}

void collectAttachmentTransitions(
    const RenderAttachmentSet&                    attachments,
    bool                                          bInitial,
    std::vector<VulkanImage::LayoutTransition>&   outTransitions)
{
    for (const auto& attachment : attachments.colors) {
        auto targetLayout = bInitial ? attachment.initialLayout : attachment.finalLayout;
        if (targetLayout == EImageLayout::Undefined && bInitial) {
            targetLayout = EImageLayout::ColorAttachmentOptimal;
        }
        if (targetLayout != EImageLayout::Undefined && attachment.image) {
            if (auto* vkImage = dynamic_cast<VulkanImage*>(attachment.image)) {
                VulkanImage::LayoutTransition transition{vkImage, targetLayout};
                if (tryResolveRenderAttachmentSubresourceRange(attachment, transition.range)) {
                    transition.useRange = true;
                }
                outTransitions.push_back(transition);
            }
        }
        if (targetLayout != EImageLayout::Undefined && attachment.resolveImage) {
            if (auto* vkImage = dynamic_cast<VulkanImage*>(attachment.resolveImage)) {
                outTransitions.push_back(VulkanImage::LayoutTransition{vkImage, targetLayout});
            }
        }
    }

    if (!attachments.depth) {
        return;
    }

    const auto& attachment = *attachments.depth;
    auto        targetLayout = bInitial ? attachment.initialLayout : attachment.finalLayout;
    if (targetLayout == EImageLayout::Undefined && bInitial) {
        targetLayout = EImageLayout::DepthStencilAttachmentOptimal;
    }
    if (targetLayout == EImageLayout::Undefined || !attachment.image) {
        return;
    }
    if (auto* vkImage = dynamic_cast<VulkanImage*>(attachment.image)) {
        VulkanImage::LayoutTransition transition{vkImage, targetLayout};
        if (tryResolveRenderAttachmentSubresourceRange(attachment, transition.range)) {
            transition.useRange = true;
        }
        outTransitions.push_back(transition);
    }
}

ImageResourceState inferTrackedImageState(EImageLayout::T layout)
{
    ImageResourceState state;
    state.layout = layout;

    switch (layout) {
        case EImageLayout::Undefined:
            break;
        case EImageLayout::ColorAttachmentOptimal:
            state.stages = EPipelineStage::ColorAttachmentOutput;
            state.access = EResourceAccess::ColorAttachmentRead | EResourceAccess::ColorAttachmentWrite;
            break;
        case EImageLayout::DepthStencilAttachmentOptimal:
            state.stages = EPipelineStage::EarlyFragmentTests | EPipelineStage::LateFragmentTests;
            state.access = EResourceAccess::DepthStencilAttachmentRead | EResourceAccess::DepthStencilAttachmentWrite;
            break;
        case EImageLayout::ShaderReadOnlyOptimal:
            state.stages = EPipelineStage::FragmentShader | EPipelineStage::ComputeShader;
            state.access = EResourceAccess::ShaderRead;
            break;
        case EImageLayout::TransferSrc:
            state.stages = EPipelineStage::Transfer;
            state.access = EResourceAccess::TransferRead;
            break;
        case EImageLayout::TransferDst:
            state.stages = EPipelineStage::Transfer;
            state.access = EResourceAccess::TransferWrite;
            break;
        case EImageLayout::General:
            state.stages = EPipelineStage::AllCommands;
            state.access = EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite;
            break;
        case EImageLayout::PresentSrcKHR:
            state.stages = EPipelineStage::AllCommands;
            state.access = EResourceAccess::None;
            break;
        default:
            state.stages = EPipelineStage::AllCommands;
            break;
    }

    return state;
}

} // namespace

// Define the static function pointers for VK_KHR_dynamic_rendering and VK_EXT_extended_dynamic_state3
PFN_vkCmdBeginRenderingKHR       VulkanCommandBuffer::s_vkCmdBeginRenderingKHR       = nullptr;
PFN_vkCmdEndRenderingKHR         VulkanCommandBuffer::s_vkCmdEndRenderingKHR         = nullptr;
PFN_vkCmdSetPolygonModeEXT       VulkanCommandBuffer::s_vkCmdSetPolygonModeEXT       = nullptr;
PFN_vkCmdBeginDebugUtilsLabelEXT VulkanCommandBuffer::s_vkCmdBeginDebugUtilsLabelEXT = nullptr;
PFN_vkCmdEndDebugUtilsLabelEXT   VulkanCommandBuffer::s_vkCmdEndDebugUtilsLabelEXT   = nullptr;

VulkanCommandPool::VulkanCommandPool(VulkanRender* render, VulkanQueue* queue, VkCommandPoolCreateFlags flags)
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

bool VulkanCommandPool::allocateCommandBuffer(VkCommandBufferLevel level, VkCommandBuffer& outCommandBuffer)
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
    clearRetainedResources();
#if YA_CMDBUF_RECORD_MODE
    recordedCommands.clear();
#endif
    _debugLabelDepth = 0;
    _resourceStateTracker.reset();
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
    _debugLabelDepth = 0;
    _resourceStateTracker.reset();
    clearRetainedResources();
#if YA_CMDBUF_RECORD_MODE
    recordedCommands.clear();
#endif
}

// ========== Recording Mode: Internal execute implementations ==========

void VulkanCommandBuffer::executeBindPipeline(IGraphicsPipeline* pipeline)
{
    YA_CORE_ASSERT(pipeline && pipeline->getHandle(), "null ppl");
    vkCmdBindPipeline(_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getHandleAs<VkPipeline>());
}

void VulkanCommandBuffer::executeBindComputePipeline(IComputePipeline* pipeline)
{
    YA_CORE_ASSERT(pipeline && pipeline->getHandle(), "null ppl");
    vkCmdBindPipeline(_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getHandleAs<VkPipeline>());
}

void VulkanCommandBuffer::executeBindVertexBuffer(uint32_t binding, const IBuffer* buffer, uint64_t offset)
{
    if (!buffer) return;

    VkBuffer     vkBuffer = buffer->getHandleAs<VkBuffer>();
    VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(_commandBuffer, binding, 1, &vkBuffer, &vkOffset);
}

void VulkanCommandBuffer::executeBindIndexBuffer(IBuffer* buffer, uint64_t offset, bool use16BitIndices)
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

void VulkanCommandBuffer::executeDrawIndirect(IBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
{
    if (!buffer || drawCount == 0) return;
    vkCmdDrawIndirect(_commandBuffer, buffer->getHandleAs<VkBuffer>(), offset, drawCount, stride);
}

void VulkanCommandBuffer::executeDrawIndexedIndirect(IBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
{
    if (!buffer || drawCount == 0) return;
    vkCmdDrawIndexedIndirect(_commandBuffer, buffer->getHandleAs<VkBuffer>(), offset, drawCount, stride);
}

void VulkanCommandBuffer::executeDrawIndexedIndirectCount(IBuffer* drawBuffer, uint64_t drawOffset,
                                                          IBuffer* countBuffer, uint64_t countOffset,
                                                          uint32_t maxDrawCount, uint32_t stride)
{
    if (!drawBuffer || !countBuffer || maxDrawCount == 0) return;
    vkCmdDrawIndexedIndirectCount(_commandBuffer,
                                  drawBuffer->getHandleAs<VkBuffer>(), drawOffset,
                                  countBuffer->getHandleAs<VkBuffer>(), countOffset,
                                  maxDrawCount, stride);
}

void VulkanCommandBuffer::executeFillBuffer(IBuffer* buffer, uint64_t offset, uint64_t size, uint32_t value)
{
    if (!buffer || size == 0) return;
    vkCmdFillBuffer(_commandBuffer, buffer->getHandleAs<VkBuffer>(), offset, size, value);
}

void VulkanCommandBuffer::executeDispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    vkCmdDispatch(_commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandBuffer::executeDispatchIndirect(IBuffer* buffer, uint64_t offset)
{
    if (!buffer) return;
    vkCmdDispatchIndirect(_commandBuffer, buffer->getHandleAs<VkBuffer>(), offset);
}

void VulkanCommandBuffer::executeBufferMemoryBarrier(IBuffer* buffer,
                                                     EPipelineStage::T srcStage,
                                                     EPipelineStage::T dstStage,
                                                     EResourceAccess::T srcAccess,
                                                     EResourceAccess::T dstAccess,
                                                     uint64_t offset,
                                                     uint64_t size)
{
    if (!buffer) return;
    VkBufferMemoryBarrier barrier{
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = EResourceAccess::toVk(srcAccess),
        .dstAccessMask       = EResourceAccess::toVk(dstAccess),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = buffer->getHandleAs<VkBuffer>(),
        .offset              = offset,
        .size                = size == 0 ? VK_WHOLE_SIZE : size,
    };
    vkCmdPipelineBarrier(_commandBuffer,
                         EPipelineStage::toVk(srcStage),
                         EPipelineStage::toVk(dstStage),
                         0,
                         0,
                         nullptr,
                         1,
                         &barrier,
                         0,
                         nullptr);
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

void VulkanCommandBuffer::executeEndRendering(const RenderingInfo& info)
{
    if (_currentRenderingMode == ERenderingMode::DynamicRendering) {
        // End dynamic rendering
        if (s_vkCmdEndRenderingKHR != nullptr) {
            s_vkCmdEndRenderingKHR(_commandBuffer);
        }
        else {
            YA_CORE_WARN("vkCmdEndRenderingKHR not available - VK_KHR_dynamic_rendering may not be enabled");
        }

        if (!info.bExternalTransitionManagement) {
            // Final layout transitions for manual image path
            std::vector<VulkanImage::LayoutTransition> transitions;
            for (auto& attachment : info.attachments.colors) {
                if (attachment.finalLayout != EImageLayout::Undefined && attachment.image) {
                    if (auto* vkImg = dynamic_cast<VulkanImage*>(attachment.image)) {
                        VulkanImage::LayoutTransition transition{vkImg, attachment.finalLayout};
                        if (tryResolveRenderAttachmentSubresourceRange(attachment, transition.range)) {
                            transition.useRange = true;
                        }
                        transitions.push_back(transition);
                    }
                }

                if (attachment.finalLayout != EImageLayout::Undefined && attachment.resolveImage) {
                    if (auto* vkImg = dynamic_cast<VulkanImage*>(attachment.resolveImage)) {
                        transitions.push_back(VulkanImage::LayoutTransition{vkImg, attachment.finalLayout});
                    }
                }
            }
            if (info.attachments.depth) {
                auto& attachment = *info.attachments.depth;
                if (attachment.finalLayout != EImageLayout::Undefined && attachment.image) {
                    if (auto* vkImg = dynamic_cast<VulkanImage*>(attachment.image)) {
                        VulkanImage::LayoutTransition transition{vkImg, attachment.finalLayout};
                        if (tryResolveRenderAttachmentSubresourceRange(attachment, transition.range)) {
                            transition.useRange = true;
                        }
                        transitions.push_back(transition);
                    }
                }
            }
            if (!transitions.empty()) {
                for (const auto& transition : transitions) {
                    executeTrackedTransition(transition.image, transition.newLayout, transition.useRange ? &transition.range : nullptr);
                }
            }
        }
    }

    // Reset rendering mode
    _currentRenderingMode = ERenderingMode::None;
}

void VulkanCommandBuffer::executeBindDescriptorSets(IPipelineLayout*                        pipelineLayout,
                                                    uint32_t                                firstSet,
                                                    const std::vector<DescriptorSetHandle>& descriptorSets,
                                                    const std::vector<uint32_t>&            dynamicOffsets,
                                                    VkPipelineBindPoint                     bindPoint)
{
    std::vector<VkDescriptorSet> vkDescriptorSets;
    vkDescriptorSets.reserve(descriptorSets.size());

    for (const auto& ds : descriptorSets)
    {
        vkDescriptorSets.push_back(ds.as<VkDescriptorSet>());
    }

    vkCmdBindDescriptorSets(
        _commandBuffer,
        bindPoint,
        pipelineLayout->getHandleAs<VkPipelineLayout>(),
        firstSet,
        static_cast<uint32_t>(vkDescriptorSets.size()),
        vkDescriptorSets.data(),
        static_cast<uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.empty() ? nullptr : dynamicOffsets.data());
}

void VulkanCommandBuffer::executePushConstants(IPipelineLayout* pipelineLayout,
                                               EShaderStage::T  stages,
                                               uint32_t         offset,
                                               uint32_t         size,
                                               const void*      data)
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

void VulkanCommandBuffer::executeCopyBuffer(IBuffer* src, IBuffer* dst, uint64_t size,
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

void VulkanCommandBuffer::copyBufferToImage(IBuffer* srcBuffer,
                                            IImage* dstImage, EImageLayout::T dstImageLayout,
                                            const std::vector<BufferImageCopy>& regions)
{
    if (!srcBuffer || !dstImage || regions.empty()) return;

    std::vector<VkBufferImageCopy> vkRegions;
    vkRegions.reserve(regions.size());

    for (const auto& region : regions) {
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

void VulkanCommandBuffer::copyImage(IImage*                       srcImage,
                                    EImageLayout::T               srcImageLayout,
                                    IImage*                       dstImage,
                                    EImageLayout::T               dstImageLayout,
                                    const std::vector<ImageCopy>& regions)
{
    if (!srcImage || !dstImage || regions.empty()) return;

    std::vector<VkImageCopy> vkRegions;
    vkRegions.reserve(regions.size());

    for (const auto& region : regions) {
        VkImageCopy vkRegion{
            .srcSubresource = {
                .aspectMask     = region.srcSubresource.aspectMask,
                .mipLevel       = region.srcSubresource.mipLevel,
                .baseArrayLayer = region.srcSubresource.baseArrayLayer,
                .layerCount     = region.srcSubresource.layerCount,
            },
            .srcOffset = {
                region.srcOffsetX,
                region.srcOffsetY,
                region.srcOffsetZ,
            },
            .dstSubresource = {
                .aspectMask     = region.dstSubresource.aspectMask,
                .mipLevel       = region.dstSubresource.mipLevel,
                .baseArrayLayer = region.dstSubresource.baseArrayLayer,
                .layerCount     = region.dstSubresource.layerCount,
            },
            .dstOffset = {
                region.dstOffsetX,
                region.dstOffsetY,
                region.dstOffsetZ,
            },
            .extent = {
                region.extentWidth,
                region.extentHeight,
                region.extentDepth,
            },
        };
        vkRegions.push_back(vkRegion);
    }

    vkCmdCopyImage(
        _commandBuffer,
        srcImage->getHandle().as<VkImage>(),
        EImageLayout::toVk(srcImageLayout),
        dstImage->getHandle().as<VkImage>(),
        EImageLayout::toVk(dstImageLayout),
        static_cast<uint32_t>(vkRegions.size()),
        vkRegions.data());
}

void VulkanCommandBuffer::executeTransitionImageLayout(IImage* image, EImageLayout::T oldLayout, EImageLayout::T newLayout,
                                                       const ImageSubresourceRange* subresourceRange)
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

void VulkanCommandBuffer::validateTrackedOldLayout(
    IImage*                      image,
    EImageLayout::T              oldLayout,
    const ImageSubresourceRange* subresourceRange)
{
    if (!image) {
        return;
    }

    const auto mismatches = _resourceStateTracker.validateLayout(*image, oldLayout, subresourceRange);
    YA_CORE_ASSERT(
        mismatches.empty(),
        "Tracked image layout mismatch before explicit transition on image {}: expected {}, actual {} for {}",
        reinterpret_cast<uintptr_t>(image),
        toDebugString(oldLayout),
        mismatches.empty() ? "n/a" : toDebugString(mismatches.front().actualLayout),
        mismatches.empty() ? std::string{"unknown-range"} : formatSubresourceRange(&mismatches.front().range));
}

void VulkanCommandBuffer::executeTrackedTransition(IImage* image, EImageLayout::T newLayout,
                                                   const ImageSubresourceRange* subresourceRange)
{
    if (!image) {
        return;
    }
    for (const auto& transition : _resourceStateTracker.transition(*image, newLayout, subresourceRange)) {
        executeTransitionImageLayout(transition.image, transition.oldState.layout, transition.newState.layout, &transition.range);
    }
}

#if YA_CMDBUF_RECORD_MODE
void VulkanCommandBuffer::executeAll()
{
    for (const auto& cmd : recordedCommands) {
        std::visit(
            [&](auto&& arg) {
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
                else if constexpr (std::is_same_v<T, RenderCommand::DrawIndirect>) {
                    executeDrawIndirect(arg.buffer, arg.offset, arg.drawCount, arg.stride);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::DrawIndexedIndirect>) {
                    executeDrawIndexedIndirect(arg.buffer, arg.offset, arg.drawCount, arg.stride);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::DrawIndexedIndirectCount>) {
                    executeDrawIndexedIndirectCount(arg.drawBuffer, arg.drawOffset, arg.countBuffer, arg.countOffset, arg.maxDrawCount, arg.stride);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::FillBuffer>) {
                    executeFillBuffer(arg.buffer, arg.offset, arg.size, arg.value);
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
                    endRendering({});
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
                else if constexpr (std::is_same_v<T, RenderCommand::BindComputePipeline>) {
                    executeBindComputePipeline(arg.pipeline);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::Dispatch>) {
                    executeDispatch(arg.groupCountX, arg.groupCountY, arg.groupCountZ);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::DispatchIndirect>) {
                    executeDispatchIndirect(arg.buffer, arg.offset);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BufferMemoryBarrier>) {
                    executeBufferMemoryBarrier(arg.buffer, arg.srcStage, arg.dstStage, arg.srcAccess, arg.dstAccess, arg.offset, arg.size);
                }
            },
            cmd.data);
    }
    ICommandBuffer::executeAll();
}
#endif


// ========== Virtual Mode: Direct vkCmd* implementations ==========

void VulkanCommandBuffer::bindPipeline(IGraphicsPipeline* pipeline)
{
    executeBindPipeline(pipeline);
}

void VulkanCommandBuffer::bindComputePipeline(IComputePipeline* pipeline)
{
    executeBindComputePipeline(pipeline);
}

void VulkanCommandBuffer::bindVertexBuffer(uint32_t binding, const IBuffer* buffer, uint64_t offset)
{
    executeBindVertexBuffer(binding, buffer, offset);
}

void VulkanCommandBuffer::bindIndexBuffer(IBuffer* buffer, uint64_t offset, bool use16BitIndices)
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

void VulkanCommandBuffer::drawIndirect(IBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
{
    executeDrawIndirect(buffer, offset, drawCount, stride);
}

void VulkanCommandBuffer::drawIndexedIndirect(IBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
{
    executeDrawIndexedIndirect(buffer, offset, drawCount, stride);
}

void VulkanCommandBuffer::drawIndexedIndirectCount(IBuffer* drawBuffer, uint64_t drawOffset,
                                                    IBuffer* countBuffer, uint64_t countOffset,
                                                    uint32_t maxDrawCount, uint32_t stride)
{
    executeDrawIndexedIndirectCount(drawBuffer, drawOffset, countBuffer, countOffset, maxDrawCount, stride);
}

void VulkanCommandBuffer::fillBuffer(IBuffer* buffer, uint64_t offset, uint64_t size, uint32_t value)
{
    executeFillBuffer(buffer, offset, size, value);
}

void VulkanCommandBuffer::bufferMemoryBarrier(IBuffer* buffer,
                                              EPipelineStage::T srcStage,
                                              EPipelineStage::T dstStage,
                                              EResourceAccess::T srcAccess,
                                              EResourceAccess::T dstAccess,
                                              uint64_t offset,
                                              uint64_t size)
{
    executeBufferMemoryBarrier(buffer, srcStage, dstStage, srcAccess, dstAccess, offset, size);
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

void VulkanCommandBuffer::executeSetDepthBias(float constantFactor, float clamp, float slopeFactor)
{
    vkCmdSetDepthBias(_commandBuffer, constantFactor, clamp, slopeFactor);
}

void VulkanCommandBuffer::setDepthBias(float constantFactor, float clamp, float slopeFactor)
{
    executeSetDepthBias(constantFactor, clamp, slopeFactor);
}

void VulkanCommandBuffer::beginRendering(const RenderingInfo& info)
{
    const char* defaultRenderLabel = "Rendering";
    const char* labelName          = defaultRenderLabel;
    if (!info.label.empty() && info.label != "None") {
        labelName = info.label.c_str();
    }
    debugBeginLabel(labelName);
    beginDynamicRenderingFromManualImages(info);
}

VkRenderingAttachmentInfo* VulkanCommandBuffer::buildDepthAttachmentInfo(const RenderAttachment*    attachment,
                                                                         VkRenderingAttachmentInfo& outDepthAttach)
{
    if (attachment == nullptr) {
        return nullptr;
    }

    outDepthAttach = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext       = nullptr,
        .imageView   = attachment->imageView->getHandle().as<VkImageView>(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .loadOp      = EAttachmentLoadOp::toVk(attachment->loadOp),
        .storeOp     = EAttachmentStoreOp::toVk(attachment->storeOp),
        .clearValue  = {},
    };

    if (attachment->clearValue.isDepthStencil) {
        outDepthAttach.clearValue.depthStencil = {
            .depth   = attachment->clearValue.depthStencil.depth,
            .stencil = attachment->clearValue.depthStencil.stencil,
        };
    }

    return &outDepthAttach;
}

void VulkanCommandBuffer::executeDynamicRendering(std::vector<VkRenderingAttachmentInfo>& colorAttachments,
                                                  VkRenderingAttachmentInfo*              pDepthAttach,
                                                  VkRenderingAttachmentInfo*              pStencilAttach,
                                                  const VkRect2D&                         renderArea,
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
        .pStencilAttachment   = pStencilAttach,
    };

    s_vkCmdBeginRenderingKHR(_commandBuffer, &vkRenderingInfo);
}

void VulkanCommandBuffer::beginDynamicRenderingFromManualImages(const RenderingInfo& info)
{
    _currentRenderingMode = ERenderingMode::DynamicRendering;

    if (s_vkCmdBeginRenderingKHR == nullptr) {
        YA_CORE_WARN("vkCmdBeginRenderingKHR not available - VK_KHR_dynamic_rendering may not be enabled");
        return;
    }

    // Initial layout transitions for manual images (mirrors RT branch).
    // RenderGraph-managed passes pre-plan transitions externally and must not
    // compete with the command buffer's local tracker here.
    for (size_t i = 0; i < info.attachments.colors.size(); ++i) {
        const auto& attachment = info.attachments.colors[i];
        validateRenderAttachment(attachment, info.label, std::format("color[{}]", i));
    }
    if (info.attachments.depth.has_value()) {
        validateRenderAttachment(*info.attachments.depth, info.label, "depth");
    }

    if (!info.bExternalTransitionManagement) {
        std::vector<VulkanImage::LayoutTransition> transitions;
        for (auto& attachment : info.attachments.colors) {
            if (attachment.initialLayout != EImageLayout::Undefined && attachment.image) {
                if (auto* vkImg = dynamic_cast<VulkanImage*>(attachment.image)) {
                    VulkanImage::LayoutTransition transition{vkImg, attachment.initialLayout};
                    if (tryResolveRenderAttachmentSubresourceRange(attachment, transition.range)) {
                        transition.useRange = true;
                    }
                    transitions.push_back(transition);
                }
            }

            if (attachment.initialLayout != EImageLayout::Undefined && attachment.resolveImage) {
                if (auto* vkImg = dynamic_cast<VulkanImage*>(attachment.resolveImage)) {
                    transitions.push_back(VulkanImage::LayoutTransition{vkImg, attachment.initialLayout});
                }
            }
        }
        if (info.attachments.depth) {
            auto& attachment   = *info.attachments.depth;
            auto  targetLayout = attachment.initialLayout;
            if (targetLayout == EImageLayout::Undefined) {
                targetLayout = EImageLayout::DepthStencilAttachmentOptimal;
            }
            if (attachment.image) {
                if (auto* vkImg = dynamic_cast<VulkanImage*>(attachment.image)) {
                    VulkanImage::LayoutTransition transition{vkImg, targetLayout};
                    if (tryResolveRenderAttachmentSubresourceRange(attachment, transition.range)) {
                        transition.useRange = true;
                    }
                    transitions.push_back(transition);
                }
            }
        }
        if (!transitions.empty()) {
            for (const auto& transition : transitions) {
                executeTrackedTransition(transition.image, transition.newLayout, transition.useRange ? &transition.range : nullptr);
            }
        }
    }

    // Build color attachments from manual images
    std::vector<VkRenderingAttachmentInfo> vkColorAttachments;
    vkColorAttachments.reserve(info.attachments.colors.size());

    for (size_t i = 0; i < info.attachments.colors.size(); ++i) {
        const auto& attachment = info.attachments.colors[i];
        VkRenderingAttachmentInfo vkAttach{
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext       = nullptr,
            .imageView   = attachment.imageView->getHandle().as<VkImageView>(),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = toVkResolveMode(attachment.resolveMode),
            .resolveImageView = attachment.resolveImageView
                ? attachment.resolveImageView->getHandle().as<VkImageView>()
                : VK_NULL_HANDLE,
            .resolveImageLayout = attachment.resolveImageView
                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp      = EAttachmentLoadOp::toVk(attachment.loadOp),
            .storeOp     = EAttachmentStoreOp::toVk(attachment.storeOp),
            .clearValue  = {
                 .color = {{
                    attachment.clearValue.color.r,
                    attachment.clearValue.color.g,
                    attachment.clearValue.color.b,
                    attachment.clearValue.color.a,
                }},
            },
        };
        vkColorAttachments.push_back(vkAttach);
    }

    // Build depth attachment using shared helper
    VkRenderingAttachmentInfo  vkDepthAttach{};
    VkRenderingAttachmentInfo* pVkDepthAttach = buildDepthAttachmentInfo(
        info.attachments.depth ? &*info.attachments.depth : nullptr,
        vkDepthAttach);

    // Stencil is currently not modelled explicitly for manual dynamic rendering either.
    VkRenderingAttachmentInfo* pVkStencilAttach = nullptr;

    // Execute dynamic rendering
    VkRect2D renderArea = {
        .offset = {static_cast<int32_t>(info.attachments.renderArea.pos.x), static_cast<int32_t>(info.attachments.renderArea.pos.y)},
        .extent = {static_cast<uint32_t>(info.attachments.renderArea.extent.x), static_cast<uint32_t>(info.attachments.renderArea.extent.y)},
    };
    executeDynamicRendering(vkColorAttachments, pVkDepthAttach, pVkStencilAttach, renderArea, info.attachments.layerCount);
}

void VulkanCommandBuffer::endRendering(const RenderingInfo& info)
{
    executeEndRendering(info);
    debugEndLabel();
}

void VulkanCommandBuffer::bindDescriptorSets(
    IPipelineLayout*                        pipelineLayout,
    uint32_t                                firstSet,
    const std::vector<DescriptorSetHandle>& descriptorSets,
    const std::vector<uint32_t>&            dynamicOffsets)
{
    executeBindDescriptorSets(pipelineLayout, firstSet, descriptorSets, dynamicOffsets, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

void VulkanCommandBuffer::bindComputeDescriptorSets(
    IPipelineLayout*                        pipelineLayout,
    uint32_t                                firstSet,
    const std::vector<DescriptorSetHandle>& descriptorSets,
    const std::vector<uint32_t>&            dynamicOffsets)
{
    executeBindDescriptorSets(pipelineLayout, firstSet, descriptorSets, dynamicOffsets, VK_PIPELINE_BIND_POINT_COMPUTE);
}

void VulkanCommandBuffer::pushConstants(
    IPipelineLayout* pipelineLayout,
    EShaderStage::T  stages,
    uint32_t         offset,
    uint32_t         size,
    const void*      data)
{
    executePushConstants(pipelineLayout, stages, offset, size, data);
}

void VulkanCommandBuffer::copyBuffer(IBuffer* src, IBuffer* dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset)
{
    executeCopyBuffer(src, dst, size, srcOffset, dstOffset);
}

void VulkanCommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    executeDispatch(groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandBuffer::dispatchIndirect(IBuffer* buffer, uint64_t offset)
{
    executeDispatchIndirect(buffer, offset);
}

void VulkanCommandBuffer::transitionImageLayout(
    IImage*                      image,
    EImageLayout::T              oldLayout,
    EImageLayout::T              newLayout,
    const ImageSubresourceRange* subresourceRange)
{
    validateTrackedOldLayout(image, oldLayout, subresourceRange);
    executeTransitionImageLayout(image, oldLayout, newLayout, subresourceRange);
    if (image) {
        _resourceStateTracker.setState(*image, inferTrackedImageState(newLayout), subresourceRange);
    }
}

void VulkanCommandBuffer::transitionImageLayoutAuto(IImage* image, EImageLayout::T newLayout, const ImageSubresourceRange* subresourceRange)
{
    executeTrackedTransition(image, newLayout, subresourceRange);
}

void VulkanCommandBuffer::debugBeginLabel(const char* labelName, const float* colorRGBA)
{
    if (!s_vkCmdBeginDebugUtilsLabelEXT) {
        return;
    }

    VkDebugUtilsLabelEXT label{
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = nullptr,
        .pLabelName = labelName,
    };
    if (colorRGBA) {
        std::memcpy(label.color, colorRGBA, sizeof(label.color));
    }
    s_vkCmdBeginDebugUtilsLabelEXT(_commandBuffer, &label);
    ++_debugLabelDepth;
}

void VulkanCommandBuffer::debugEndLabel()
{
    if (!s_vkCmdEndDebugUtilsLabelEXT) {
        YA_CORE_WARN("vkCmdEndDebugUtilsLabelEXT not available");
        return;
    }

    if (_debugLabelDepth == 0) {
        YA_CORE_ERROR("debugEndLabel called without a matching debugBeginLabel on command buffer {}", (uintptr_t)_commandBuffer);
        return;
    }

    s_vkCmdEndDebugUtilsLabelEXT(_commandBuffer);
    --_debugLabelDepth;
}

} // namespace ya
