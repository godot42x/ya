#pragma once

#include "Core/Common/Types.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"

#include <functional>
#include <memory>

namespace ya
{

struct ICommandBuffer;
struct Node;
struct RenderImage;

/// Shared 2D compose pass used by every viewport-facing UI/overlay path:
/// runtime game UI composite, editor 2D canvas preview, and the editor 3D
/// viewport composition (scene color + overlay + debug lines).
enum class ERender2DComposePassKind : uint8_t
{
    RuntimeUIComposite = 0,
    EditorCanvasPreview,
    EditorViewportCompose,
};

struct FRender2DComposePassDesc
{
    ERender2DComposePassKind kind = ERender2DComposePassKind::RuntimeUIComposite;
    Extent2D                 logicalViewportExtent{};
    glm::vec2                canvasPan  = glm::vec2(0.0f);
    float                    canvasZoom = 1.0f;

    /// EditorViewportCompose: full-screen scene color sampled as a sprite.
    std::shared_ptr<Texture> sceneSourceTexture = nullptr;

    /// EditorViewportCompose: camera used by world-space content (debug
    /// lines). UI-only passes leave this at identity.
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
void prepareRender2DComposePassPipeline(const FRender2DComposePassDesc& passDesc,
                                        EFormat::T                      colorFormat,
                                        EFormat::T                      depthFormat = EFormat::Undefined);

/// Record one shared 2D compose pass into `target`. `depthTarget` is optional
/// (the editor viewport depth-tests debug overlays against it). `extraContent`
/// runs inside the Render2D recording window for caller-owned content such as
/// camera overlay text and physics debug lines.
void recordRender2DComposePass(ICommandBuffer*                  cmdBuf,
                               RenderImage&                     target,
                               RenderImage*                     depthTarget,
                               Node*                            uiSceneRoot,
                               const FRender2DComposePassDesc&  passDesc,
                               const std::function<void()>&     extraContent = {});

} // namespace ya
