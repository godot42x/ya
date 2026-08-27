#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIPanel::paintSelf(UIFrameBuilder& builder)
{
    // Resolve chain (plan §3.2): authored FPanelStyle > setColor degenerate
    // override > theme key > authoring fill/image. Presenters that recolor
    // via setColor keep working under any mounted theme.
    if (_authoredStyle.has_value()) {
        builder.addBrush(_layoutRect, _authoredStyle->fillColor);
        return;
    }
    if (!_styleKey.empty() && !_bExplicitFill) {
        if (const FPanelStyle* style = resolveThemeStyle<FPanelStyle>(*this, _styleKey)) {
            builder.addBrush(_layoutRect, style->fillColor);
            return;
        }
    }

    // Framework fallback: authoring fill + optional authored image
    // (unchanged behavior).
    if (!_image.isLoaded()) {
        if (_cornerRadius > 0.0f) {
            builder.addRoundedRect(_layoutRect, _color, _cornerRadius);
        }
        else {
            builder.addSprite(_layoutRect, _color, nullptr);
        }
        return;
    }
    // Strong lifetime: the builder resolves the texture through the host's
    // resolver; the snapshot retains it until queue submit completes.
    builder.addSprite(_layoutRect, _color, builder.resolveTexture(_image.getPath()));
}

} // namespace ya
