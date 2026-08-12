#pragma once

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Image element: draws a texture resolved through the frame build context's
/// textureResolver (host-provided), stretched to the layout rect.
///
/// Without a resolver or on a cache miss the element draws a translucent
/// placeholder block (documented limitation for resolver-less hosts), so
/// layout and hit testing stay visible in any host.
struct UIImage : public UIElement
{
    YA_REFLECT_BEGIN(UIImage, UIElement)
    YA_REFLECT_FIELD(_assetPath, .instanceEditable())
    YA_REFLECT_FIELD(_tint, .instanceEditable())
    YA_REFLECT_FIELD(_placeholderColor, .instanceEditable())
    YA_REFLECT_END()

    explicit UIImage(std::string name = "Image") : UIElement(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIImage>; }

    /// Asset path resolved through UIFrameBuildContext::textureResolver.
    std::string _assetPath;
    glm::vec4   _tint = {1.0f, 1.0f, 1.0f, 1.0f};
    /// Drawn when the texture cannot be resolved.
    glm::vec4   _placeholderColor = {0.24f, 0.26f, 0.31f, 1.0f};

    void paintSelf(UIFrameBuilder& builder) override;
};

} // namespace ya
