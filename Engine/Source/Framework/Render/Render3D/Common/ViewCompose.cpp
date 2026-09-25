#include "Render3D/Common/ViewCompose.h"

#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "Render2D/Render2D.h"

#include <algorithm>

namespace ya
{

YA_RENDER_3D_API Rect2D makeViewDisplayInsetRect(const glm::vec2& hostExtent, float widthFraction, float marginFraction)
{
    if (hostExtent.x <= 1.0f || hostExtent.y <= 1.0f) {
        return {};
    }

    const float safeWidthFraction  = std::clamp(widthFraction, 0.05f, 0.5f);
    const float safeMarginFraction = std::clamp(marginFraction, 0.0f, 0.2f);
    const float margin             = std::max(hostExtent.x, hostExtent.y) * safeMarginFraction;
    float       width              = hostExtent.x * safeWidthFraction;
    float       height             = width * (hostExtent.y / hostExtent.x);
    if (height + margin * 2.0f > hostExtent.y) {
        height = std::max(1.0f, hostExtent.y - margin * 2.0f);
        width  = height * (hostExtent.x / hostExtent.y);
    }
    width  = std::max(1.0f, std::min(width, hostExtent.x - margin));
    height = std::max(1.0f, std::min(height, hostExtent.y - margin));

    return Rect2D{
        .pos    = {hostExtent.x - margin - width, hostExtent.y - margin - height},
        .extent = {width, height},
    };
}

YA_RENDER_3D_API void recordCameraViewCompose(ICommandBuffer*        cmdBuf,
                                              RenderTexture*         cameraDisplayRT,
                                              const UIFrameSnapshot* uiFrameSnapshot,
                                              Extent2D               logicalViewExtent)
{
    if (!cmdBuf) {
        return;
    }

    // Game UI: after graphics/post so it never enters bloom or tonemapping.
    // Target is the Camera WorldView display image.
    if (cameraDisplayRT && uiFrameSnapshot) {
        recordRender2DComposePass(cmdBuf,
                                  *cameraDisplayRT,
                                  nullptr,
                                  uiFrameSnapshot,
                                  FRender2DComposePassDesc{
                                      .kind                  = ERender2DComposePassKind::RuntimeUIComposite,
                                      .logicalExtent = logicalViewExtent,
                                  });
    }
}

} // namespace ya
