#include "GUI/Widgets/Controls/Image.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "RHI/Core/Texture.h"

#include <nlohmann/json.hpp>

namespace ya
{

void UIImage::setTexture(std::shared_ptr<Texture> texture)
{
    if (_texture == texture) {
        return;
    }
    _texture = std::move(texture);
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIImage::paintSelf(UIFrameBuilder& builder)
{
    std::shared_ptr<Texture> texture = _texture;
    if (!texture && !_assetPath.empty()) {
        texture = builder.resolveTexture(_assetPath);
    }
    if (texture) {
        builder.addSprite(_layoutRect, _tint, texture);
        return;
    }
    const FImageStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect, style.placeholderFill);
}

void UIImage::deserializeFields(const nlohmann::json& fields)
{
    UIElement::deserializeFields(fields);
    static const glm::vec4 kDefaultPlaceholder{0.24f, 0.26f, 0.31f, 1.0f};
    if (!hasAuthoredStyle() && _placeholderColor != kDefaultPlaceholder) {
        setStyleField("placeholderFill", FBrush::solid(_placeholderColor), EUIPropertyImpact::Paint);
    }
}

} // namespace ya
