#include "Render3D/Common/ViewCompose.h"

#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/RenderTexture.h"

namespace ya
{

YA_RENDER_3D_API void recordCameraViewCompose(ICommandBuffer*         cmdBuf,
                                              RenderTexture*          cameraDisplayRT,
                                              const CameraFrameInput& camera,
                                              const ViewComposeInput& viewCompose)
{
    if (!cmdBuf) {
        return;
    }

    // UI pass: after graphics/post so Game UI never enters bloom or tonemapping.
    // Target is the Camera WorldView display image, not a swapchain import.
    if (camera.uiFrameSnapshot && cameraDisplayRT) {
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
                                  });
    }

    // Editor gizmos / viewport compose still write a Camera or PreviewTarget RT.
    if (viewCompose.recordCompose) {
        viewCompose.recordCompose(cmdBuf);
    }
}

} // namespace ya
