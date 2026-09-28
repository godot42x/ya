#include "GameRuntime/GUI/GameUI/DefaultGameUIController.h"

#include "Core/Log.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "GUI/Layout/UILayout.h"

#include "Scene/Core/Scene.h"

namespace ya
{

void DefaultGameUIController::onSceneActivated(Scene& scene, GameUIHost& host)
{
    auto& attachments = _sceneAttachments[&scene];
    // Defensive: if this scene is activated again while its previous
    // attachments are still mounted (e.g. a reload path that re-activates the
    // same Scene object), unmount them first so the WidgetTree never
    // accumulates duplicates.
    for (auto& attachment : attachments) {
        attachment.detach();
    }
    attachments.clear();
    host.clearMountedRoots();
    // Single mount path shared with the editor canvas preview; the
    // controller keeps the attachments for scene-lifecycle tracking.
    std::vector<std::pair<std::string, std::weak_ptr<UIElement>>> roots;
    for (FSceneUIMount& mount : mountSceneAutoMountEntries(scene, host.getTree(), host.getDocumentStore())) {
        if (UIElementRef widget = mount.attachment.widget.lock()) {
            roots.emplace_back(mount.entryId, widget);
        }
        if (mount.attachment.valid()) {
            attachments.push_back(std::move(mount.attachment));
        }
    }
    host.setMountedRoots(std::move(roots));
}

void DefaultGameUIController::onSceneDeactivated(Scene& scene, GameUIHost& host)
{
    host.clearMountedRoots();
    auto it = _sceneAttachments.find(&scene);
    if (it == _sceneAttachments.end()) {
        return;
    }
    for (auto& attachment : it->second) {
        attachment.detach();
    }
    _sceneAttachments.erase(it);
}

WidgetAttachment DefaultGameUIController::addToWorld(Scene& world, const UIElementRef& widget, GameUIHost& host)
{
    if (!widget) {
        YA_CORE_ERROR("DefaultGameUIController::addToWorld: null widget");
        return {};
    }
    FCanvasSlotArgs rootSlot{
        .anchorMin = {0.0f, 0.0f},
        .anchorMax = {1.0f, 1.0f},
    };
    WidgetAttachment attachment = host.getTree().attachToLayer(WidgetTree::ELayer::Content,
                                                                widget, rootSlot);
    if (attachment.valid()) {
        // World-scoped: unmounted with the scene lifecycle, so re-entering a
        // world never accumulates widgets.
        _sceneAttachments[&world].push_back(attachment);
    }
    return attachment;
}

WidgetAttachment DefaultGameUIController::addToWorld(Scene& world,
                                                      const UIElementRef& widget,
                                                      const FCanvasSlotArgs& args,
                                                      GameUIHost& host)
{
    if (!widget) {
        YA_CORE_ERROR("DefaultGameUIController::addToWorld: null widget");
        return {};
    }
    WidgetAttachment attachment = host.getTree().attachToLayer(WidgetTree::ELayer::Content, widget, args);
    if (attachment.valid()) {
        _sceneAttachments[&world].push_back(attachment);
    }
    return attachment;
}

} // namespace ya
