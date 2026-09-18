#include "Render3D/Common/ViewCompose.h"

#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "Render2D/Render2D.h"

#include <algorithm>
#include <span>

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
                                              const UIFrameSnapshot*                 uiFrameSnapshot,
                                              Extent2D                               logicalViewportExtent,
                                              std::span<const ViewDisplayInsetImage> insets)
{
    if (!cmdBuf) {
        return;
    }

    // UI + extra View insets: after graphics/post so Game UI never enters
    // bloom or tonemapping. Target is the Camera WorldView display image.
    const bool bHasInsets = !insets.empty();
    if (cameraDisplayRT && (uiFrameSnapshot || bHasInsets)) {
        recordRender2DComposePass(cmdBuf,
                                  *cameraDisplayRT,
                                  nullptr,
                                  uiFrameSnapshot,
                                  FRender2DComposePassDesc{
                                      .kind                  = ERender2DComposePassKind::RuntimeUIComposite,
                                      .logicalViewportExtent = logicalViewportExtent,
                                  },
                                  [&]() { recordViewDisplayInsets(insets); });
    }

}

} // namespace ya
