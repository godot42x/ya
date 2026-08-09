#pragma once

#include "Host/GUI/GameUI/IGameUIController.h"

#include <unordered_map>
#include <vector>

namespace ya
{

/// Default policy: auto-mount every autoMount SceneWidgetEntry of the active
/// scene into the content layer (entry zOrder applies to the widget), unmount
/// them on deactivation, and attach addToWorld widgets to the content layer of
/// the currently presented world.
struct DefaultGameUIController : public IGameUIController
{
    void onSceneActivated(Scene& scene, GameUIHost& host) override;
    void onSceneDeactivated(Scene& scene, GameUIHost& host) override;
    [[nodiscard]] WidgetAttachment addToWorld(Scene& world,
                                              const UIElementRef& widget,
                                              GameUIHost& host) override;

  private:
    /// Per-scene attachments created by this controller (auto-mounted entries).
    std::unordered_map<Scene*, std::vector<WidgetAttachment>> _sceneAttachments;
};

} // namespace ya
