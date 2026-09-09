#pragma once

#include "Core/Common/Types.h"
#include "Core/Event.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GameEditor/UI/EditorSurface.h"
#include "GameEditor/UI/EditorSurfaceContext.h"

namespace ya
{

struct App;
struct WidgetTree;
enum class EWidgetRouteResult : uint8_t;

using EditorWindowId = uint32_t;
inline constexpr EditorWindowId kDefaultEditorWindowId = 1;

/// One native editor window. ES-2 is a single default session; a second
/// session is forbidden until ES-5. The session owns the chrome Surface and
/// routes tick/events/snapshot; it does not absorb selection/undo/actions
/// (ES-3) or become a window manager.
struct EditorWindowSession
{
private:
    EditorWindowId   _windowId = kDefaultEditorWindowId;
    EditorSurface    _surface;
    EditorWindowMetrics _metrics{};

public:
    explicit EditorWindowSession(EditorWindowId windowId = kDefaultEditorWindowId)
        : _windowId(windowId)
    {
    }

    [[nodiscard]] EditorWindowId windowId() const { return _windowId; }
    [[nodiscard]] EditorSurface& surface() { return _surface; }
    [[nodiscard]] const EditorSurface& surface() const { return _surface; }
    [[nodiscard]] const EditorWindowMetrics& metrics() const { return _metrics; }

    /// Transitional App adapter (ES-5 deletes Surface::tick(App&)).
    void tick(App& app, float dt);
    void tick(const FEditorSurfaceContext& context, float dt);

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& windowPoint)
    {
        return _surface.dispatchEvent(event, windowPoint);
    }
    [[nodiscard]] const UIFrameSnapshot& snapshot() const { return _surface.snapshot(); }
    [[nodiscard]] WidgetTree* tree() const { return _surface.tree(); }
    [[nodiscard]] bool wantsTextInput() const { return _surface.wantsTextInput(); }
    [[nodiscard]] bool isViewportHovered() const { return _surface.isViewportHovered(); }
    [[nodiscard]] bool isViewportFocused() const { return _surface.isViewportFocused(); }
    [[nodiscard]] bool isViewportOverlayActive() const { return _surface.isViewportOverlayActive(); }
    [[nodiscard]] bool isPointInViewport(const glm::vec2& windowPoint) const
    {
        return _surface.isPointInViewport(windowPoint);
    }

    void shutdown() { _surface.shutdown(); }
};

} // namespace ya
