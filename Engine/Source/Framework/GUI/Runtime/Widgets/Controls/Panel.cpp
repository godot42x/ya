#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIPanel::paintSelf(UIFrameBuilder& builder)
{
    if (!_image.isLoaded()) {
        builder.addSprite(_layoutRect, _color, nullptr);
        return;
    }
    // Strong lifetime: the builder resolves the texture through the host's
    // resolver; the snapshot retains it until queue submit completes.
    builder.addSprite(_layoutRect, _color, builder.resolveTexture(_image.getPath()));
}

} // namespace ya
