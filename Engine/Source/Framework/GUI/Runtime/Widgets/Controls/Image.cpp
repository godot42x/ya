#include "GUI/Widgets/Controls/Image.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "RHI/Core/Texture.h"


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

void UIImage::setResourceMissing(bool missing)
{
    if (_bResourceMissing == missing) {
        return;
    }
    _bResourceMissing = missing;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIImage::paintSelf(UIFrameBuilder& builder)
{
    if (_texture) {
        builder.addSprite(_layoutRect, _tint, _texture);
        return;
    }

    const FImageStyle& style = resolvedStyle();
    if (!_assetPath.empty()) {
        const FGuiTextureLookup lookup = builder.resolveTextureLookup(_assetPath);
        if (lookup.state == EGuiTextureState::Ready && lookup.texture) {
            builder.addSprite(_layoutRect, _tint, lookup.texture);
            return;
        }
        if (lookup.state == EGuiTextureState::Failed || _bResourceMissing) {
            builder.addBrush(_layoutRect, style.errorFill);
            return;
        }
        builder.addBrush(_layoutRect, style.placeholderFill);
        return;
    }

    builder.addBrush(_layoutRect, _bResourceMissing ? style.errorFill : style.placeholderFill);
}

} // namespace ya
