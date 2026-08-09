#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Draw2D/Render2D.h"

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

void UIPanel::paintSelf(const WidgetPaintContext& ctx)
{
    Texture* texture = _image.isLoaded() ? _image.getShared().get() : nullptr;
    Render2D::makeSprite(glm::vec3(toScreenPxPos(ctx, _layoutRect.pos), 0.0f),
                         toScreenPxSize(ctx, _layoutRect.extent),
                         texture,
                         _color);
}

} // namespace ya
