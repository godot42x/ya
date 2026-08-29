#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

#include <nlohmann/json.hpp>

namespace ya
{

std::unique_ptr<UISlot> UIPanel::createSlotForChild(UIElement& child)
{
    // Slots come from the installed canvas layout; the base hook routes
    // layout()/layoutAssigned()/computeDesiredSize() through it as well.
    if (const UICanvasLayout* canvas = getCanvasLayout()) {
        return canvas->createSlot(const_cast<UIPanel&>(*this), child);
    }
    return UIElement::createSlotForChild(child);
}

void UIPanel::paintSelf(UIFrameBuilder& builder)
{
    // Sparse overlay on the theme. An image binding is content, not chrome:
    // it wins over an authored solid fill so GI-202 recolor + image stays the
    // pre-fold setColor fallback. Theme still wins over image when the
    // panel has no authored style (same as before _bExplicitFill).
    auto paintFill = [&](const FBrush& fill) {
        if (_cornerRadius > 0.0f && fill.isSolid()) {
            builder.addRoundedRect(_layoutRect, fill.tintColor, _cornerRadius);
            return;
        }
        builder.addBrush(_layoutRect, fill);
    };

    if (!_image.isLoaded()) {
        const FPanelStyle& style = resolvedStyle();
        const bool        bThemed = !_styleKey.empty() && getTree() && getTree()->getTheme()
                             && getTree()->getTheme()->find<FPanelStyle>(_styleKey);
        if (hasAuthoredStyle() || bThemed) {
            paintFill(style.fillColor);
            return;
        }
        if (_cornerRadius > 0.0f) {
            builder.addRoundedRect(_layoutRect, _color, _cornerRadius);
        }
        else {
            builder.addSprite(_layoutRect, _color, nullptr);
        }
        return;
    }
    if (!hasAuthoredStyle() && !_styleKey.empty()) {
        if (const FPanelStyle* style = resolveThemeStyle<FPanelStyle>(*this, _styleKey)) {
            paintFill(style->fillColor);
            return;
        }
    }
    builder.addSprite(_layoutRect, _color, builder.resolveTexture(_image.getPath()));
}

void UIPanel::deserializeFields(const nlohmann::json& fields)
{
    nlohmann::json rest = fields;
    const bool bLegacyExplicit = rest.contains("_bExplicitFill") && rest["_bExplicitFill"] == true;
    rest.erase("_bExplicitFill");
    UIElement::deserializeFields(rest);
    if (bLegacyExplicit && !hasAuthoredStyle()) {
        setStyleField("fillColor", FBrush::solid(_color), EUIPropertyImpact::Paint);
    }
}

} // namespace ya
