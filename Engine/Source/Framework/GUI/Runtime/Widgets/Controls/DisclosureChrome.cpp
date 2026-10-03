#include "GUI/Widgets/Controls/DisclosureChrome.h"

#include <algorithm>

namespace ya
{
namespace
{

Rect2D disclosureBoxRect(const Rect2D& buttonRect)
{
    const float box = std::clamp(std::min(buttonRect.extent.x, buttonRect.extent.y) - 4.0f,
                                 10.0f,
                                 14.0f);
    return Rect2D{
        .pos = {
            buttonRect.pos.x + (buttonRect.extent.x - box) * 0.5f,
            buttonRect.pos.y + (buttonRect.extent.y - box) * 0.5f,
        },
        .extent = {box, box},
    };
}

void paintHover(UIFrameBuilder& builder, const Rect2D& rect, bool bHovered, const FBrush& hoveredFill)
{
    if (bHovered && hoveredFill.tintColor.a > 0.0f) {
        builder.addBrush(rect, hoveredFill);
    }
}

void paintPlusMinus(UIFrameBuilder& builder, const Rect2D& boxRect, bool bExpanded, const glm::vec4& color)
{
    builder.addRectOutline(insetRect(boxRect, 1.0f), color, 1.0f);
    const float pad  = std::max(2.5f, boxRect.extent.x * 0.22f);
    const float midX = boxRect.pos.x + boxRect.extent.x * 0.5f;
    const float midY = boxRect.pos.y + boxRect.extent.y * 0.5f;
    const float x0   = boxRect.pos.x + pad;
    const float x1   = boxRect.pos.x + boxRect.extent.x - pad;
    const float y0   = boxRect.pos.y + pad;
    const float y1   = boxRect.pos.y + boxRect.extent.y - pad;
    builder.addLine({x0, midY}, {x1, midY}, color, 1.25f);
    if (!bExpanded) {
        builder.addLine({midX, y0}, {midX, y1}, color, 1.25f);
    }
}

void paintChevron(UIFrameBuilder& builder, const Rect2D& boxRect, bool bExpanded, const glm::vec4& color)
{
    const glm::vec2 c = boxRect.pos + boxRect.extent * 0.5f;
    const float     s = std::min(boxRect.extent.x, boxRect.extent.y) * 0.22f;
    // Collapsed: `>` pointing right. Expanded: the same two segments rotated
    // 90° clockwise in Y-down (tip points down). Not two unrelated glyphs.
    const glm::vec2 r1{-s, -1.35f * s};
    const glm::vec2 r2{s, 0.0f};
    const glm::vec2 r3{-s, 1.35f * s};
    auto map = [&](glm::vec2 r) {
        if (!bExpanded) {
            return c + r;
        }
        return glm::vec2{c.x - r.y, c.y + r.x};
    };
    // Diagonal strokes: the hard rotated quad staircases on the 1x surface and
    // the OS upscale smears the steps; the AA fringe keeps the mark legible.
    constexpr float kChevronFeather = 0.55f;
    builder.addLine(map(r1), map(r2), color, 1.5f, kChevronFeather);
    builder.addLine(map(r2), map(r3), color, 1.5f, kChevronFeather);
}

void paintDisclosureImage(UIFrameBuilder& builder, const Rect2D& boxRect, const FDisclosureSpec& spec, bool bExpanded)
{
    const bool bHasExpanded = brushHasIcon(spec.expandedImage);
    if (bExpanded && bHasExpanded) {
        builder.addBrush(boxRect, spec.expandedImage);
        return;
    }
    if (!brushHasIcon(spec.collapsedImage)) {
        paintChevron(builder, boxRect, bExpanded, spec.collapsedImage.tintColor.a > 0.0f
                                                      ? spec.collapsedImage.tintColor
                                                      : glm::vec4{0.60f, 0.65f, 0.70f, 1.0f});
        return;
    }
    if (bExpanded && !bHasExpanded) {
        const auto texture = builder.resolveTexture(spec.collapsedImage.resource);
        builder.addSprite(boxRect, spec.collapsedImage.tintColor, texture, {1.0f, 1.0f}, {-1.0f, -1.0f});
        return;
    }
    builder.addBrush(boxRect, spec.collapsedImage);
}

} // namespace

FDisclosureLeading layoutDisclosureLeading(const Rect2D& header,
                                           float         buttonSlotWidth,
                                           bool          bShowButton,
                                           bool          bHasIcon,
                                           float         iconSize,
                                           float         packHeight)
{
    constexpr float kInset = 2.0f;
    constexpr float kGap   = 4.0f;
    const float innerH = std::max(0.0f, header.extent.y - kInset * 2.0f);
    const float rowH   = packHeight > 0.0f ? std::min(packHeight, std::max(0.0f, header.extent.y))
                                           : innerH;
    const float y      = header.pos.y + (header.extent.y - rowH) * 0.5f;
    FDisclosureLeading out;
    float x = header.pos.x + kInset;
    if (bShowButton) {
        out.button = {
            .pos    = {x, y},
            .extent = {buttonSlotWidth, rowH},
        };
        x += buttonSlotWidth + kGap;
    }
    if (bHasIcon) {
        const float size = std::min(iconSize, std::max(0.0f, rowH));
        out.icon         = {
            .pos    = {x, y + (rowH - size) * 0.5f},
            .extent = {size, size},
        };
        x += size + kGap;
    }
    out.title = {
        .pos    = {x, y},
        .extent = {std::max(0.0f, header.pos.x + header.extent.x - x - kGap), rowH},
    };
    return out;
}

void paintDisclosureButton(UIFrameBuilder& builder, const FDisclosurePaint& paint)
{
    if (paint.spec.kind == EDisclosureKind::Hidden) {
        return;
    }
    if (paint.buttonRect.extent.x <= 0.0f || paint.buttonRect.extent.y <= 0.0f) {
        return;
    }
    const Rect2D boxRect = disclosureBoxRect(paint.buttonRect);
    paintHover(builder, boxRect, paint.bHovered, paint.hoveredFill);

    switch (paint.spec.kind) {
    case EDisclosureKind::PlusMinus:
        paintPlusMinus(builder, boxRect, paint.bExpanded, paint.color);
        break;
    case EDisclosureKind::Chevron:
        paintChevron(builder, boxRect, paint.bExpanded, paint.color);
        break;
    case EDisclosureKind::Glyph:
        if (paint.font) {
            builder.addText(boxRect,
                            paint.bExpanded ? paint.spec.expandedGlyph : paint.spec.collapsedGlyph,
                            paint.color,
                            paint.font,
                            EWidgetAlignH::Center,
                            EWidgetAlignV::Center);
        }
        break;
    case EDisclosureKind::Image:
        paintDisclosureImage(builder, boxRect, paint.spec, paint.bExpanded);
        break;
    case EDisclosureKind::Hidden:
        break;
    }
}

} // namespace ya
