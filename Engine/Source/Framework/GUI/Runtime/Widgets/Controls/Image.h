#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <memory>

namespace ya
{

struct Texture;

/// Image element: draws a texture stretched to the layout rect.
///
/// Resolution order: a live `_texture` (editor viewport / composed RT) wins;
/// otherwise `_assetPath` is resolved through the frame build context's
/// textureResolver. Without either, the themed placeholder fill is drawn so
/// layout and hit testing stay visible in any host.
struct YA_GUI_API UIImage : public UIElement, public UIStyledWidget<UIImage, FImageStyle>
{
    YA_REFLECT_BEGIN(UIImage, UIElement)
    YA_REFLECT_FIELD(_assetPath, .instanceEditable())
    YA_REFLECT_FIELD(_tint, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FImageStyle)

    explicit UIImage(std::string name = "Image") : UIElement(std::move(name), "image") {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIImage>; }

    /// Asset path resolved through UIFrameBuildContext::textureResolver.
    std::string _assetPath;
    glm::vec4   _tint = {1.0f, 1.0f, 1.0f, 1.0f};
    /// Host-owned live GPU image (viewport RT). The snapshot retains this
    /// shared_ptr through queue submit. Takes precedence over `_assetPath`.
    void setTexture(std::shared_ptr<Texture> texture);
    [[nodiscard]] const std::shared_ptr<Texture>& getTexture() const { return _texture; }
    /// True when a resource was expected but is unavailable (failed/missing
    /// asset resolve or viewport texture not ready). Distinct from the neutral
    /// placeholder shown for an intentionally empty image.
    void setResourceMissing(bool missing);
    [[nodiscard]] bool isResourceMissing() const { return _bResourceMissing; }

    void paintSelf(UIFrameBuilder& builder) override;
    [[nodiscard]] bool isHoverable() const override { return true; }

  private:
    std::shared_ptr<Texture> _texture;
    bool                     _bResourceMissing = false;
};

} // namespace ya
