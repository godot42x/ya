#include "GUI/Widgets/Brush.h"

#include <algorithm>

namespace ya
{

namespace
{

void fitPair(float& a, float& b, float budget)
{
    a = std::max(0.0f, a);
    b = std::max(0.0f, b);
    const float sum = a + b;
    if (sum <= budget || sum <= 1e-6f) {
        return;
    }
    const float s = budget / sum;
    a *= s;
    b *= s;
}

} // namespace

int sliceBrush(const FBrush& brush, const Rect2D& dest, glm::vec2 texturePx, FBrushSlice out[kMaxBrushSlices])
{
    if (dest.extent.x <= 0.0f || dest.extent.y <= 0.0f) {
        return 0;
    }

    const bool bSliced = (brush.drawType == FBrush::EDrawType::NinePatch ||
                          brush.drawType == FBrush::EDrawType::Border) &&
                         !brush.isSolid() && texturePx.x > 0.0f && texturePx.y > 0.0f;
    if (!bSliced) {
        out[0] = FBrushSlice{dest, {0.0f, 0.0f}, {1.0f, 1.0f}};
        return 1;
    }

    float texLeft   = brush.margin.x;
    float texTop    = brush.margin.y;
    float texRight  = brush.margin.z;
    float texBottom = brush.margin.w;
    fitPair(texLeft, texRight, texturePx.x);
    fitPair(texTop, texBottom, texturePx.y);

    float destLeft   = texLeft;
    float destTop    = texTop;
    float destRight  = texRight;
    float destBottom = texBottom;
    fitPair(destLeft, destRight, dest.extent.x);
    fitPair(destTop, destBottom, dest.extent.y);

    const float uvX[4] = {
        0.0f,
        texLeft / texturePx.x,
        1.0f - texRight / texturePx.x,
        1.0f,
    };
    const float uvY[4] = {
        0.0f,
        texTop / texturePx.y,
        1.0f - texBottom / texturePx.y,
        1.0f,
    };
    const float destX[4] = {
        dest.pos.x,
        dest.pos.x + destLeft,
        dest.pos.x + dest.extent.x - destRight,
        dest.pos.x + dest.extent.x,
    };
    const float destY[4] = {
        dest.pos.y,
        dest.pos.y + destTop,
        dest.pos.y + dest.extent.y - destBottom,
        dest.pos.y + dest.extent.y,
    };

    const bool bSkipCenter = brush.drawType == FBrush::EDrawType::Border;
    int        count       = 0;
    for (int row = 0; row < 3; ++row) {
        const float h = destY[row + 1] - destY[row];
        if (h <= 1e-4f) {
            continue;
        }
        for (int col = 0; col < 3; ++col) {
            if (bSkipCenter && row == 1 && col == 1) {
                continue;
            }
            const float w = destX[col + 1] - destX[col];
            if (w <= 1e-4f) {
                continue;
            }
            FBrushSlice slice;
            slice.dest.pos    = {destX[col], destY[row]};
            slice.dest.extent = {w, h};
            slice.uvOffset    = {uvX[col], uvY[row]};
            slice.uvScale     = {uvX[col + 1] - uvX[col], uvY[row + 1] - uvY[row]};
            out[count++]      = slice;
        }
    }
    return count;
}

} // namespace ya
