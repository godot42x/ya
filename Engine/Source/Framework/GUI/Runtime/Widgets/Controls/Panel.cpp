#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

#include <nlohmann/json.hpp>

namespace ya
{

void UIPanel::paintSelf(UIFrameBuilder& builder)
{
    const FPanelStyle& style = resolvedStyle();
    auto paintFill = [&](const FBrush& fill) {
        if (_cornerRadius > 0.0f && fill.isSolid()) {
            builder.addRoundedRect(_layoutRect, fill.tintColor, _cornerRadius);
            return;
        }
        builder.addBrush(_layoutRect, fill);
    };

    if (_image.isLoaded()) {
        const bool bThemed = !_styleKey.empty() && getTree() && getTree()->getTheme()
                             && getTree()->getTheme()->find<FPanelStyle>(_styleKey);
        // Image is content. A mounted theme without an authored overlay still
        // owns chrome; GI-202 recolor (authored fillColor) tints the image.
        if (!hasAuthoredStyle() && bThemed) {
            paintFill(style.fillColor);
            return;
        }
        builder.addSprite(_layoutRect, style.fillColor.tintColor, builder.resolveTexture(_image.getPath()));
        return;
    }
    paintFill(style.fillColor);
}

const FPanelStyle& UIPanel::resolvedStyle(ReactiveBase::EDirtyLevel level, bool bTrackDependencies) const
{
    return resolvedStyleCache(*this, level,
                              [this](FPanelStyle& style) {
                                  const bool bThemed = !_styleKey.empty() && getTree() && getTree()->getTheme()
                                                       && getTree()->getTheme()->find<FPanelStyle>(_styleKey);
                                  if (bThemed) {
                                      return;
                                  }
                                  const bool bHasFill = _authoredStyle.is_object() && _authoredStyle.contains("fillColor");
                                  if (!bHasFill) {
                                      style.fillColor = FBrush::solid(_color);
                                  }
                              },
                              bTrackDependencies);
}

void UIPanel::deserializeFields(const nlohmann::json& fields)
{
    UIElement::deserializeFields(fields);
}

} // namespace ya
