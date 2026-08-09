#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIPanel::paintSelf(UIFrameBuilder& builder)
{
    const ya::Ptr<Texture> texture = _image.isLoaded() ? _image.getShared() : ya::Ptr<Texture>();
    builder.addSprite(_layoutRect, _color, texture);
}

} // namespace ya
