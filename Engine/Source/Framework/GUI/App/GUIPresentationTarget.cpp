#include "GUI/App/GUIPresentationTarget.h"

#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"

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
        auto renderImage = std::make_shared<RenderImage>();
        renderImage->label       = label;
        renderImage->image       = std::move(importedImage);
        renderImage->defaultView = std::move(imageView);
        outTargets.push_back(std::make_shared<GUIPresentationTarget>(GUIPresentationTarget{
            .renderSurface = GUIRenderSurface::wrapExternal(std::move(renderImage), EImageLayout::PresentSrcKHR),
        }));
    }
}

} // namespace ya
