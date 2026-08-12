#include "GUI/Widgets/Controls/Image.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIImage::paintSelf(UIFrameBuilder& builder)
{
    std::shared_ptr<Texture> texture;
    if (!_assetPath.empty()) {
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
