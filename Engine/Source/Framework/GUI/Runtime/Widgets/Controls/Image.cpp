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

void UIImage::paintSelf(UIFrameBuilder& builder)
{
    std::shared_ptr<Texture> texture = _texture;
    if (!texture && !_assetPath.empty()) {
        texture = builder.resolveTexture(_assetPath);
    }
    if (texture) {
        builder.addSprite(_layoutRect, _tint, texture);
    }
    else {
        // Placeholder block: unresolved images stay visible so layout and hit
        // testing remain debuggable in any host.
        builder.addSprite(_layoutRect, _placeholderColor, nullptr);
    }
}

} // namespace ya
