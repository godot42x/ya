#pragma once

#include "Core/Base.h"
#include "Render3D/Common/RenderFrameInputs.h"

#include <glm/glm.hpp>
#include <memory>

namespace ya
{

struct ICommandBuffer;
struct RenderTexture;

/// Place a dest rect in the host display RT (origin at the RT top-left).
/// Host layout policy calls this; it is not a View identity or output extent.
[[nodiscard]] YA_RENDER_3D_API Rect2D makeViewDisplayInsetRect(const glm::vec2& hostExtent,
                                                               float            widthFraction  = 0.22f,
                                                               float            marginFraction = 0.02f);

/// Game UI onto this Camera's offscreen display RT. Not display compose: must
/// not write `swapchain[imageIndex]`, acquire, or present. `cameraDisplayRT` is
/// `getViewDisplayImageShared()` (post or raw WorldView color), and may
/// be null on the first frame before the world graph creates it.
YA_RENDER_3D_API void recordCameraViewCompose(ICommandBuffer*        cmdBuf,
                                              RenderTexture*         cameraDisplayRT,
                                              const UIFrameSnapshot* uiFrameSnapshot,
                                              Extent2D               logicalViewExtent);

} // namespace ya
