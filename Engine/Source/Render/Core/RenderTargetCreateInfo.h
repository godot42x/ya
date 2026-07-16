#pragma once

#include "Render/RenderDefines.h"

#include <optional>
#include <string>
#include <vector>

namespace ya
{

struct IRenderPass;

/**
 * @brief Configuration for creating a RenderTarget
 * Supports multiple color attachments and both RenderPass/Dynamic Rendering modes
 */
struct RenderTargetCreateInfo
{
    std::string       label                         = "RenderTarget";
    ERenderingMode::T renderingMode                 = ERenderingMode::DynamicRendering;
    bool              bSwapChainTarget              = false; // If true, use swapchain images instead of creating our own
    int32_t           swapChianColorAttachmentIndex = 0;

    Extent2D extent           = {.width = 800, .height = 600};
    uint32_t frameBufferCount = 1; // for custom render targets
    uint32_t layerCount       = 1; // for array textures or cubemaps

    [[deprecated("Unimplemented")]] uint32_t mipLevels = 1; // Number of mip levels for attachments

    struct AttachmentSpec
    {
        std::vector<AttachmentDescription>   colorAttach   = {}; // Support multiple color attachments
        std::optional<AttachmentDescription> depthAttach   = {}; // Undefined = no depth
        std::optional<AttachmentDescription> resolveAttach = {}; // Optional resolve attachment
    };

    struct RenderPassSpec
    {
        IRenderPass* renderPass = nullptr;
        uint32_t     index      = 0;
    };
    AttachmentSpec attachments = {};
    RenderPassSpec subpass     = {};
};

} // namespace ya
