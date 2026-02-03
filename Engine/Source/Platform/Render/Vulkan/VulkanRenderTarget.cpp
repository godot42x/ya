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

namespace ya
{

VulkanRenderTarget::VulkanRenderTarget(const RenderTargetCreateInfo &ci)
{
    label             = ci.label;
    _colorFormat      = ci.colorFormat;
    _depthFormat      = ci.depthFormat;
    _samples          = ci.samples;
    _bSwapChainTarget = ci.bSwapChainTarget;
    _frameBufferCount = ci.frameBufferCount;
    _extent           = ci.extent;
    _renderPass       = ci.renderPass; // For legacy RenderPass API
    _renderingMode     = ci.renderPass ? ERenderingMode::RenderPass : ERenderingMode::DynamicRendering;

    // Get renderer reference
    _vkRender = App::get()->getRender()->as<VulkanRender>();

    // For swapchain targets, sync with swapchain
    if (_bSwapChainTarget) {
        auto swapchain    = _vkRender->getSwapchain();
        _extent           = swapchain->getExtent();
        _frameBufferCount = swapchain->getImageCount();
    }

    init();
}

VulkanRenderTarget::~VulkanRenderTarget()
{
    destroy();
}

void VulkanRenderTarget::init()
{
    createAttachments();

    // Create framebuffers if using legacy RenderPass API
    if (_renderPass) {
        createFrameBuffers();
    }
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

    // Destroy old attachments
    destroyFrameBuffers();
    destroyAttachments();

    // For swapchain targets, sync extent
    if (_bSwapChainTarget) {
        auto swapchain    = _vkRender->getSwapchain();
        _extent           = swapchain->getExtent();
        _frameBufferCount = swapchain->getImageCount();
    }

    // Create new attachments
    createAttachments();

    // Create framebuffers if using legacy RenderPass API
    if (_renderPass) {
        createFrameBuffers();
    }

    // Notify listeners that attachments have been recreated
    onAttachmentsRecreated.broadcast();
}

void VulkanRenderTarget::destroy()
{
    if (_vkRender) {
        _vkRender->waitIdle();
    }
    destroyFrameBuffers();
    destroyAttachments();
}

void VulkanRenderTarget::beginFrame()
{
    // Handle dirty flag - recreate if needed
    if (bDirty) {
        recreate();
        bDirty = false;
    }

    // Update frame index
    if (_bSwapChainTarget) {
        auto swapchain     = _vkRender->getSwapchain();
        _currentFrameIndex = swapchain->getCurImageIndex();
    }
    else {
        // For non-swapchain targets with multiple frames, cycle through
        if (_frameBufferCount > 1) {
            _currentFrameIndex = (_currentFrameIndex + 1) % _frameBufferCount;
        }
    }
}

void VulkanRenderTarget::createAttachments()
{
    _frameAttachments.resize(_frameBufferCount);

    auto swapchain   = _vkRender->getSwapchain();
    auto vkSwapchain = static_cast<VulkanSwapChain *>(swapchain);

    for (uint32_t i = 0; i < _frameBufferCount; ++i) {
        auto &frame = _frameAttachments[i];

        // ===== Color Attachment =====
        if (_bSwapChainTarget) {
            // Wrap swapchain image (we don't own it)
            frame.colorImage = VulkanImage::from(
                _vkRender,
                vkSwapchain->getVkImages()[i],
                toVk(_colorFormat),
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
            frame.ownsColorImage = false;
        }
        else {
            // Create our own color image
            ImageCreateInfo imageCI{
                .label  = std::format("{}_Color_{}", label, i),
                .format = _colorFormat,
                .extent = {
                    .width  = _extent.width,
                    .height = _extent.height,
                    .depth  = 1,
                },
                .mipLevels     = 1,
                .samples       = _samples,
                .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .sharingMode   = ESharingMode::Exclusive,
                .initialLayout = EImageLayout::Undefined,
            };
            frame.colorImage     = VulkanImage::create(_vkRender, imageCI);
            frame.ownsColorImage = true;
        }

        // Create color image view
        if (frame.colorImage) {
            frame.colorImageView = std::make_shared<VulkanImageView>(
                _vkRender,
                frame.colorImage.get(),
                VK_IMAGE_ASPECT_COLOR_BIT);
            frame.colorImageView->setDebugName(std::format("{}_ColorView_{}", label, i));
        }

        // ===== Depth Attachment (if requested) =====
        if (_depthFormat != EFormat::Undefined) {
            ImageCreateInfo depthCI{
                .label  = std::format("{}_Depth_{}", label, i),
                .format = _depthFormat,
                .extent = {
                    .width  = _extent.width,
                    .height = _extent.height,
                    .depth  = 1,
                },
                .mipLevels     = 1,
                .samples       = _samples,
                .usage         = EImageUsage::DepthStencilAttachment,
                .sharingMode   = ESharingMode::Exclusive,
                .initialLayout = EImageLayout::Undefined,
            };
            frame.depthImage = VulkanImage::create(_vkRender, depthCI);

            if (frame.depthImage) {
                VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
                // Add stencil aspect if format has stencil
                if (RenderHelper::isDepthStencilFormat(_depthFormat)) {
                    aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
                frame.depthImageView = std::make_shared<VulkanImageView>(
                    _vkRender,
                    frame.depthImage.get(),
                    aspectFlags);
                frame.depthImageView->setDebugName(std::format("{}_DepthView_{}", label, i));
            }
        }
    }

    YA_CORE_DEBUG("Created {} frame attachments for render target '{}'", _frameBufferCount, label);
}

void VulkanRenderTarget::destroyAttachments()
{
    for (auto &frame : _frameAttachments) {
        // Clear framebuffer first (it references the views)
        frame.frameBuffer.reset();

        // Clear views
        frame.colorImageView.reset();
        frame.depthImageView.reset();

        // Clear images (only if we own them)
        if (frame.ownsColorImage) {
            frame.colorImage.reset();
        }
        else {
            frame.colorImage.reset(); // Just release the shared_ptr, image is owned by swapchain
        }
        frame.depthImage.reset();
    }
    _frameAttachments.clear();
}

IImage *VulkanRenderTarget::getColorImage(uint32_t index) const
{
    if (_frameAttachments.empty() || _currentFrameIndex >= _frameAttachments.size()) {
        return nullptr;
    }
    return _frameAttachments[_currentFrameIndex].colorImage.get();
}

IImageView *VulkanRenderTarget::getColorImageView(uint32_t index) const
{
    if (_frameAttachments.empty() || _currentFrameIndex >= _frameAttachments.size()) {
        return nullptr;
    }
    return _frameAttachments[_currentFrameIndex].colorImageView.get();
}

IImage *VulkanRenderTarget::getDepthImage() const
{
    if (_frameAttachments.empty() || _currentFrameIndex >= _frameAttachments.size()) {
        return nullptr;
    }
    return _frameAttachments[_currentFrameIndex].depthImage.get();
}

IImageView *VulkanRenderTarget::getDepthImageView() const
{
    if (_frameAttachments.empty() || _currentFrameIndex >= _frameAttachments.size()) {
        return nullptr;
    }
    return _frameAttachments[_currentFrameIndex].depthImageView.get();
}

// ===== Legacy RenderPass API Support =====

void VulkanRenderTarget::createFrameBuffers()
{
    if (!_renderPass) {
        return;
    }

    YA_CORE_DEBUG("Creating {} VkFramebuffers for render target '{}'", _frameBufferCount, label);

    for (auto &frame : _frameAttachments) {
        // Collect attachments for framebuffer
        std::vector<std::shared_ptr<IImage>> attachments;
        if (frame.colorImage) {
            attachments.push_back(frame.colorImage);
        }
        if (frame.depthImage) {
            attachments.push_back(frame.depthImage);
        }

        // Create framebuffer
        auto vkRenderPass = dynamic_cast<VulkanRenderPass *>(_renderPass);
        frame.frameBuffer = std::make_shared<VulkanFrameBuffer>(
            _vkRender,
            vkRenderPass,
            _extent.width,
            _extent.height);

        // Recreate with attachments
        frame.frameBuffer->recreate(attachments, _extent.width, _extent.height);
    }
}

void VulkanRenderTarget::destroyFrameBuffers()
{
    for (auto &frame : _frameAttachments) {
        frame.frameBuffer.reset();
    }
}

IFrameBuffer *VulkanRenderTarget::getFrameBuffer() const
{
    if (!_renderPass || _frameAttachments.empty() || _currentFrameIndex >= _frameAttachments.size()) {
        return nullptr;
    }
    return _frameAttachments[_currentFrameIndex].frameBuffer.get();
}

void VulkanRenderTarget::onRenderGUI()
{
    ImGui::PushID(label.c_str());
    if (ImGui::CollapsingHeader(label.c_str())) {
        ImGui::Indent();

        ImGui::Text("Extent: %u x %u", _extent.width, _extent.height);
        ImGui::Text("Color Format: %d", static_cast<int>(_colorFormat));
        ImGui::Text("Depth Format: %d", static_cast<int>(_depthFormat));
        ImGui::Text("Frame Count: %u", _frameBufferCount);
        ImGui::Text("Current Frame: %u", _currentFrameIndex);
        ImGui::Text("Is Swapchain Target: %s", _bSwapChainTarget ? "Yes" : "No");
        ImGui::Text("Has FrameBuffer: %s", _renderPass ? "Yes (Legacy RenderPass)" : "No (Dynamic Rendering)");
        ImGui::Text("Dirty: %s", bDirty ? "Yes" : "No");

        // Show image handles for debugging
        if (ImGui::TreeNode("Attachments")) {
            for (uint32_t i = 0; i < _frameAttachments.size(); ++i) {
                const auto &frame = _frameAttachments[i];
                ImGui::Text("Frame %u:", i);
                ImGui::Indent();
                if (frame.colorImage) {
                    ImGui::Text("  Color Image: %p", frame.colorImage->getHandle());
                }
                if (frame.colorImageView) {
                    ImGui::Text("  Color View: %p", frame.colorImageView->getHandle());
                }
                if (frame.depthImage) {
                    ImGui::Text("  Depth Image: %p", frame.depthImage->getHandle());
                }
                if (frame.depthImageView) {
                    ImGui::Text("  Depth View: %p", frame.depthImageView->getHandle());
                }
                if (frame.frameBuffer) {
                    ImGui::Text("  FrameBuffer: %p", frame.frameBuffer->getHandle());
                }
                ImGui::Unindent();
            }
            ImGui::TreePop();
        }

        ImGui::Unindent();
    }
    ImGui::PopID();
}

// ===== Factory function =====
std::shared_ptr<IRenderTarget> createRenderTarget(const RenderTargetCreateInfo &ci)
{
    return std::make_shared<VulkanRenderTarget>(ci);
}

} // namespace ya
