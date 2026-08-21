#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIPanel::paintSelf(UIFrameBuilder& builder)
{
    // Theme resolution (style-system Phase 3): a panel WITHOUT an explicitly
    // authored fill (setColor never called) draws its fill brush from
    // FPanelStyle. Explicit authoring wins over the theme (resolve chain
    // "widget explicit override" first, plan §3.2), so presenters that
    // recolor panels keep working under any mounted theme.
    if (!_styleKey.empty() && !_bExplicitFill) {
        if (const FPanelStyle* style = resolveThemeStyle<FPanelStyle>(*this, _styleKey)) {
            builder.addBrush(_layoutRect, style->fillColor);
            return;
        }
    }

    // Framework fallback: authoring fill + optional authored image
    // (unchanged behavior).
    if (!_image.isLoaded()) {
        builder.addSprite(_layoutRect, _color, nullptr);
        return;
    }
    // Strong lifetime: the builder resolves the texture through the host's
    // resolver; the snapshot retains it until queue submit completes.
    builder.addSprite(_layoutRect, _color, builder.resolveTexture(_image.getPath()));
}

} // namespace ya
