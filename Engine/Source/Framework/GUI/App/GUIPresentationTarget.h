#pragma once

// ============================================================================
// GUIPresentationTarget - one imported swapchain image (+ view) used as the
// direct presentation target of a standalone GUI app (gui-app-bootstrap
// Phase 1).
//
// The swapchain images are imported as renderable images with
// finalLayout = PresentSrcKHR and wrapped in a RenderImage so the compose
// pass consumes the same contract as every other UI target. Ownership stays
// with the GUI app host; rebuildPresentationResources() releases and
// recreates the whole set at frame boundaries after swapchain recreation.
// ============================================================================

#include "RHI/Core/RenderImage.h"

#include <memory>
#include <vector>

namespace ya
{

struct IRender;
struct VulkanSwapChain;

/// One imported swapchain image + default view.
struct GUIPresentationTarget final
{
    /// Rebuild `outTargets` from the current swapchain (clears it first).
    /// Called at startup and after every swapchain recreation (resize /
    /// restore); the caller must have waited for in-flight work first.
    static void buildAll(IRender&                                                 render,
                         VulkanSwapChain&                                         swapchain,
                         const char*                                              labelPrefix,
                         std::vector<std::shared_ptr<GUIPresentationTarget>>& outTargets);

    std::shared_ptr<RenderImage> renderImage;
};

} // namespace ya
