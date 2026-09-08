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

void UIImage::setOpaqueSample(bool opaque)
{
    if (_bOpaqueSample == opaque) {
        return;
    }
    _bOpaqueSample = opaque;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIImage::setScaleMode(EImageScaleMode mode)
{
    if (_scaleMode == mode) {
        return;
    }
    _scaleMode = mode;
    invalidateProperty(EUIPropertyImpact::Paint);
}

namespace
{

Rect2D spriteDestRect(const Rect2D& bounds, EImageScaleMode mode, const Texture& texture)
{
    if (mode != EImageScaleMode::Contain) {
        return bounds;
    }
    return containedImageRect(bounds,
                              static_cast<float>(texture.getWidth()),
                              static_cast<float>(texture.getHeight()));
}

} // namespace

void UIImage::paintSelf(UIFrameBuilder& builder)
{
    if (_texture) {
        builder.addSprite(spriteDestRect(_layoutRect, _scaleMode, *_texture),
                          _tint,
                          _texture,
                          {0.0f, 0.0f},
                          {1.0f, 1.0f},
                          _bOpaqueSample);
        return;
    }

    const FImageStyle& style = resolvedStyle();
    if (!_assetPath.empty()) {
        const FGuiTextureLookup lookup = builder.resolveTextureLookup(_assetPath);
        if (lookup.state == EGuiTextureState::Ready && lookup.texture) {
            builder.addSprite(spriteDestRect(_layoutRect, _scaleMode, *lookup.texture),
                              _tint,
                              lookup.texture);
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
