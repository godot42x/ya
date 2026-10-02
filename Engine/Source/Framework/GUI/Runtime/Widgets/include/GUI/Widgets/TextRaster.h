#pragma once

#include "Core/Base.h"

#include <glm/vec2.hpp>

#include <cstdint>

namespace ya
{

/// Bitmap glyphs are never requested below this many device pixels.
/// One floor for every script: the font stack does not know the script when
/// the size is chosen. CJK at 9px is tight; FreeType autohint still keeps
/// ink (font-rendering skill). Below the floor the glyphs stay at this size
/// and may overflow or clip.
inline constexpr uint32_t kMinBitmapRasterPx = 9;

/// How to draw one run of text at a given logical size and device scale.
/// `rasterPx` is the font size to request. `residual` is the scale handed to
/// `ScreenDrawList::makeText`. Bitmap runs use residual {1, 1}: the atlas is
/// already the device-pixel size, and resampling it drops strokes.
struct FTextRasterPlan
{
    /// uiScale * dpiScale * renderScale. Geometry uses this. It is also the
    /// draw scale when no device-size font can be fetched and the face is
    /// not a texel-pinned bitmap.
    glm::vec2 deviceScale{1.0f, 1.0f};
    /// Pixel size passed to FontManager. Whole device pixels.
    uint32_t rasterPx = 1;
    /// Scale for makeText. {1, 1} when the fetched font's metrics are already
    /// device pixels (bitmap and SDF).
    glm::vec2 residual{1.0f, 1.0f};
    /// True when `rasterPx` is a bitmap size. The quad must match the atlas
    /// 1:1, so residual stays {1, 1}.
    bool bBitmapOneToOne = true;
};

/// Single entry for UI text scale.
/// `uiScale` is the host zoom (designer wheel, user magnification).
/// `dpiScale` is the tree's logical-to-device ratio.
/// `renderScale` is the paint-time render transform.
/// The em size is the Y axis: glyphs stay square at the vertical device
/// size. Layout keeps measuring the logical font; this plan is draw-only.
[[nodiscard]] YA_GUI_API FTextRasterPlan planTextRaster(float     logicalFontPx,
                                                        glm::vec2 uiScale,
                                                        float     dpiScale,
                                                        glm::vec2 renderScale);

} // namespace ya
