#include "GUI/Widgets/TextRaster.h"

#include "Render/Resources/FontManager.h"

#include <cmath>

namespace ya
{

FTextRasterPlan planTextRaster(float logicalFontPx, glm::vec2 uiScale, float dpiScale, glm::vec2 renderScale)
{
    FTextRasterPlan plan;
    const float     dpi = dpiScale > 0.0f ? dpiScale : 1.0f;
    plan.deviceScale    = uiScale * dpi * renderScale;

    const float logical = logicalFontPx > 1.0f ? logicalFontPx : 1.0f;
    const int   devicePx = static_cast<int>(std::lround(logical * plan.deviceScale.y));
    if (devicePx <= static_cast<int>(kBitmapMaxSize)) {
        const int floored = devicePx > static_cast<int>(kMinBitmapRasterPx)
                                ? devicePx
                                : static_cast<int>(kMinBitmapRasterPx);
        plan.rasterPx         = static_cast<uint32_t>(floored);
        plan.residual         = {1.0f, 1.0f};
        plan.bBitmapOneToOne  = true;
        return plan;
    }

    plan.rasterPx        = static_cast<uint32_t>(devicePx);
    plan.residual        = {1.0f, 1.0f};
    plan.bBitmapOneToOne = false;
    return plan;
}

} // namespace ya
