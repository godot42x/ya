#include "GUI/Widgets/UIFrameSnapshot.h"

#include "RHI/Core/Texture.h"
#include "Render/Resources/FontManager.h"

#include <algorithm>

namespace ya
{

void UIFrameBuilder::pushClip(const Rect2D& logicalClip)
{
    // A clip pushed while a render transform is active lives in the
    // transform's output space (the same space the emitted items' rects are
    // mapped INTO), so it is resolved here, at push time, and only
    // uiScale/offset applied later.
    Rect2D resolved = mapRenderTransformRect(logicalClip);
    if (!_clipStack.empty()) {
        const Rect2D& parent = _clipStack.back();
        const glm::vec2 parentMax = parent.pos + parent.extent;
        const glm::vec2 clipMax   = resolved.pos + resolved.extent;
        resolved.pos    = glm::max(resolved.pos, parent.pos);
        resolved.extent = glm::max(glm::vec2(0.0f), glm::min(clipMax, parentMax) - resolved.pos);
    }
    _clipStack.push_back(resolved);
}

void UIFrameBuilder::popClip()
{
    if (!_clipStack.empty()) {
        _clipStack.pop_back();
    }
}

void UIFrameBuilder::pushRenderTransform(const FUIRenderTransform& transform)
{
    FUIResolvedRenderTransform resolved;
    resolved.scale       = transform.scale;
    resolved.translation = transform.pivot * (glm::vec2(1.0f) - transform.scale) + transform.translation;
    resolved.opacity     = transform.opacity;
    resolved.tint        = transform.tint;
    if (!_renderTransformStack.empty()) {
        const FUIResolvedRenderTransform& parent = _renderTransformStack.back();
        // Compose: the new transform maps first (inner, p -> p*S1 + T1), then
        // the parent maps the result (outer, p' -> p'*S2 + T2). Combined:
        //   p -> p * (S1*S2) + (T1*S2 + T2)
        const glm::vec2 translation = resolved.translation * parent.scale + parent.translation;
        resolved.translation = translation;
        resolved.scale       = resolved.scale * parent.scale;
        resolved.opacity     = resolved.opacity * parent.opacity;
        resolved.tint        = resolved.tint * parent.tint;
    }
    _renderTransformStack.push_back(resolved);
}

void UIFrameBuilder::popRenderTransform()
{
    if (!_renderTransformStack.empty()) {
        _renderTransformStack.pop_back();
    }
}

const UIFrameBuilder::FUIResolvedRenderTransform& UIFrameBuilder::currentRenderTransform() const
{
    static const FUIResolvedRenderTransform kIdentity{};
    return _renderTransformStack.empty() ? kIdentity : _renderTransformStack.back();
}

Rect2D UIFrameBuilder::mapRenderTransformRect(const Rect2D& rect) const
{
    if (_renderTransformStack.empty()) {
        return rect;
    }
    const FUIResolvedRenderTransform& xf = _renderTransformStack.back();
    return Rect2D{
        .pos    = rect.pos * xf.scale + xf.translation,
        .extent = rect.extent * xf.scale,
    };
}

glm::vec2 UIFrameBuilder::mapRenderTransformPoint(const glm::vec2& point) const
{
    if (_renderTransformStack.empty()) {
        return point;
    }
    const FUIResolvedRenderTransform& xf = _renderTransformStack.back();
    return point * xf.scale + xf.translation;
}

glm::vec4 UIFrameBuilder::mapRenderTransformColor(const glm::vec4& color) const
{
    if (_renderTransformStack.empty()) {
        return color;
    }
    const FUIResolvedRenderTransform& xf = _renderTransformStack.back();
    glm::vec4 out = color * xf.tint;
    out.w *= xf.opacity;
    return out;
}

glm::vec2 UIFrameBuilder::getRenderTransformScale() const
{
    return currentRenderTransform().scale;
}

void UIFrameBuilder::addSprite(const Rect2D&                   logicalRect,
                               const glm::vec4&                color,
                               const std::shared_ptr<Texture>& texture,
                               glm::vec2                       uvOffset,
                               glm::vec2                       uvScale,
                               bool                            bOpaqueSample)
{
    const Rect2D rect = mapRenderTransformRect(logicalRect);
    UIFrameDrawItem item;
    item.kind          = UIFrameDrawItem::EKind::Sprite;
    item.pos           = toPx(rect.pos);
    item.size          = rect.extent * _ctx.uiScale;
    item.color         = mapRenderTransformColor(color);
    item.texture       = texture;
    item.uvOffset      = uvOffset;
    item.uvScale       = uvScale;
    item.bOpaqueSample = bOpaqueSample;
    if (!_clipStack.empty()) {
        item.bClipped = true;
        const Rect2D& clip = _clipStack.back();
        item.clip.pos     = toPx(clip.pos);
        item.clip.extent  = clip.extent * _ctx.uiScale;
    }
    _items.push_back(std::move(item));
}

void UIFrameBuilder::addRoundedRect(const Rect2D& logicalRect, const glm::vec4& color, float cornerRadius)
{
    const Rect2D rect     = mapRenderTransformRect(logicalRect);
    const float  scaleMix = 0.5f * (getRenderTransformScale().x + getRenderTransformScale().y);
    UIFrameDrawItem item;
    item.kind         = UIFrameDrawItem::EKind::Sprite;
    item.pos          = toPx(rect.pos);
    item.size         = rect.extent * _ctx.uiScale;
    item.color        = mapRenderTransformColor(color);
    item.cornerRadius = cornerRadius * _ctx.uiScale.x * scaleMix;
    if (!_clipStack.empty()) {
        item.bClipped = true;
        const Rect2D& clip = _clipStack.back();
        item.clip.pos     = toPx(clip.pos);
        item.clip.extent  = clip.extent * _ctx.uiScale;
    }
    _items.push_back(std::move(item));
}

void UIFrameBuilder::addRoundedSurface(const Rect2D&    logicalRect,
                                       const glm::vec4& fillColor,
                                       const glm::vec4& borderColor,
                                       float            cornerRadius,
                                       float            borderThickness)
{
    const bool bHasBorder = borderColor.a > 0.0f && borderThickness > 0.0f &&
                            logicalRect.extent.x > borderThickness * 2.0f &&
                            logicalRect.extent.y > borderThickness * 2.0f;
    if (!bHasBorder) {
        addRoundedRect(logicalRect, fillColor, cornerRadius);
        return;
    }

    // Outer ring first, then the fill inset inside it: a uniform-radius border
    // without a second shader path (the sprite shader fills a rounded quad; it
    // cannot carve a ring).
    addRoundedRect(logicalRect, borderColor, cornerRadius);
    if (fillColor.a <= 0.0f) {
        // Border-only surface (a rounded hairline): nothing to inset-draw, and
        // skipping the transparent fill keeps the frame at one draw item.
        return;
    }
    const Rect2D inner = insetRect(logicalRect, borderThickness);
    addRoundedRect(inner, fillColor, std::max(cornerRadius - borderThickness, 0.0f));
}

void UIFrameBuilder::addRectFilledMultiColor(const Rect2D&    logicalRect,
                                             const glm::vec4& colTL,
                                             const glm::vec4& colTR,
                                             const glm::vec4& colBR,
                                             const glm::vec4& colBL)
{
    const Rect2D  rect = mapRenderTransformRect(logicalRect);
    const glm::vec4 tl  = mapRenderTransformColor(colTL);
    const glm::vec4 tr  = mapRenderTransformColor(colTR);
    const glm::vec4 br  = mapRenderTransformColor(colBR);
    const glm::vec4 bl  = mapRenderTransformColor(colBL);
    UIFrameDrawItem item;
    item.kind            = UIFrameDrawItem::EKind::Sprite;
    item.pos             = toPx(rect.pos);
    item.size            = rect.extent * _ctx.uiScale;
    item.color           = tl;
    item.vertexColors    = {tl, tr, br, bl};
    item.bPerVertexColor = true;
    if (!_clipStack.empty()) {
        item.bClipped = true;
        const Rect2D& clip = _clipStack.back();
        item.clip.pos     = toPx(clip.pos);
        item.clip.extent  = clip.extent * _ctx.uiScale;
    }
    _items.push_back(std::move(item));
}

void UIFrameBuilder::addBrush(const Rect2D& logicalRect, const FBrush& brush)
{
    // A solid brush IS a surface: rounded fill + optional hairline ring, drawn
    // by the SDF round-rect branch of the sprite shader. This is the single place
    // a themed surface is realized, so every control that paints through a style
    // brush (button / field / tab / menu / card / badge / row) picks up roundness
    // and edge definition from its state brush alone.
    if (brush.isSolid()) {
        const bool bHasBorder = brush.borderColor.a > 0.0f && brush.borderThickness > 0.0f;
        if (brush.cornerRadius > 0.0f || bHasBorder) {
            addRoundedSurface(logicalRect,
                              brush.tintColor,
                              brush.borderColor,
                              brush.cornerRadius,
                              brush.borderThickness);
            return;
        }
    }
    std::shared_ptr<Texture> texture;
    glm::vec2                texturePx{0.0f, 0.0f};
    if (!brush.resource.empty()) {
        texture = resolveTexture(brush.resource);
        if (texture) {
            texturePx = {static_cast<float>(texture->getWidth()), static_cast<float>(texture->getHeight())};
        }
    }
    // Solid fill (no resource) and image (resource + tint) both go through
    // addSprite: null texture draws the white sprite, so tint colors it. This
    // is the "solid color is the degenerate brush form" grounding.
    // NinePatch/Border emit one sprite per UV cell (see sliceBrush).
    FBrushSlice slices[kMaxBrushSlices];
    const int   count = sliceBrush(brush, logicalRect, texturePx, slices);
    for (int i = 0; i < count; ++i) {
        addSprite(slices[i].dest, brush.tintColor, texture, slices[i].uvOffset, slices[i].uvScale);
    }
}

void UIFrameBuilder::addText(const Rect2D& logicalRect,
                             const std::string& text,
                             const glm::vec4& color,
                             const std::shared_ptr<Font>& font,
                             EWidgetAlignH hAlign,
                             EWidgetAlignV vAlign)
{
    if (!font || text.empty()) {
        return;
    }
    // Register missing glyphs at PAINT time (no GPU work): the host flushes
    // the pending set at a safe frame point (Core Rule 6 — never create
    // textures during command recording). Missing glyphs render as '?' for
    // one frame until the flush lands.
    if (FontManager::get()->requestGlyphs(*font, text)) {
        // New glyphs were registered: text measured against the '?' fallback
        // this frame is stale (e.g. CJK advance vs tofu). Re-measure next
        // frame by invalidating the current paint widget's layout.
        if (UIElement* widget = currentPaintWidget()) {
            widget->markLayoutDirty();
        }
    }

    const Rect2D  rect      = mapRenderTransformRect(logicalRect);
    const glm::vec2 pos      = toPx(rect.pos);
    const glm::vec2 size     = rect.extent * _ctx.uiScale;
    const glm::vec2 textScale = _ctx.uiScale * getRenderTransformScale();
    glm::vec2       drawPos  = pos;

    const float textWidth  = font->measureText(text);
    const float textScaleX = textScale.x;
    const float textScaleY = textScale.y;
    if (hAlign == EWidgetAlignH::Center) {
        drawPos.x += (size.x - textWidth * textScaleX) * 0.5f;
    }
    else if (hAlign == EWidgetAlignH::Right) {
        drawPos.x += size.x - textWidth * textScaleX;
    }
    if (vAlign == EWidgetAlignV::Center) {
        drawPos.y += (size.y - font->lineHeight * textScaleY) * 0.5f;
    }
    else if (vAlign == EWidgetAlignV::Bottom) {
        drawPos.y += size.y - font->lineHeight * textScaleY;
    }

    UIFrameDrawItem item;
    item.kind  = UIFrameDrawItem::EKind::Text;
    item.pos   = drawPos;
    item.size  = {textWidth * textScaleX, font->lineHeight * textScaleY};
    item.color = mapRenderTransformColor(color);
    item.font  = font;
    item.text  = text;
    item.textScale = textScale;
    if (!_clipStack.empty()) {
        item.bClipped = true;
        const Rect2D& clip = _clipStack.back();
        item.clip.pos     = toPx(clip.pos);
        item.clip.extent  = clip.extent * _ctx.uiScale;
    }
    _items.push_back(std::move(item));
}

void UIFrameBuilder::addLine(const glm::vec2& logicalFrom,
                             const glm::vec2& logicalTo,
                             const glm::vec4& color,
                             float            thickness)
{
    const float scaleMix = 0.5f * (getRenderTransformScale().x + getRenderTransformScale().y);
    UIFrameDrawItem item;
    item.kind          = UIFrameDrawItem::EKind::Line;
    item.lineFrom      = toPx(mapRenderTransformPoint(logicalFrom));
    item.lineTo        = toPx(mapRenderTransformPoint(logicalTo));
    item.color         = mapRenderTransformColor(color);
    item.lineThickness = thickness * scaleMix;
    if (!_clipStack.empty()) {
        item.bClipped = true;
        const Rect2D& clip = _clipStack.back();
        item.clip.pos     = toPx(clip.pos);
        item.clip.extent  = clip.extent * _ctx.uiScale;
    }
    _items.push_back(std::move(item));
}

void UIFrameBuilder::addRectOutline(const Rect2D& logicalRect, const glm::vec4& color, float thickness)
{
    const glm::vec2 p0 = logicalRect.pos;
    const glm::vec2 p1 = logicalRect.pos + logicalRect.extent;
    addLine(p0, {p1.x, p0.y}, color, thickness);
    addLine({p1.x, p0.y}, p1, color, thickness);
    addLine(p1, {p0.x, p1.y}, color, thickness);
    addLine({p0.x, p1.y}, p0, color, thickness);
}

void UIFrameBuilder::addCheckMark(const Rect2D& boxRect, const glm::vec4& color)
{
    const float x = boxRect.pos.x;
    const float y = boxRect.pos.y;
    const float w = boxRect.extent.x;
    const float h = boxRect.extent.y;
    const float thickness = std::max(1.5f, w * 0.12f);
    const glm::vec2 p0{x + w * 0.22f, y + h * 0.52f};
    const glm::vec2 p1{x + w * 0.42f, y + h * 0.74f};
    const glm::vec2 p2{x + w * 0.80f, y + h * 0.26f};
    addLine(p0, p1, color, thickness);
    addLine(p1, p2, color, thickness);
}

void UIFrameBuilder::addBezierCubic(const glm::vec2& p0,
                                    const glm::vec2& c1,
                                    const glm::vec2& c2,
                                    const glm::vec2& p1,
                                    const glm::vec4& color,
                                    float            thickness,
                                    int              segments)
{
    segments = std::clamp(segments, 1, 64);
    glm::vec2 prev = p0;
    for (int i = 1; i <= segments; ++i) {
        const float   t  = static_cast<float>(i) / static_cast<float>(segments);
        const float   u  = 1.0f - t;
        const glm::vec2 pt = u * u * u * p0 + 3.0f * u * u * t * c1 +
                             3.0f * u * t * t * c2 + t * t * t * p1;
        addLine(prev, pt, color, thickness);
        prev = pt;
    }
}

UIFrameSnapshot UIFrameBuilder::build(Extent2D logicalExtent)
{
    UIFrameSnapshot snapshot;
    snapshot.logicalExtent = logicalExtent;
    snapshot.buildContext  = _ctx;
    // Catalog is tree-owned; the immutable packet must not keep a pointer
    // that outlives WidgetTree::buildSnapshot.
    snapshot.buildContext.textureCatalog = nullptr;
    snapshot.items         = std::move(_items);
    return snapshot;
}

FGuiTextureLookup UIFrameBuilder::resolveTextureLookup(const std::string& assetPath) const
{
    if (_ctx.textureCatalog) {
        return _ctx.textureCatalog->bind(assetPath, _ctx.textureResolver);
    }
    FGuiTextureLookup lookup;
    if (_ctx.textureResolver) {
        lookup.texture = _ctx.textureResolver(assetPath);
        lookup.state   = lookup.texture ? EGuiTextureState::Ready : EGuiTextureState::Pending;
    }
    return lookup;
}

bool UIFrameBuilder::hasCachedItems(const UIElement* widget) const
{
    return _readCache && _readCache->find(widget->getRuntimeId()) != _readCache->end();
}

void UIFrameBuilder::cacheItems(const UIElement* widget, size_t start)
{
    if (!_writeCache) {
        return;
    }
    // Cache even an empty segment (e.g. a plain panel whose paintSelf adds no
    // items): otherwise such a widget never registers in the read cache and
    // is re-run every frame.
    std::vector<UIFrameDrawItem> segment(_items.begin() + static_cast<ptrdiff_t>(start), _items.end());
    (*_writeCache)[widget->getRuntimeId()] = std::move(segment);
}

void UIFrameBuilder::reuseCachedItems(const UIElement* widget)
{
    if (!_readCache) {
        return;
    }
    const auto it = _readCache->find(widget->getRuntimeId());
    if (it == _readCache->end()) {
        return;
    }
    const std::vector<UIFrameDrawItem>& segment = it->second;
    _items.insert(_items.end(), segment.begin(), segment.end());
    // Re-write the reused segment into the write cache so the next frame can
    // keep reusing it (the write cache is the next frame's read cache).
    if (_writeCache) {
        (*_writeCache)[widget->getRuntimeId()] = segment;
    }
}

} // namespace ya
