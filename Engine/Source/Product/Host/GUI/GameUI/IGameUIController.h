#pragma once

// ============================================================================
// IGameUIController - project-replaceable Game UI policy (ui-widget-tree-
// refactor Phase 3).
//
// The controller decides how worlds/scenes map to the presentation WidgetTree:
// which entries auto-mount, what happens on scene switch, how persistent UI
// is kept, and where dynamic widgets land. The default implementation mounts
// autoMount SceneWidgetEntries on scene activation and unmounts them on
// deactivation. A project can replace it for split-screen, loading screens,
// multi-world or layered UI policies.
// ============================================================================

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetAttachment.h"

#include <memory>

namespace ya
{

struct Scene;
struct GameUIHost;

struct IGameUIController
{
    virtual ~IGameUIController() = default;

    /// Scene became the active presentation world: instantiate autoMount
    /// entries into the host tree.
    virtual void onSceneActivated(Scene& scene, GameUIHost& host) = 0;

    /// Scene is no longer presented (switched away or destroyed): unmount the
    /// attachments this controller created for it.
    virtual void onSceneDeactivated(Scene& scene, GameUIHost& host) = 0;

    /// Resolve `world` to the host tree's content layer and attach `widget`.
    /// Returns an invalid attachment when the world is not currently
    /// presented (never silently mounts to another tree).
    [[nodiscard]] virtual WidgetAttachment addToWorld(Scene& world,
                                                      const UIElementRef& widget,
                                                      GameUIHost& host) = 0;
};

} // namespace ya
