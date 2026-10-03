#pragma once

#include "Core/Common/Types.h"
#include "Core/Event.h"
#include "GUI/Widgets/WidgetTree.h"

#include <glm/glm.hpp>
#include <memory>

namespace ya
{

struct Texture;

/// Snapshot of the retained viewport host each frame. Overlays read this in
/// viewport-local coordinates (origin = top-left of the viewport image rect).
struct FEditorViewportHostState
{
    Rect2D    widgetRect{};
    glm::vec2 extent{0.0f};
    /// Device pixels per logical point. Screen overlays drawn into the view
    /// target multiply logical positions by this. Pointer hits stay logical.
    float     pixelDensity = 1.0f;
    bool      bHovered  = false;
    bool      bFocused  = false;
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};

/// Chrome widget that displays a Camera / PreviewTarget image. Layout rect is
/// not the GPU view extent. Surface pushes the image; it does not own the
/// camera render chain or the present surface.
struct IEditorViewportHost
{
    virtual ~IEditorViewportHost() = default;
    virtual void setDisplayImage(const std::shared_ptr<Texture>& texture, bool missing) = 0;

    /// Camera preview panel stacked on top of the viewport image. Viewport
    /// chrome owns it: the preview View renders into its own image and this
    /// places it, so nothing has to blit it onto the world render target (which
    /// would put every world overlay recorded afterwards on top of it).
    ///
    /// localRect is viewport-local logical pixels (origin = top-left of the
    /// viewport image), matching the overlay/gizmo convention. A null texture or
    /// a zero-extent rect collapses the panel.
    virtual void setPreviewImage(const std::shared_ptr<Texture>& texture, const Rect2D& localRect) = 0;

    [[nodiscard]] virtual Rect2D imageRect() const = 0;

    /// True when the tree-logical point is on the world image and nothing
    /// chrome-like is stacked over it there. The editor's world interaction asks
    /// this so pointer input follows the hit test the GUI router used, instead
    /// of assuming the whole viewport rect is world.
    [[nodiscard]] virtual bool isWorldPoint(const glm::vec2& logicalPoint) const = 0;

    [[nodiscard]] virtual bool isHovered() const = 0;
    [[nodiscard]] virtual bool isFocused() const = 0;
    /// Clicking the viewport image must take keyboard focus so WASD reaches
    /// the editor camera after the pointer leaves the image.
    virtual void takeKeyboardFocus() = 0;
};

struct IEditorViewportHostSink
{
    virtual ~IEditorViewportHostSink() = default;
    /// The chrome widget registers itself while it is in the tree. That edge is
    /// also the viewport's visibility: the dock detaches the widget when another
    /// tab in its stack is selected, so `nullptr` means "no viewport on screen"
    /// rather than "the rect is unchanged".
    virtual void setViewportHost(IEditorViewportHost* host) = 0;
};

/// Retained viewport overlay contract. Implementations (gizmo, selection
/// marquee, drag-drop preview) consume host state and may capture pointer input
/// while active. Rendering is owned by the editor viewport compose path.
struct IEditorViewportOverlay
{
    virtual ~IEditorViewportOverlay() = default;

    virtual void syncHost(const FEditorViewportHostState& host) = 0;

    /// `localPoint` is relative to `host.widgetRect.pos`.
    [[nodiscard]] virtual EWidgetRouteResult dispatchEvent(const Event& event,
                                                           const glm::vec2& localPoint) = 0;

  [[nodiscard]] virtual bool wantsPointerCapture() const { return false; }
  [[nodiscard]] virtual bool isActive() const { return false; }
};

/// Owns the single viewport overlay installed by editor tooling.
class EditorViewportOverlayHost
{
  public:
    void setOverlay(std::shared_ptr<IEditorViewportOverlay> overlay);
    void clearOverlay();

    void syncHost(const FEditorViewportHostState& host);

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& localPoint);
    [[nodiscard]] bool wantsPointerCapture() const;
    [[nodiscard]] bool isActive() const;

  private:
    std::shared_ptr<IEditorViewportOverlay> _overlay;
    FEditorViewportHostState                _host{};
};

} // namespace ya
