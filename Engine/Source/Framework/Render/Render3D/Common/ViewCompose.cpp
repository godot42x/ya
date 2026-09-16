#include "Render3D/Common/ViewCompose.h"

#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "Render2D/Render2D.h"

#include <span>

namespace ya
{

namespace
{

void recordViewDisplayInsets(std::span<const ViewDisplayInsetImage> insets)
{
    for (const auto& inset : insets) {
        if (!inset.texture || inset.destRect.extent.x <= 0.0f || inset.destRect.extent.y <= 0.0f) {
            continue;
        }

        constexpr float kBorder = 2.0f;
        Render2D::makeSprite(glm::vec3(inset.destRect.pos.x - kBorder, inset.destRect.pos.y - kBorder, 0.0f),
                             inset.destRect.extent + glm::vec2(kBorder * 2.0f),
                             nullptr,
                             glm::vec4(0.02f, 0.03f, 0.04f, 0.92f));
        Render2D::makeSprite(glm::vec3(inset.destRect.pos, 0.0f),
                             inset.destRect.extent,
                             inset.texture.get(),
                             glm::vec4(1.0f),
                             {1.0f, 1.0f},
                             {0.0f, 0.0f},
                             true);
    }
}

} // namespace

YA_RENDER_3D_API void recordCameraViewCompose(ICommandBuffer*                        cmdBuf,
                                              RenderTexture*                         cameraDisplayRT,
                                              const CameraFrameInput&                camera,
                                              const ViewComposeInput&                viewCompose,
                                              std::span<const ViewDisplayInsetImage> insets)
{
    if (!cmdBuf) {
        return;
    }

    // UI + extra View insets: after graphics/post so Game UI never enters
    // bloom or tonemapping. Target is the Camera WorldView display image.
    const bool bHasInsets = !insets.empty();
    if (cameraDisplayRT && (camera.uiFrameSnapshot || bHasInsets)) {
        recordRender2DComposePass(cmdBuf,
                                  *cameraDisplayRT,
                                  nullptr,
                                  camera.uiFrameSnapshot,
                                  FRender2DComposePassDesc{
                                      .kind                  = ERender2DComposePassKind::RuntimeUIComposite,
                                      .logicalViewportExtent = Extent2D{
                                          .width  = static_cast<uint32_t>(camera.viewportRect.extent.x),
                                          .height = static_cast<uint32_t>(camera.viewportRect.extent.y),
                                      },
                                  },
                                  [&]() { recordViewDisplayInsets(insets); });
    }

    // Editor gizmos / viewport compose still write a Camera or PreviewTarget RT.
    if (viewCompose.recordCompose) {
        viewCompose.recordCompose(cmdBuf);
    }
}

} // namespace ya
