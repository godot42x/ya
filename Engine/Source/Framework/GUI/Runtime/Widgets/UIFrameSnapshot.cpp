#include "GUI/Widgets/UIFrameSnapshot.h"

#include "Render/Resources/FontManager.h"

#include <algorithm>

namespace ya
{

void UIFrameBuilder::pushClip(const Rect2D& logicalClip)
{
    Rect2D resolved = logicalClip;
    if (!_clipStack.empty()) {
        const Rect2D& parent = _clipStack.back();
        const glm::vec2 parentMax = parent.pos + parent.extent;
        const glm::vec2 clipMax   = logicalClip.pos + logicalClip.extent;
        resolved.pos    = glm::max(logicalClip.pos, parent.pos);
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

void UIFrameBuilder::addSprite(const Rect2D& logicalRect, const glm::vec4& color, const std::shared_ptr<Texture>& texture)
{
    UIFrameDrawItem item;
    item.kind    = UIFrameDrawItem::EKind::Sprite;
    item.pos     = toPx(logicalRect.pos);
    item.size    = logicalRect.extent * _ctx.uiScale;
    item.color   = color;
    item.texture = texture;
    if (!_clipStack.empty()) {
        item.bClipped = true;
        const Rect2D& clip = _clipStack.back();
        item.clip.pos     = toPx(clip.pos);
        item.clip.extent  = clip.extent * _ctx.uiScale;
    }
    _items.push_back(std::move(item));
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

    const glm::vec2 pos  = toPx(logicalRect.pos);
    const glm::vec2 size = logicalRect.extent * _ctx.uiScale;
    glm::vec2       drawPos = pos;

    const float textWidth  = font->measureText(text);
    const float textScaleX = _ctx.uiScale.x;
    const float textScaleY = _ctx.uiScale.y;
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
    item.color = color;
    item.font  = font;
    item.text  = text;
    item.textScale = _ctx.uiScale;
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
    UIFrameDrawItem item;
    item.kind          = UIFrameDrawItem::EKind::Line;
    item.lineFrom      = toPx(logicalFrom);
    item.lineTo        = toPx(logicalTo);
    item.color         = color;
    item.lineThickness = thickness;
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
    snapshot.items         = std::move(_items);
    return snapshot;
}

bool UIFrameBuilder::hasCachedItems(const UIElement* widget) const
{
    return _readCache && _readCache->find(widget) != _readCache->end();
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
    (*_writeCache)[widget] = std::move(segment);
}

void UIFrameBuilder::reuseCachedItems(const UIElement* widget)
{
    if (!_readCache) {
        return;
    }
    const auto it = _readCache->find(widget);
    if (it == _readCache->end()) {
        return;
    }
    const std::vector<UIFrameDrawItem>& segment = it->second;
    _items.insert(_items.end(), segment.begin(), segment.end());
    // Re-write the reused segment into the write cache so the next frame can
    // keep reusing it (the write cache is the next frame's read cache).
    if (_writeCache) {
        (*_writeCache)[widget] = segment;
    }
}

} // namespace ya
