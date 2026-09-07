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
/// otherwise `_assetPath` is resolved through the tree's texture catalog
/// (Pending = placeholderFill, Ready = sprite, Failed = errorFill).
/// `setResourceMissing(true)` is for live RT absence (viewport), not "path
/// not loaded yet". Hosts bump `UIFrameBuildContext.generation` only when the
/// resolver identity changes; everyday ready is catalog.notify(path).
struct YA_GUI_API UIImage : public UIElement, public UIStyledWidget<UIImage, FImageStyle>
{
    YA_REFLECT_BEGIN(UIImage, UIElement)
    YA_REFLECT_FIELD(_assetPath, .instanceEditable())
    YA_REFLECT_FIELD(_tint, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FImageStyle)

    explicit UIImage(std::string name = "Image") : UIElement(std::move(name), "image") {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIImage>; }

    void setAssetPath(std::string path)
    {
        if (_assetPath == path) {
            return;
        }
        _assetPath = std::move(path);
        invalidateProperty(EUIPropertyImpact::Paint);
    }

    /// Asset path resolved through the tree texture catalog / frame builder.
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
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override
    {
        node["control"] = {{"type", "image"}, {"assetPath", _assetPath}};
    }
    [[nodiscard]] bool isHoverable() const override { return true; }

  private:
    std::shared_ptr<Texture> _texture;
    bool                     _bResourceMissing = false;
};

} // namespace ya
