#pragma once

// ============================================================================
// UIFrameSnapshot - immutable per-frame Game UI draw data (ui-widget-tree-
// refactor Phase 4).
//
// The tree is laid out and PAINTED into a builder before the RenderGraph is
// built; the resulting snapshot is the only thing command recording may read.
// Recording never touches WidgetTree / widgets / Scene / ECS.
//
// Items carry strong references to the fonts they draw and the same
// non-owning asset refs the renderer always used for textures (textures are
// owned by the asset cache, whose lifetime covers queue submit); GPU-safe
// lifetime is guaranteed even if the widget is detached or destroyed right
// after the snapshot was built.
// ============================================================================

#include "Core/Common/AssetRef.h"
#include "Core/Common/Types.h"

#include "GUI/Widgets/UIElement.h"

#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct Font;

/// Build context: how tree-local logical pixels map to render-target pixels.
/// The host owns the mapping (viewport rect, framebuffer scale, editor preview
/// offset/scale); widgets never see ImGui or window coordinates.
struct UIFrameBuildContext
{
    glm::vec2 uiScale = {1.0f, 1.0f}; // logical px -> render-target px
    glm::vec2 offset  = {0.0f, 0.0f}; // render-target px origin of logical (0,0)
};

/// One resolved draw command (render-target pixels, top-left origin, Y down).
struct UIFrameDrawItem
{
    enum class EKind : uint8_t
    {
        Sprite,
        Text,
    };

    EKind     kind;
    glm::vec2 pos    = {0.0f, 0.0f};
    glm::vec2 size   = {0.0f, 0.0f};
    glm::vec4 color  = {1.0f, 1.0f, 1.0f, 1.0f};
    bool      bClipped = false;
    Rect2D    clip    {}; // resolved clip rect (render-target px)

    // Sprite: null texture = white. Non-owning: the asset cache owns textures
    // and outlives queue submit (cleared only at shutdown after waitIdle).
    ya::Ptr<Texture> texture;
    // Text:
    std::shared_ptr<Font> font;
    std::string           text;
};

/// Immutable frame packet consumed by the compose pass.
struct UIFrameSnapshot
{
    Extent2D                   logicalExtent{};
    UIFrameBuildContext        buildContext;
    std::vector<UIFrameDrawItem> items;
};

/// Accumulates resolved draw items during the pre-graph paint pass.
class UIFrameBuilder
{
  public:
    explicit UIFrameBuilder(const UIFrameBuildContext& ctx) : _ctx(ctx) {}

    /// Push a logical clip rect (intersected with the current clip).
    void pushClip(const Rect2D& logicalClip);
    void popClip();

    /// Record a sprite. `logicalRect` in tree-local logical pixels; null
    /// texture draws the white texture.
    void addSprite(const Rect2D& logicalRect, const glm::vec4& color, const ya::Ptr<Texture>& texture);

    /// Record text aligned inside `logicalRect` (same semantics as the legacy
    /// UITextNode paint: h/v align via measured text).
    void addText(const Rect2D& logicalRect,
                 const std::string& text,
                 const glm::vec4& color,
                 const std::shared_ptr<Font>& font,
                 EWidgetAlignH hAlign,
                 EWidgetAlignV vAlign);

    /// Move the accumulated items into an immutable snapshot.
    [[nodiscard]] UIFrameSnapshot build(Extent2D logicalExtent);

  private:
    [[nodiscard]] glm::vec2 toPx(const glm::vec2& logical) const { return _ctx.offset + logical * _ctx.uiScale; }

    const UIFrameBuildContext& _ctx;
    std::vector<Rect2D>        _clipStack;
    std::vector<UIFrameDrawItem> _items;
};

} // namespace ya
