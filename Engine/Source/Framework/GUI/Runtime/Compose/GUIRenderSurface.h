#pragma once

#include "Core/Api.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/RenderTexture.h"

#include <memory>
#include <string>

namespace ya
{

struct ICommandBuffer;
struct IRenderResourceFactory;

/// Description for a GUI-owned offscreen compose target. The surface owns its
/// RenderTexture; callers must recreate it only at a frame boundary after any
/// command buffer that retained the old image has completed.
struct FGUIRenderSurfaceDesc
{
    std::string        label;
    Extent2D           extent{};
    EFormat::T         colorFormat = EFormat::R8G8B8A8_UNORM;
    EImageUsage::T     usage = static_cast<EImageUsage::T>(
        EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc);
    EImageLayout::T    finalLayout = EImageLayout::ShaderReadOnlyOptimal;
};

/// A single GUI compose destination. It unifies an owned offscreen image and
/// an externally-owned image (such as a swapchain import) behind the same
/// prepare/record contract; it never pumps events, acquires swapchain images,
/// or presents.
class YA_GUI_API GUIRenderSurface final
{
private:
    std::shared_ptr<RenderTexture> _image;
    EImageLayout::T              _finalLayout = EImageLayout::Undefined;

public:
    /// Create an owned offscreen surface. Returns null for an empty extent or
    /// if the RHI factory cannot create its image/view.
    [[nodiscard]] static std::shared_ptr<GUIRenderSurface> createOffscreen(
        IRenderResourceFactory&          factory,
        const FGUIRenderSurfaceDesc&     desc);

    /// Wrap an image whose lifetime is owned by the caller. Used for imported
    /// swapchain images; the surface only retains it while recording.
    [[nodiscard]] static std::shared_ptr<GUIRenderSurface> wrapExternal(
        std::shared_ptr<RenderTexture> image,
        EImageLayout::T              finalLayout);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] const std::shared_ptr<RenderTexture>& getRenderImage() const { return _image; }
    [[nodiscard]] EImageLayout::T getFinalLayout() const { return _finalLayout; }

    /// Prepare the exact format variant that record() will use. Must run
    /// before command recording.
    void prepare(const FRender2DComposePassDesc& passDesc,
                 EFormat::T                     depthFormat = EFormat::Undefined) const;

    /// Record the shared 2D compose pass into this surface. The surface owns
    /// the final layout; callers cannot accidentally leave an offscreen
    /// target in PresentSrcKHR or a swapchain target in ShaderReadOnlyOptimal.
    void record(ICommandBuffer*                 cmdBuf,
                RenderTexture*                  depthTarget,
                const UIFrameSnapshot*          uiFrameSnapshot,
                FRender2DComposePassDesc        passDesc,
                const std::function<void()>&    extraContent = {}) const;

private:
    GUIRenderSurface(std::shared_ptr<RenderTexture> image, EImageLayout::T finalLayout);
};

} // namespace ya
