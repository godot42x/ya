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

#include "GUI/Layout/UILayoutTypes.h"
#include "GUI/Widgets/Brush.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/GuiTextureCatalog.h"
#include "GUI/Widgets/UIElement.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
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
///
/// `uiScale` on a WidgetTree snapshot is the final logical->target-pixel
/// factor (user zoom * tree DPI). `userZoom` keeps the host zoom that went
/// into that product so text can re-rasterize from zoom and DPI separately.
/// Ad-hoc builders leave `fontDpi` at 0 and `userZoom` at 1; their `uiScale`
/// is the whole device scale.
struct UIFrameBuildContext
{
    glm::vec2 uiScale = {1.0f, 1.0f}; // logical px -> target px (zoom * DPI inside a tree)
    glm::vec2 offset  = {0.0f, 0.0f}; // render-target px origin of logical (0,0)

    /// Host-provided monotonic generation token: bump when the texture
    /// *resolver identity* changes (tests swapping a fake resolver, host
    /// replacing the source). Everyday async ready does **not** bump this —
    /// FGuiTextureCatalog notifies only the widgets that bound that path.
    /// WidgetTree treats a generation bump as ResourceReady and drops draw-item
    /// caches. Coordinate mapping changes (uiScale/offset / DPI) are a
    /// separate BuildContextChanged path.
    uint64_t generation = 0;

    /// Tree-owned catalog wired by WidgetTree::buildSnapshot. Widgets resolve
    /// through UIFrameBuilder::resolveTextureLookup so Pending/Ready/Failed
    /// stay path-keyed. Null in ad-hoc builders; then `textureResolver` is
    /// the lookup-only fallback.
    FGuiTextureCatalog* textureCatalog = nullptr;

    /// Lookup-only fallback when no catalog/source is mounted (tests, headless
    /// hosts that inject a lambda). Product hosts set WidgetTree::setTextureSource
    /// and keep this as a cache hit helper. Non-null return is Ready; null is
    /// Pending — this lambda cannot express Failed.
    std::function<std::shared_ptr<Texture>(const std::string& assetPath)> textureResolver;

    /// Authoring-canvas ghost. When > 0, a widget whose visibility excludes it
    /// from rendering still paints, wrapped in this opacity (designer canvas:
    /// a Hidden root must read as a dimmed preview, not a blank frame).
    /// Collapsed stays unpainted even then -- it has no layout rect to ghost.
    /// 0 keeps runtime semantics (skip render entirely).
    float ghostInvisibleOpacity = 0.0f;

    /// Authoring-canvas subtree filter: return false to skip a widget and its
    /// whole subtree in this snapshot (the designer's per-node display
    /// toggles). Evaluated once per visited widget; null paints everything.
    /// The policy lives with the host -- the framework only runs the gate.
    std::function<bool(const UIElement&)> subtreePaintFilter;

    /// This tree's device pixels per logical pixel. 0 = ad-hoc builder: text
    /// follows `uiScale` alone. WidgetTree::buildSnapshot always sets it, so
    /// two trees painted in one frame each rasterize at their own size.
    /// Measure stays on the logical font; `planTextRaster` folds this with
    /// `userZoom` at draw time.
    float fontDpi = 0.0f;

    /// Host zoom before this tree folded DPI into `uiScale`. Meaningful when
    /// `fontDpi > 0`. Ad-hoc builders leave it at 1 and put the whole scale
    /// in `uiScale`.
    glm::vec2 userZoom = {1.0f, 1.0f};
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
    // Sprite UV: default (offset 0, scale 1) stretches the whole resource.
    // Nine-patch cells set a sub-rect in UV space.
    glm::vec2 uvOffset = {0.0f, 0.0f};
    glm::vec2 uvScale  = {1.0f, 1.0f};
    // Corner radius in target px for the SDF round-rect alpha branch (0 = sharp).
    float     cornerRadius = 0.0f;
    // Text:
    std::shared_ptr<Font> font;
    std::string           text;
    glm::vec2             textScale = {1.0f, 1.0f};
    // Line (render-target px endpoints; the compose pass draws a rotated
    // thin quad of `lineThickness` width along the segment):
    glm::vec2 lineFrom      = {0.0f, 0.0f};
    glm::vec2 lineTo        = {0.0f, 0.0f};
    float     lineThickness = 1.0f;
    /// Per-vertex colors for Sprite, Y-down ImGui order: TL, TR, BR, BL.
    /// Ignored unless `bPerVertexColor`. `color` stays `vertexColors[0]` for dump.
    std::array<glm::vec4, 4> vertexColors{};
    bool                     bPerVertexColor = false;
    /// Scene / viewport RTs often store unused alpha = 0. Sprite2D discards
    /// those fragments; opaque sampling keeps RGB and writes A=1.
    bool                     bOpaqueSample = false;
    bool operator==(const UIFrameDrawItem& other) const
    {
        return kind == other.kind && pos == other.pos && size == other.size && color == other.color &&
               bClipped == other.bClipped && clip.pos == other.clip.pos && clip.extent == other.clip.extent &&
               texture == other.texture && uvOffset == other.uvOffset && uvScale == other.uvScale &&
               cornerRadius == other.cornerRadius && font == other.font && text == other.text &&
               textScale == other.textScale && lineFrom == other.lineFrom && lineTo == other.lineTo &&
               lineThickness == other.lineThickness && vertexColors == other.vertexColors &&
               bPerVertexColor == other.bPerVertexColor && bOpaqueSample == other.bOpaqueSample;
    }
};

/// Immutable frame packet consumed by the compose pass.
struct UIFrameSnapshot
{
    Extent2D                   logicalExtent{};
    UIFrameBuildContext        buildContext;
    std::vector<UIFrameDrawItem> items;
};

/// Shrink `rect` on all sides so a 1px outline sits inside self-clip instead
/// of being discarded on the layout-rect edge.
[[nodiscard]] inline Rect2D insetRect(const Rect2D& rect, float amount)
{
    const float inset = std::max(amount, 0.0f);
    return Rect2D{
        .pos    = rect.pos + glm::vec2(inset),
        .extent = glm::max(rect.extent - glm::vec2(inset * 2.0f), glm::vec2(0.0f)),
    };
}

struct Font;

/// Accumulates resolved draw items during the pre-graph paint pass.
class YA_GUI_API UIFrameBuilder
{
  public:
    explicit UIFrameBuilder(const UIFrameBuildContext& ctx) : _ctx(ctx) {}

    /// Push a logical clip rect (intersected with the current clip).
    void pushClip(const Rect2D& logicalClip);
    void popClip();

    /// Inherited render transform, pushed by UIElement::paint for widgets that
    /// declare render opacity / translation / scale / tint. While active,
    /// every emitted rect, colour and clip is mapped through it, so it reaches
    /// the widget's own items AND its whole subtree (UMG RenderOpacity /
    /// RenderTransform, Godot CanvasItem modulate semantics).
    ///
    /// This is not UIOverlay (the stacked layout host). Layout rects and hit
    /// testing stay in untransformed space; the mapping is applied when draw
    /// items are emitted.
    ///
    /// The pivot is the logical-space point the scale is applied around (a
    /// widget passes its layout rect + normalized pivot). Nesting composes:
    /// the outer transform maps whatever the inner transform produced.
    ///
    /// The transform is resolved to draw items at emit time (positions, sizes,
    /// colours). Because cached draw-item segments are resolved this way, a
    /// transform change MUST invalidate the subtree - the setter does that
    /// via EUIPropertyImpact::SubtreePaintContext.
    struct FUIRenderTransform
    {
        float     opacity     = 1.0f;
        glm::vec4 tint        = {1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec2 translation = {0.0f, 0.0f};
        glm::vec2 scale       = {1.0f, 1.0f};
        glm::vec2 pivot       = {0.0f, 0.0f};
    };

    void pushRenderTransform(const FUIRenderTransform& transform);
    void popRenderTransform();

    /// Record a sprite. `logicalRect` in tree-local logical pixels; null
    /// texture draws the white texture. `uvOffset`/`uvScale` select a UV
    /// sub-rect (default = whole texture).
    void addSprite(const Rect2D&                    logicalRect,
                   const glm::vec4&                 color,
                   const std::shared_ptr<Texture>&  texture,
                   glm::vec2                        uvOffset = {0.0f, 0.0f},
                   glm::vec2                        uvScale  = {1.0f, 1.0f},
                   bool                             bOpaqueSample = false);

    /// Record a filled rounded rectangle. `cornerRadius` is in tree-local
    /// logical px (scaled to target px at compose time). Drawn via the shader's
    /// SDF round-rect alpha branch (no texture needed).
    void addRoundedRect(const Rect2D& logicalRect, const glm::vec4& color, float cornerRadius);

    /// Record a rounded surface = rounded fill + optional 1px-style border in
    /// one call. The border is drawn as an outer rounded rect with the fill
    /// inset inside it, so a themed card/panel reads as a real surface (edge
    /// definition) instead of a flat color patch. `borderThickness` 0 or a
    /// transparent `borderColor` degrades to a plain rounded fill.
    void addRoundedSurface(const Rect2D&    logicalRect,
                           const glm::vec4& fillColor,
                           const glm::vec4& borderColor,
                           float            cornerRadius,
                           float            borderThickness = 1.0f);

    /// Record a filled rect with per-corner colors (Y-down ImGui order:
    /// top-left, top-right, bottom-right, bottom-left). Compose writes four
    /// different vertex colors; the GPU interpolates. No texture.
    ///
    /// A quad is two triangles. A true 2D field (all four corners independent)
    /// shows a diagonal seam. 1D gradients are correct: keep the two vertices
    /// of each axis-aligned edge the same color (horizontal: TL==BL, TR==BR;
    /// vertical: TL==TR, BL==BR). HSV squares are two 1D layers, not one 2D quad.
    void addRectFilledMultiColor(const Rect2D&    logicalRect,
                                 const glm::vec4& colTL,
                                 const glm::vec4& colTR,
                                 const glm::vec4& colBR,
                                 const glm::vec4& colBL);

    /// Record a brush (solid color / image / nine-patch / border). A solid
    /// brush has an empty resource and its tint colors the white sprite; an
    /// image brush resolves `resource` through the build context's texture
    /// resolver. NinePatch/Border slice into UV sub-rects (1 tex px = 1
    /// logical px); missing texture size falls back to a whole-resource stretch.
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

    /// Two-segment check glyph inside `boxRect` (same geometry as UICheckBox).
    void addCheckMark(const Rect2D& boxRect, const glm::vec4& color);

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
    void countRebuild(const UIElement* widget)
    {
        ++_rebuildCount;
        YA_GUI_INSPECTOR_RECORD_REBUILD(_inspector, widget);
    }
    [[nodiscard]] uint32_t getRebuildCount() const { return _rebuildCount; }

    void bindInspector(FGuiFrameInspectorRecord* inspector) { _inspector = inspector; }

    // === Reactive incremental reuse ===
    /// Bind the double-buffered per-widget draw-item caches (owned by
    /// WidgetTree). Unbound builders always re-run every widget.
    void bindCache(
        const std::unordered_map<uint64_t, std::vector<UIFrameDrawItem>>* readCache,
        std::unordered_map<uint64_t, std::vector<UIFrameDrawItem>>* writeCache)
    {
        _readCache  = readCache;
        _writeCache = writeCache;
    }
    [[nodiscard]] size_t getItemCount() const { return _items.size(); }
    /// Whether the read cache holds a segment for `widget` (cold-start check).
    [[nodiscard]] bool hasCachedItems(const UIElement* widget) const;

    /// Ghost opacity for widgets excluded from rendering (0 = skip them, the
    /// runtime default). Read by UIElement::paint.
    [[nodiscard]] float ghostInvisibleOpacity() const { return _ctx.ghostInvisibleOpacity; }
    /// Authoring-canvas subtree filter (null = paint everything).
    [[nodiscard]] const std::function<bool(const UIElement&)>& subtreePaintFilter() const
    {
        return _ctx.subtreePaintFilter;
    }

    /// Logical-size font for measure and for the face `addText` then re-rasterizes
    /// at the device pixel size. The size argument is logical pixels; density
    /// is applied by `planTextRaster`, not by this lookup.
    std::shared_ptr<Font> getFont(const FName& fontName, uint32_t fontSize) const;

    /// The tree's device-pixel ratio when this snapshot came from a tree
    /// (nullopt on an ad-hoc builder). Helpers that resolve fonts
    /// (Style::resolveTextFont) may pass it; FontManager ignores it and
    /// `addText` is what re-rasterizes.
    [[nodiscard]] std::optional<float> fontDpi() const
    {
        if (_ctx.fontDpi > 0.0f) {
            return _ctx.fontDpi;
        }
        return std::nullopt;
    }
    /// Store this widget's newly painted segment into the write cache.
    void cacheItems(const UIElement* widget, size_t start);
    /// Append the widget's previous-frame segment from the read cache.
    void reuseCachedItems(const UIElement* widget);

    /// Path-keyed lookup (Pending / Ready / Failed). Empty path is Pending.
    [[nodiscard]] FGuiTextureLookup resolveTextureLookup(const std::string& assetPath) const;

    /// Strong texture pointer for brushes/sprites. Null on Pending/Failed.
    [[nodiscard]] std::shared_ptr<Texture> resolveTexture(const std::string& assetPath) const
    {
        return resolveTextureLookup(assetPath).texture;
    }

  private:
    /// Resolved render transform (result of composing the stack):
    /// p' = p * scale + translation, colours multiplied by tint then opacity.
    struct FUIResolvedRenderTransform
    {
        glm::vec2 scale       = {1.0f, 1.0f};
        glm::vec2 translation = {0.0f, 0.0f};
        float     opacity     = 1.0f;
        glm::vec4 tint        = {1.0f, 1.0f, 1.0f, 1.0f};
    };
    [[nodiscard]] const FUIResolvedRenderTransform& currentRenderTransform() const;
    [[nodiscard]] Rect2D    mapRenderTransformRect(const Rect2D& rect) const;
    [[nodiscard]] glm::vec2 mapRenderTransformPoint(const glm::vec2& point) const;
    [[nodiscard]] glm::vec4 mapRenderTransformColor(const glm::vec4& color) const;
    [[nodiscard]] glm::vec2 getRenderTransformScale() const;

    [[nodiscard]] glm::vec2 toPx(const glm::vec2& logical) const { return _ctx.offset + logical * _ctx.uiScale; }

    const UIFrameBuildContext& _ctx;
    std::vector<Rect2D>        _clipStack;
    std::vector<FUIResolvedRenderTransform> _renderTransformStack;
    std::vector<UIFrameDrawItem> _items;
    uint32_t                   _widgetCount = 0;
    uint32_t                   _rebuildCount = 0;
    const std::unordered_map<uint64_t, std::vector<UIFrameDrawItem>>* _readCache  = nullptr;
    std::unordered_map<uint64_t, std::vector<UIFrameDrawItem>>*       _writeCache = nullptr;
    FGuiFrameInspectorRecord*                                         _inspector = nullptr;
};

} // namespace ya
