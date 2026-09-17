#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <cstdint>
#include <memory>

namespace ya
{

struct Texture;

enum class EImageScaleMode : uint8_t
{
    Stretch, // fill the layout rect (default; live RTs / square thumbs)
    Contain, // letterbox inside the layout rect, keep texture aspect
};

/// Dest rect that fits `texW` x `texH` inside `bounds` without cropping.
[[nodiscard]] inline Rect2D containedImageRect(const Rect2D& bounds, float texW, float texH)
{
    if (bounds.extent.x <= 0.0f || bounds.extent.y <= 0.0f || texW <= 0.0f || texH <= 0.0f) {
        return bounds;
    }
    const float boundAspect = bounds.extent.x / bounds.extent.y;
    const float texAspect   = texW / texH;
    Rect2D      dest        = bounds;
    if (texAspect > boundAspect) {
        dest.extent.y = bounds.extent.x / texAspect;
        dest.pos.y += (bounds.extent.y - dest.extent.y) * 0.5f;
    }
    else {
        dest.extent.x = bounds.extent.y * texAspect;
        dest.pos.x += (bounds.extent.x - dest.extent.x) * 0.5f;
    }
    return dest;
}

/// Image element: draws a texture into the layout rect.
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
    /// Live scene RTs (HDR / unused alpha) must not go through Sprite2D's
    /// `texColor.a < 0.01` discard — that punches the whole viewport to black.
    void setOpaqueSample(bool opaque);
    [[nodiscard]] bool isOpaqueSample() const { return _bOpaqueSample; }
    void setScaleMode(EImageScaleMode mode);
    [[nodiscard]] EImageScaleMode getScaleMode() const { return _scaleMode; }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override
    {
        node["control"] = {{"type", "image"}, {"assetPath", _assetPath}};
    }
    [[nodiscard]] bool isHoverable() const override { return true; }

  private:
    std::shared_ptr<Texture> _texture;
    bool                     _bResourceMissing = false;
    bool                     _bOpaqueSample    = false;
    EImageScaleMode          _scaleMode        = EImageScaleMode::Stretch;
};

} // namespace ya
