#include "GUI/Host/GUIPresentationTarget.h"

#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "RHI/Core/RenderTexture.h"

#include <format>

namespace ya
{

void GUIPresentationTarget::buildAll(IRender&                                          render,
                                     VulkanSwapChain&                                  swapchain,
                                     const char*                                       labelPrefix,
                                     std::vector<std::shared_ptr<GUIPresentationTarget>>& outTargets)
{
    outTargets.clear();
    outTargets.reserve(swapchain.getImageCount());
    for (uint32_t i = 0; i < swapchain.getImageCount(); ++i) {
        const std::string label = std::format("{}_Presentation_{}", labelPrefix, i);
        auto importedImage = render.getResourceFactory()->importImage(ImportedImageDesc{
            .label         = label,
            .nativeHandle  = static_cast<void*>(swapchain.getVkImages().at(i)),
            .format        = swapchain.getFormat(),
            .usage         = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::TransferSrc),
            .extent        = {.width = swapchain.getExtent().width, .height = swapchain.getExtent().height, .depth = 1},
            .initialLayout = EImageLayout::Undefined,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        });
        auto imageView = render.getResourceFactory()->createImageView(
            importedImage,
            ImageViewCreateInfo{
                .label          = std::format("{}_Presentation_{}_View", labelPrefix, i),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            });
        auto resource = std::make_shared<ImageResource>();
        resource->label       = label;
        resource->desc.image  = ImageCreateInfo{
            .label       = label,
            .format      = swapchain.getFormat(),
            .extent      = {.width = swapchain.getExtent().width, .height = swapchain.getExtent().height, .depth = 1},
            .mipLevels   = 1,
            .arrayLayers = 1,
            .samples     = ESampleCount::Sample_1,
            .usage       = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::TransferSrc),
            .initialLayout = EImageLayout::Undefined,
        };
        resource->desc.defaultView = ImageViewCreateInfo{
            .label          = std::format("{}_Presentation_{}_View", labelPrefix, i),
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        resource->image       = std::move(importedImage);
        resource->defaultView = std::move(imageView);
        outTargets.push_back(std::make_shared<GUIPresentationTarget>(GUIPresentationTarget{
            .renderSurface = GUIRenderSurface::wrapExternal(RenderTexture::adopt(std::move(resource)), EImageLayout::PresentSrcKHR),
        }));
    }
}

} // namespace ya
