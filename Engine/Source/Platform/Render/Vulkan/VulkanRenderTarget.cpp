#include "VulkanRenderTarget.h"
#include "Core/App/App.h"
#include "imgui.h"

// Vulkan-specific includes
#include "VulkanFrameBuffer.h"
#include "VulkanImage.h"
#include "VulkanImageView.h"
#include "VulkanRender.h"
#include "VulkanRenderPass.h"
#include "VulkanSwapChain.h"

#include "utility.cc/string_utils.h"

namespace ya
{

VulkanRenderTarget::~VulkanRenderTarget()
{
    destroy();
}

void getAttachmentsFromRenderPass(const IRenderPass *pass, uint32_t subPassIndex, std::vector<AttachmentDescription> &colorAttachments, AttachmentDescription &depthAttachment)
{
    auto attachments = pass->getAttachmentDesc();
    auto subpass     = pass->getSubPass(subPassIndex);

    for (const auto &attachmentRef : subpass.colorAttachments) {
        colorAttachments.push_back(attachments.at(attachmentRef.ref));
    }
    if (subpass.depthAttachment.ref != -1) {
        depthAttachment = attachments.at(subpass.depthAttachment.ref);
    }
}


bool VulkanRenderTarget::onInit(const RenderTargetCreateInfo &ci)
{
    YA_CORE_ASSERT(ci.renderingMode != ERenderingMode::None, "Rendering mode must be specified");

    // Get renderer reference
    _vkRender         = App::get()->getRender()->as<VulkanRender>();
    _frameBufferCount = ci.frameBufferCount;
    if (bSwapChainTarget) {
        auto swapchain    = _vkRender->getSwapchain();
        _extent           = swapchain->getExtent();
        _frameBufferCount = swapchain->getImageCount();
    }


    // TODO: detect the renderpass ptr, remote the mode field
    if (_renderingMode == ERenderingMode::RenderPass) {

        _renderPass   = ci.subpass.renderPass;
        _subpassIndex = ci.subpass.index;

        YA_CORE_ASSERT(_renderPass != nullptr, "RenderPass mode requires a valid renderPass");
        YA_CORE_ASSERT(_renderPass->isValidSubpassIndex(_subpassIndex), "Invalid subpass index");

        getAttachmentsFromRenderPass(_renderPass,
                                     _subpassIndex,
                                     _colorAttachments,
                                     _depthAttachment);
    }
    else {
        _colorAttachments = ci.attachments.colorAttach;
        _depthAttachment  = ci.attachments.depthAttach;
    }

    YA_CORE_DEBUG("RenderTarget '{}': Extracted from RenderPass - {} color attachments, depth format: {}",
                  label,
                  _colorAttachments.size(),
                  static_cast<int>(_depthAttachment.format));
    return recreateImagesAndFrameBuffer(_frameBufferCount,
                                        _colorAttachments,
                                        _depthAttachment);
}

void VulkanRenderTarget::recreate()
{
    YA_CORE_INFO("Recreating VulkanRenderTarget '{}' with extent: {}x{}", label, _extent.width, _extent.height);

    if (_extent.width <= 0 || _extent.height <= 0) {
        YA_CORE_WARN("Invalid extent for render target '{}', skipping recreation", label);
        return;
    }

    // Wait for GPU to finish using old resources
    _vkRender->waitIdle();


    // For swapchain targets, sync extent
    if (bSwapChainTarget) {
        auto swapchain    = _vkRender->getSwapchain();
        _extent           = swapchain->getExtent();
        _frameBufferCount = swapchain->getImageCount();
    }

    recreateImagesAndFrameBuffer(_frameBufferCount,
                                 _colorAttachments,
                                 _depthAttachment);

    onFramebufferRecreated.broadcast();
}

void VulkanRenderTarget::destroy()
{
    _frameBuffers.clear();
    _vkRender->waitIdle();
}

void VulkanRenderTarget::beginFrame(ICommandBuffer *cmdBuf)
{
    // Handle dirty flag - recreate if needed
    if (bDirty) {
        recreate();
        bDirty = false;
    }

    if (bSwapChainTarget) {
        auto swapchain     = _vkRender->getSwapchain();
        _currentFrameIndex = swapchain->getCurImageIndex();
    }


    // For SwapChain targets in Dynamic Rendering mode, transition from PRESENT_SRC_KHR to ColorAttachmentOptimal

    _frameBuffers[_currentFrameIndex]->begin(cmdBuf);

    // if (_renderPass) {
    //     _renderPass->begin(cmdBuf,
    //                        _frameBuffers[_currentFrameIndex].get(),
    //                        _extent,
    //                        {});
    // }
}

void VulkanRenderTarget::endFrame(ICommandBuffer *cmdBuf)
{
    _frameBuffers[_currentFrameIndex]->end(cmdBuf);

    // For SwapChain targets in Dynamic Rendering mode, transition to PRESENT_SRC_KHR


    // Update frame index
    if (bSwapChainTarget) {
        // auto swapchain     = _vkRender->getSwapchain();
        // _currentFrameIndex = (_currentFrameIndex + 1) % swapchain->getImageCount();
    }
    else {
        // For non-swapchain targets with multiple frames, cycle through
        if (getFrameBufferCount() > 1) {
            _currentFrameIndex = (_currentFrameIndex + 1) % getFrameBufferCount();
        }
    }

    // if (_renderingMode == ERenderingMode::RenderPass) {
    //     _renderPass->end(cmdBuf);
    // }
}

bool VulkanRenderTarget::recreateImagesAndFrameBuffer(uint32_t                                  frameBufferCount,
                                                      const std::vector<AttachmentDescription> &colorAttachments,
                                                      const AttachmentDescription              &depthAttachment)
{

    auto swapchain = _vkRender->getSwapchain()->as<VulkanSwapChain>();


    const uint32_t colorAttachmentCount = colorAttachments.size();

    // Clear old framebuffers before creating new ones to avoid appending
    _frameBuffers.clear();



    for (uint32_t i = 0; i < frameBufferCount; ++i) {
        std::vector<stdptr<IImage>> colorImages = {};
        stdptr<IImage>              depthImage  = nullptr;
        colorImages.reserve(colorAttachmentCount);


        // ===== Color Attachments =====
        for (uint32_t attachIdx = 0; attachIdx < colorAttachmentCount; ++attachIdx)
        {
            if (bSwapChainTarget && attachIdx == (uint32_t)swapChianColorAttachmentIndex) {
                // For swapchain targets, only the first color attachment comes from swapchain

                YA_CORE_ASSERT(swapchain->getFormat() == colorAttachments[attachIdx].format, "Swapchain format must match color attachment format");

                auto image    = VulkanImage::from(_vkRender,
                                               swapchain->getVkImages()[i],
                                               swapchain->getSurfaceFormat(),
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
                image->bOwned = false; // manage by swapchain
                // cmdBuf->transitionImageLayout(image.get(), EImageLayout::PresentSrcKHR);
                colorImages.push_back(image);
            }
            else {
                const auto &colorAttachment = colorAttachments[attachIdx];
                // Create our own color image
                ImageCreateInfo imageCI{
                    .label  = std::format("{}_Color{}_{}", label, attachIdx, i),
                    .format = colorAttachment.format,
                    .extent = {
                        .width  = _extent.width,
                        .height = _extent.height,
                        .depth  = 1,
                    },
                    .mipLevels     = 1,
                    .samples       = colorAttachment.samples,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                    .sharingMode   = ESharingMode::Exclusive,
                    .initialLayout = EImageLayout::Undefined,
                };
                auto image    = VulkanImage::create(_vkRender, imageCI);
                image->bOwned = true;
                colorImages.push_back(image);
            }
        }

        // ===== Depth Attachment (if requested) =====
        if (depthAttachment.format != EFormat::Undefined) {
            ImageCreateInfo depthCI{
                .label  = std::format("{}_Depth_{}", label, i),
                .format = depthAttachment.format,
                .extent = {
                    .width  = _extent.width,
                    .height = _extent.height,
                    .depth  = 1,
                },
                .mipLevels     = 1,
                .samples       = depthAttachment.samples,
                .usage         = EImageUsage::DepthStencilAttachment,
                .sharingMode   = ESharingMode::Exclusive,
                .initialLayout = EImageLayout::Undefined,
            };
            depthImage = VulkanImage::create(_vkRender, depthCI);
        }



        FrameBufferCreateInfo fbCI{
            .label       = std::format("{}_FrameBuffer_{}", label, i),
            .width       = _extent.width,
            .height      = _extent.height,
            .colorImages = colorImages,
            .depthImage  = depthImage,
            .renderPass  = _renderPass,
        };
        auto fb = IFrameBuffer::create(_vkRender, fbCI);
        YA_CORE_ASSERT(fb != nullptr, "Failed to create framebuffer");
        _frameBuffers.push_back(fb);
    }

    YA_CORE_DEBUG("Created {} frame attachments ({} color + {} depth) for render target '{}'",
                  frameBufferCount,
                  colorAttachmentCount,
                  (depthAttachment.format != EFormat::Undefined ? 1 : 0),
                  label);
    return true;
}



void VulkanRenderTarget::onRenderGUI()
{
    ImGui::PushID(label.c_str());
    if (ImGui::CollapsingHeader(label.c_str())) {
        ImGui::Indent();

        ImGui::Text("Extent: %u x %u", _extent.width, _extent.height);
        ImGui::Text("Color Attachments: %zu", _colorAttachments.size());
        for (size_t i = 0; i < _colorAttachments.size(); ++i) {
            ImGui::Text("  Color[%zu] Format: %d", i, static_cast<int>(_colorAttachments[i].format));
        }
        ImGui::Text("Depth Format: %d", static_cast<int>(_depthAttachment.format));
        ImGui::Text("Frame Count: %u", _frameBufferCount);
        ImGui::Text("Current Frame: %u", _currentFrameIndex);
        ImGui::Text("Is Swapchain Target: %s", bSwapChainTarget ? "Yes" : "No");
        ImGui::Text("Has FrameBuffer: %s", _renderPass ? "Yes (Legacy RenderPass)" : "No (Dynamic Rendering)");
        ImGui::Text("Dirty: %s", bDirty ? "Yes" : "No");

        // Show image handles for debugging
        if (ImGui::TreeNode("Frame Buffers")) {
            for (uint32_t i = 0; i < _frameBuffers.size(); ++i) {
                const auto &frame = _frameBuffers[i];
                ImGui::Text("Frame %u:", i);

                ImGui::Text("  FrameBuffer: %p -> %p", frame.get(), frame->getHandle());
                ImGui::Indent();

                // Show all color attachments
                for (size_t colorIdx = 0; colorIdx < frame->_colorImages.size(); ++colorIdx) {
                    if (frame->_colorImages[colorIdx]) {
                        ImGui::Text("  Color[%zu] Image: %p", colorIdx, (void *)frame->_colorImages[colorIdx]->getHandle());
                    }
                    if (colorIdx < frame->_colorImageViews.size() && frame->_colorImageViews[colorIdx]) {
                        ImGui::Text("  Color[%zu] View: %p", colorIdx, (void *)frame->_colorImageViews[colorIdx]->getHandle());
                    }
                }

                if (frame->_depthImage) {
                    ImGui::Text("  Depth Image: %p", frame->_depthImage->getHandle());
                }
                if (frame->_depthImageView) {
                    ImGui::Text("  Depth View: %p", (void *)frame->_depthImageView->getHandle());
                }
                ImGui::Unindent();
            }
            ImGui::TreePop();
        }

        ImGui::Unindent();
    }
    ImGui::PopID();
}

IFrameBuffer *VulkanRenderTarget::getFrameBuffer(int32_t index) const
{
    if (index == -1) {
        return _frameBuffers[_currentFrameIndex].get();
    }
    return _frameBuffers[index].get();
}


} // namespace ya
