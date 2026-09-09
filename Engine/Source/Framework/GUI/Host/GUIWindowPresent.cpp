#include "GUI/Host/GUIWindowPresent.h"

#include "Core/Log.h"
#include "GUI/Compose/GUIRenderSurface.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/PresentFrame.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "Render/Resources/FontManager.h"

#include <format>
#include <optional>

namespace ya
{

void rebuildGuiSurfacePresentation(FGUISurfacePresentResources& resources,
                                   const char*                  labelPrefix,
                                   bool                         bWaitForGpu)
{
    if (!resources.render || !resources.present) {
        return;
    }
    if (bWaitForGpu) {
        resources.present->waitInFlight();
    }
    resources.commandBuffers.clear();
    resources.presentationTargets.clear();

    ISwapchain* swapchainBase = resources.present->getSwapchain();
    auto*       swapchain     = swapchainBase ? swapchainBase->as<VulkanSwapChain>() : nullptr;
    if (!swapchain) {
        YA_CORE_ERROR("GUI extra present: swapchain is not VulkanSwapChain");
        return;
    }
    resources.render->allocateCommandBuffers(swapchain->getImageCount(), resources.commandBuffers);
    GUIPresentationTarget::buildAll(*resources.render,
                                    *swapchain,
                                    labelPrefix ? labelPrefix : "GUIExtra",
                                    resources.presentationTargets);
    resources.cachedSwapchainHandle = swapchain->getHandle();
    resources.cachedSwapchainExtent = swapchain->getExtent();
}

void presentGuiSnapshot(FGUISurfacePresentResources& resources,
                        const UIFrameSnapshot&       snapshot,
                        Extent2D                     logicalExtent,
                        Render2DPassSlot             passSlot,
                        bool                         bMinimized,
                        bool&                        bSwapchainRecreatePending)
{
    if (!resources.render || !resources.present) {
        return;
    }
    if (bSwapchainRecreatePending) {
        if (auto* swapchain = resources.present->getSwapchain()->as<VulkanSwapChain>()) {
            swapchain->requestRecreate();
        }
        bSwapchainRecreatePending = false;
    }
    if (bMinimized || !resources.present->isPresentable()) {
        return;
    }

    FPresentFrame presentFrame{.surface = resources.present};
    if (!acquirePresentFrame(presentFrame)) {
        return;
    }
    if (!presentFrame.acquired()) {
        submitPresentFrame(presentFrame, {});
        return;
    }
    const int32_t imageIndex = presentFrame.imageIndex;

    ISwapchain* swapchainBase = resources.present->getSwapchain();
    auto*       swapchain     = swapchainBase ? swapchainBase->as<VulkanSwapChain>() : nullptr;
    if (!swapchain) {
        submitPresentFrame(presentFrame, {});
        return;
    }
    const Extent2D swapchainExtent = swapchain->getExtent();
    if (swapchain->getHandle() != resources.cachedSwapchainHandle ||
        swapchain->getImageCount() != resources.presentationTargets.size() ||
        swapchainExtent.width != resources.cachedSwapchainExtent.width ||
        swapchainExtent.height != resources.cachedSwapchainExtent.height) {
        rebuildGuiSurfacePresentation(resources, "GUIExtra", /*bWaitForGpu=*/false);
        swapchainBase = resources.present->getSwapchain();
        swapchain     = swapchainBase ? swapchainBase->as<VulkanSwapChain>() : nullptr;
        if (!swapchain) {
            submitPresentFrame(presentFrame, {});
            return;
        }
    }
    if (static_cast<size_t>(imageIndex) >= resources.presentationTargets.size() ||
        static_cast<size_t>(imageIndex) >= resources.commandBuffers.size()) {
        YA_CORE_ERROR("GUI extra present: image index {} out of range", imageIndex);
        submitPresentFrame(presentFrame, {});
        return;
    }

    const auto& presentation = resources.presentationTargets[static_cast<size_t>(imageIndex)];
    if (!presentation || !presentation->renderSurface || !presentation->renderSurface->isValid()) {
        YA_CORE_ERROR("GUI extra present: presentation surface {} is invalid", imageIndex);
        submitPresentFrame(presentFrame, {});
        return;
    }
    const auto& renderSurface = presentation->renderSurface;
    const auto& renderImage   = renderSurface->getRenderImage();
    const Extent2D presentExtent = renderImage->getExtent();
    renderSurface->prepare(FRender2DComposePassDesc{
        .kind     = ERender2DComposePassKind::RuntimeUIComposite,
        .passSlot = passSlot,
    });

    FontManager::get()->flushPendingGlyphs(*resources.render);
    (void)FontManager::get()->consumeNewGlyphCapture();

    auto cmdBuf = resources.commandBuffers[static_cast<size_t>(imageIndex)];
    cmdBuf->reset();
    cmdBuf->begin();
    cmdBuf->retireResource(renderImage->getImageShared());
    cmdBuf->retireResource(renderImage->getImageViewShared());
    cmdBuf->transitionImageLayoutAuto(renderImage->getImage(), EImageLayout::ColorAttachmentOptimal);
    cmdBuf->beginRendering(RenderingInfo{
        .label                         = "GUIExtra_Clear",
        .bExternalTransitionManagement = true,
        .attachments                   = RenderAttachmentSet{
            .renderArea = Rect2D{
                .pos    = {0.0f, 0.0f},
                .extent = {static_cast<float>(presentExtent.width), static_cast<float>(presentExtent.height)},
            },
            .layerCount = 1,
            .colors     = {
                RenderAttachment{
                    .image         = renderImage->getImage(),
                    .imageView     = renderImage->getImageView(),
                    .loadOp        = EAttachmentLoadOp::Clear,
                    .storeOp       = EAttachmentStoreOp::Store,
                    .clearValue    = ClearValue(0.05f, 0.06f, 0.07f, 1.0f),
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ColorAttachmentOptimal,
                },
            },
            .depth = std::nullopt,
        },
    });
    cmdBuf->endRendering();
    renderSurface->record(
        cmdBuf.get(),
        nullptr,
        &snapshot,
        FRender2DComposePassDesc{
            .kind                  = ERender2DComposePassKind::RuntimeUIComposite,
            .passSlot              = passSlot,
            .logicalViewportExtent = logicalExtent,
        });
    cmdBuf->end();
    submitPresentFrame(presentFrame, {cmdBuf->getHandle()});
}

} // namespace ya
