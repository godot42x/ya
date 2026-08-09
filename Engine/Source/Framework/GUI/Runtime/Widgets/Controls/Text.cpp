#include "GUI/Widgets/Controls/Text.h"

#include "GUI/Draw2D/Render2D.h"
#include "GUI/Resources/FontManager.h"

namespace ya
{

namespace
{

glm::vec2 toScreenPxPos(const WidgetPaintContext& ctx, const glm::vec2& point)
{
    return point * ctx.uiScale;
}

glm::vec2 toScreenPxSize(const WidgetPaintContext& ctx, const glm::vec2& extent)
{
    return extent * ctx.uiScale;
}

} // namespace

void UIText::paintSelf(const WidgetPaintContext& ctx)
{
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }

    const glm::vec2 pos  = toScreenPxPos(ctx, _layoutRect.pos);
    const glm::vec2 size = toScreenPxSize(ctx, _layoutRect.extent);
    glm::vec2       drawPos = pos;

    const float textWidth  = font->measureText(_text);
    const float textScaleX = ctx.uiScale.x;
    const float textScaleY = ctx.uiScale.y;
    if (_hAlign == EWidgetAlignH::Center) {
        drawPos.x += (size.x - textWidth * textScaleX) * 0.5f;
    }
    else if (_hAlign == EWidgetAlignH::Right) {
        drawPos.x += size.x - textWidth * textScaleX;
    }
    if (_vAlign == EWidgetAlignV::Center) {
        drawPos.y += (size.y - font->lineHeight * textScaleY) * 0.5f;
    }
    else if (_vAlign == EWidgetAlignV::Bottom) {
        drawPos.y += size.y - font->lineHeight * textScaleY;
    }
    Render2D::makeText(_text, glm::vec3(drawPos, 0.0f), _color, font.get());
}

glm::vec2 UIText::computeDesiredSize() const
{
    if (!_bAutoSize) {
        return _size;
    }
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return _size;
    }
    return {font->measureText(_text), font->lineHeight};
}

} // namespace ya
