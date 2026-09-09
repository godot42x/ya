#pragma once

#include "Core/Base.h"
#include "Render3D/Common/RenderFrameInputs.h"

namespace ya
{

struct ICommandBuffer;
struct RenderTexture;

/// Game UI + editor overlay onto this Camera's offscreen display RT.
/// Not display compose: must not write `swapchain[imageIndex]`, acquire, or
/// present. `cameraDisplayRT` is `getViewportDisplayImageShared()` (post or
/// raw WorldView color), and may be null on the first frame before the
/// world graph creates it.
YA_RENDER_3D_API void recordCameraViewCompose(ICommandBuffer*          cmdBuf,
                                              RenderTexture*           cameraDisplayRT,
                                              const CameraFrameInput&  camera,
                                              const ViewComposeInput&  viewCompose);

} // namespace ya
