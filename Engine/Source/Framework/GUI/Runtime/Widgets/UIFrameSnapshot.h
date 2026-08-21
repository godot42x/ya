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

#include "GUI/Widgets/Brush.h"
#include "GUI/Widgets/UIElement.h"

#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
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

    /// Host-provided monotonic generation token: bump whenever the coordinate
    /// mapping or the resource resolver changes in a way WidgetTree cannot
    /// compare itself (e.g. viewport resize, DPI change, asset/texture reload).
    /// WidgetTree compares this (and uiScale/offset) to drop draw-item caches
    /// that hold now-stale target-pixel or resolved-texture data.
    uint64_t generation = 0;

    /// Host-provided strong-resource resolver: asset path -> strong texture
    /// reference. The snapshot holds the returned shared_ptr, so draw
    /// resources stay alive through queue submit even if the asset cache
    /// unloads/clears/reloads the texture afterwards. Widgets never reach the
    /// asset layer themselves; without a resolver image textures fall back to
    /// the white sprite (documented limitation for resolver-less hosts).
    std::function<std::shared_ptr<Texture>(const std::string& assetPath)> textureResolver;
};

/// One resolved draw command (render-target pixels, top-left origin, Y down).
struct UIFrameDrawItem
{
    enum class EKind : uint8_t
    {
        Sprite,
        Text,
        Line,
    };

    EKind     kind;
    glm::vec2 pos    = {0.0f, 0.0f};
    glm::vec2 size   = {0.0f, 0.0f};
    glm::vec4 color  = {1.0f, 1.0f, 1.0f, 1.0f};
    bool      bClipped = false;
    Rect2D    clip    {}; // resolved clip rect (render-target px)

    // Sprite: null texture = white. Strong reference resolved at snapshot
    // build time: the packet keeps the texture alive through queue submit.
    std::shared_ptr<Texture> texture;
    // Text:
    std::shared_ptr<Font> font;
    std::string           text;
    glm::vec2             textScale = {1.0f, 1.0f};
    // Line (render-target px endpoints; the compose pass draws a rotated
    // thin quad of `lineThickness` width along the segment):
    glm::vec2 lineFrom      = {0.0f, 0.0f};
    glm::vec2 lineTo        = {0.0f, 0.0f};
    float     lineThickness = 1.0f;
};

/// Immutable frame packet consumed by the compose pass.
struct UIFrameSnapshot
{
    Extent2D                   logicalExtent{};
    UIFrameBuildContext        buildContext;
    std::vector<UIFrameDrawItem> items;
};

/// Accumulates resolved draw items during the pre-graph paint pass.
class YA_GUI_API UIFrameBuilder
{
  public:
    explicit UIFrameBuilder(const UIFrameBuildContext& ctx) : _ctx(ctx) {}

    /// Push a logical clip rect (intersected with the current clip).
    void pushClip(const Rect2D& logicalClip);
    void popClip();

    /// Record a sprite. `logicalRect` in tree-local logical pixels; null
    /// texture draws the white texture.
    void addSprite(const Rect2D& logicalRect, const glm::vec4& color, const std::shared_ptr<Texture>& texture);

    /// Record a brush (solid color / image / nine-patch). A solid brush has an
    /// empty resource and its tint colors the white sprite; an image brush
    /// resolves `resource` through the build context's texture resolver.
    /// NinePatch/Border currently degrade to a whole-resource stretch: UV
    /// sub-region slicing needs `uvTranslation` exposed on the public draw
    /// path (drawTextureInternal already carries it) — see Brush.h.
    void addBrush(const Rect2D& logicalRect, const FBrush& brush);

    /// Record text aligned inside `logicalRect` (h/v align via measured text).
    void addText(const Rect2D& logicalRect,
                 const std::string& text,
                 const glm::vec4& color,
                 const std::shared_ptr<Font>& font,
                 EWidgetAlignH hAlign,
                 EWidgetAlignV vAlign);

    /// Record a line segment (logical px endpoints). Drawn as a thin rotated
    /// quad of `thickness` width in the compose pass, so any angle works;
    /// honors the current clip stack like sprites/text.
    void addLine(const glm::vec2& logicalFrom,
                 const glm::vec2& logicalTo,
                 const glm::vec4& color,
                 float            thickness = 1.0f);

    /// Record a rectangle outline (4 line segments, logical rect).
    void addRectOutline(const Rect2D& logicalRect, const glm::vec4& color, float thickness = 1.0f);

    /// Record a cubic bezier approximated by `segments` line segments
    /// (client-side tessellation; the frame only carries the polyline).
    void addBezierCubic(const glm::vec2& p0,
                        const glm::vec2& c1,
                        const glm::vec2& c2,
                        const glm::vec2& p1,
                        const glm::vec4& color,
                        float            thickness = 1.0f,
                        int              segments  = 24);

    /// Move the accumulated items into an immutable snapshot.
    [[nodiscard]] UIFrameSnapshot build(Extent2D logicalExtent);

    /// Count one widget participating in the paint walk (called by
    /// UIElement::paint before painting itself). Feeds GuiPerfStats.
    void countWidget() { ++_widgetCount; }
    [[nodiscard]] uint32_t getWidgetCount() const { return _widgetCount; }
    /// Count one widget re-running its paintSelf (dirty) instead of reusing.
    void countRebuild() { ++_rebuildCount; }
    [[nodiscard]] uint32_t getRebuildCount() const { return _rebuildCount; }

    // === Reactive incremental reuse ===
    /// Bind the double-buffered per-widget draw-item caches (owned by
    /// WidgetTree). Unbound builders always re-run every widget.
    void bindCache(
        const std::unordered_map<const UIElement*, std::vector<UIFrameDrawItem>>* readCache,
        std::unordered_map<const UIElement*, std::vector<UIFrameDrawItem>>* writeCache)
    {
        _readCache  = readCache;
        _writeCache = writeCache;
    }
    [[nodiscard]] size_t getItemCount() const { return _items.size(); }
    /// Whether the read cache holds a segment for `widget` (cold-start check).
    [[nodiscard]] bool hasCachedItems(const UIElement* widget) const;
    /// Store this widget's newly painted segment into the write cache.
    void cacheItems(const UIElement* widget, size_t start);
    /// Append the widget's previous-frame segment from the read cache.
    void reuseCachedItems(const UIElement* widget);

    /// Resolve an asset path to a strong texture reference through the build
    /// context's resolver (null without a resolver or on cache miss).
    [[nodiscard]] std::shared_ptr<Texture> resolveTexture(const std::string& assetPath) const
    {
        return _ctx.textureResolver ? _ctx.textureResolver(assetPath) : nullptr;
    }

  private:
    [[nodiscard]] glm::vec2 toPx(const glm::vec2& logical) const { return _ctx.offset + logical * _ctx.uiScale; }

    const UIFrameBuildContext& _ctx;
    std::vector<Rect2D>        _clipStack;
    std::vector<UIFrameDrawItem> _items;
    uint32_t                   _widgetCount = 0;
    uint32_t                   _rebuildCount = 0;
    const std::unordered_map<const UIElement*, std::vector<UIFrameDrawItem>>* _readCache  = nullptr;
    std::unordered_map<const UIElement*, std::vector<UIFrameDrawItem>>*       _writeCache = nullptr;
};

} // namespace ya
