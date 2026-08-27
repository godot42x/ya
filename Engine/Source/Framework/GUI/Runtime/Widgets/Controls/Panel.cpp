#include "GUI/Widgets/Controls/Panel.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

#include <nlohmann/json.hpp>

namespace ya
{

void UIPanel::paintSelf(UIFrameBuilder& builder)
{
    // Resolve chain: authored FPanelStyle (setStyle / setColor) > theme key >
    // authoring fill/image. An image binding is content, not chrome: it wins
    // over an authored solid fill so GI-202 recolor + image stays the
    // pre-fold setColor fallback. Theme still wins over image when the
    // panel has no authored style (same as before _bExplicitFill).
    auto paintFill = [&](const FBrush& fill) {
        if (_cornerRadius > 0.0f && fill.isSolid()) {
            builder.addRoundedRect(_layoutRect, fill.tintColor, _cornerRadius);
            return;
        }
        builder.addBrush(_layoutRect, fill);
    };

    if (_authoredStyle.has_value() && !_image.isLoaded()) {
        paintFill(_authoredStyle->fillColor);
        return;
    }
    if (!_authoredStyle.has_value() && !_styleKey.empty()) {
        if (const FPanelStyle* style = resolveThemeStyle<FPanelStyle>(*this, _styleKey)) {
            paintFill(style->fillColor);
            return;
        }
    }

    if (!_image.isLoaded()) {
        if (_cornerRadius > 0.0f) {
            builder.addRoundedRect(_layoutRect, _color, _cornerRadius);
        }
        else {
            builder.addSprite(_layoutRect, _color, nullptr);
        }
        return;
    }
    builder.addSprite(_layoutRect, _color, builder.resolveTexture(_image.getPath()));
}

void UIPanel::deserializeFields(const nlohmann::json& fields)
{
    nlohmann::json rest = fields;
    const bool bLegacyExplicit = rest.contains("_bExplicitFill") && rest["_bExplicitFill"] == true;
    rest.erase("_bExplicitFill");
    UIElement::deserializeFields(rest);
    if (bLegacyExplicit && !_authoredStyle) {
        FPanelStyle style;
        style.fillColor = FBrush::Solid(_color);
        _authoredStyle  = std::move(style);
    }
}

} // namespace ya
