#pragma once

#include "Core/Common/Types.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "Render2D/Render2D.h"

#include <functional>
#include <memory>

namespace ya
{

struct ICommandBuffer;
struct RenderTexture;

/// Shared 2D compose pass used by every viewport-facing UI/overlay path:
/// runtime UI presentation/offscreen parity, editor 2D canvas preview, and
/// the editor 3D viewport composition (scene color + overlay + debug lines).
enum class ERender2DComposePassKind : uint8_t
{
    RuntimeUIComposite = 0,
    /// Same UI packet as RuntimeUIComposite, but clears and finishes as an
    /// owned offscreen target. Kept separate so a windowed target and an
    /// offscreen mirror can record in the same command buffer without sharing
    /// Render2D's per-pass vertex/descriptor resources.
    RuntimeUIOffscreen,
    EditorCanvasPreview,
    EditorViewportCompose,
    EditorToolSurface,
};

/// Editor viewport compose / 2D canvas always write this HDR target. Pipeline
/// prep must use the same format; it is independent of Deferred vs Forward
/// color, but depth still follows the active 3D pipeline.
inline constexpr EFormat::T kEditorViewportComposeColorFormat = EFormat::R16G16B16A16_SFLOAT;

struct FRender2DComposePassDesc
{
    ERender2DComposePassKind kind = ERender2DComposePassKind::RuntimeUIComposite;
    /// Per-window slot. `kInvalidRender2DPassSlot` falls back to the process
    /// kind pool (single-window editor/runtime). Multi-window hosts must set
    /// a slot from `Render2D::acquirePassSlot()`.
    Render2DPassSlot         passSlot = kInvalidRender2DPassSlot;
    Extent2D                 logicalExtent{};
    glm::vec2                canvasPan  = glm::vec2(0.0f);
    float                    canvasZoom = 1.0f;

    /// Layout the target is transitioned to after the pass. Intermediate
    /// targets (world composite, editor preview) stay ShaderReadOnlyOptimal so
    /// they can be sampled later; a direct-to-swapchain presentation pass
    /// passes PresentSrcKHR (swapchain images are not created with SAMPLED
    /// usage, so the sampled layout would be invalid).
    EImageLayout::T finalLayout = EImageLayout::ShaderReadOnlyOptimal;

    /// EditorViewportCompose: full-screen scene color sampled as a sprite.
    std::shared_ptr<Texture> sceneSourceTexture = nullptr;

    /// EditorViewportCompose: camera used by world-space content (debug
    /// lines). Only `view` / `viewProjection` are forwarded to Render2D (it
    /// has no camera concept); `position` / `projection` stay local to this
    /// layer. UI-only passes leave these at identity.
    struct Camera
    {
        glm::vec3 position      = glm::vec3(0.0f);
        glm::mat4 view          = glm::mat4(1.0f);
        glm::mat4 projection    = glm::mat4(1.0f);
        glm::mat4 viewProjection = glm::mat4(1.0f);
    } camera;
};

/// Prepare the Render2D pipeline variant required by one shared compose pass.
/// Must be called before command recording begins.
YA_GUI_API void prepareRender2DComposePassPipeline(const FRender2DComposePassDesc& passDesc,
                                                   EFormat::T                      colorFormat,
                                                   EFormat::T                      depthFormat = EFormat::Undefined);

/// Record one shared 2D compose pass into `target`. `uiFrameSnapshot` is the
/// immutable per-frame Game UI packet (already resolved to render-target
/// pixels); command recording never touches the live widget tree. May be null
/// for passes without Game UI (editor viewport). `depthTarget` is optional
/// (the editor viewport depth-tests debug overlays against it).
/// `extraContent` runs inside the Render2D recording window for caller-owned
/// content such as camera overlay text and physics debug lines.
YA_GUI_API void recordRender2DComposePass(ICommandBuffer*                  cmdBuf,
                                          RenderTexture&                   target,
                                          RenderTexture*                   depthTarget,
                                          const UIFrameSnapshot*           uiFrameSnapshot,
                                          const FRender2DComposePassDesc&  passDesc,
                                          const std::function<void(Render2DList&)>& extraContent = {});

/// Replay a UI snapshot into an already-open raster pass. Does not begin or
/// end rendering and does not transition the target. Used by the editor
/// shell, which draws into the presentation pass the same way ImGui used to.
/// `extraContent` runs inside the Render2D recording window after product
/// items (same slot as `recordRender2DComposePass`).
YA_GUI_API void replayUIFrameSnapshot(ICommandBuffer*                cmdBuf,
                                      const UIFrameSnapshot&         snapshot,
                                      Extent2D                       targetExtent,
                                      ERender2DComposePassKind       kind,
                                      const std::function<void(Render2DList&)>& extraContent = {});

} // namespace ya
