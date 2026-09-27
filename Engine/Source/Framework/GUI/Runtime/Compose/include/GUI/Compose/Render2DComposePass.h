#pragma once

#include "Core/Common/Types.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "Render2D/ScreenDraw.h"

#include <functional>
#include <memory>

namespace ya
{

struct ICommandBuffer;
struct RenderTexture;

/// Shared screen compose pass: runtime UI presentation, offscreen parity, and
/// the editor 2D canvas preview. View overlays are not recorded here.
enum class ERender2DComposePassKind : uint8_t
{
    RuntimeUIComposite = 0,
    /// Same UI packet as RuntimeUIComposite, but clears and finishes as an
    /// owned offscreen target. Kept separate so a windowed target and an
    /// offscreen mirror can record in the same command buffer without sharing
    /// Render2D's per-pass vertex/descriptor resources.
    RuntimeUIOffscreen,
    EditorCanvasPreview,
    EditorToolSurface,
};

/// 2D canvas preview writes this target. It is not a scene image.
inline constexpr EFormat::T kEditorCanvasPreviewColorFormat = EFormat::R16G16B16A16_SFLOAT;

struct FRender2DComposePassDesc
{
    ERender2DComposePassKind kind = ERender2DComposePassKind::RuntimeUIComposite;
    Extent2D                 logicalExtent{};
    glm::vec2                canvasPan  = glm::vec2(0.0f);
    float                    canvasZoom = 1.0f;

    /// Layout the target is transitioned to after the pass. An offscreen
    /// target stays ShaderReadOnlyOptimal so it can be sampled later; a
    /// direct-to-swapchain presentation pass passes PresentSrcKHR (swapchain
    /// images are not created with SAMPLED usage, so the sampled layout would
    /// be invalid).
    EImageLayout::T finalLayout = EImageLayout::ShaderReadOnlyOptimal;
};

/// Prepare the screen PSO this recorder will bind. Must be called before
/// command recording begins.
YA_GUI_API void prepareRender2DComposePassPipeline(ScreenDrawRecorder& recorder,
                                                   EFormat::T          colorFormat,
                                                   EFormat::T          depthFormat = EFormat::Undefined);

/// Record one screen compose pass into `target`. `uiFrameSnapshot` is the
/// immutable per-frame Game UI packet (already resolved to render-target
/// pixels); command recording never touches the live widget tree. May be null
/// for a canvas that only draws its grid. `extraContent` appends more screen
/// draws before the list is recorded.
YA_GUI_API void recordRender2DComposePass(ICommandBuffer*                  cmdBuf,
                                          RenderTexture&                   target,
                                          const UIFrameSnapshot*           uiFrameSnapshot,
                                          const FRender2DComposePassDesc&  passDesc,
                                          ScreenDrawRecorder&              recorder,
                                          const std::function<void(ScreenDrawList&)>& extraContent = {});

/// Replay a UI snapshot into an already-open raster pass. Does not begin or
/// end rendering and does not transition the target. Used by the editor
/// shell, which draws into the presentation pass the same way ImGui used to.
/// `extraContent` runs inside the Render2D recording window after product
/// items (same slot as `recordRender2DComposePass`).
YA_GUI_API void replayUIFrameSnapshot(ICommandBuffer*                cmdBuf,
                                      const UIFrameSnapshot&         snapshot,
                                      Extent2D                       targetExtent,
                                      EFormat::T                     colorFormat,
                                      ScreenDrawRecorder&            recorder,
                                      const std::function<void(ScreenDrawList&)>& extraContent = {});

} // namespace ya
