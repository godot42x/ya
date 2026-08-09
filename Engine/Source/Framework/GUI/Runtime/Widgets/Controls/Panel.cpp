#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Widgets/Controls/PaintCommon.h"
#include "GUI/Draw2D/Render2D.h"

namespace ya
{

void UIPanel::paintSelf(const WidgetPaintContext& ctx)
{
    Texture* texture = _image.isLoaded() ? _image.getShared().get() : nullptr;
    Render2D::makeSprite(glm::vec3(paint_util::toScreenPxPos(ctx, _layoutRect.pos), 0.0f),
                         paint_util::toScreenPxSize(ctx, _layoutRect.extent),
                         texture,
                         _color);
}

} // namespace ya
