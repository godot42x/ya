#include "Host/GUI/GameUI/DefaultGameUIController.h"

#include "Core/Log.h"

#include "Host/GUI/GameUI/GameUIHost.h"

#include "Scene/Core/Scene.h"

namespace ya
{

void DefaultGameUIController::onSceneActivated(Scene& scene, GameUIHost& host)
{
    auto& attachments = _sceneAttachments[&scene];
    // Single mount path shared with the editor canvas preview; the
    // controller keeps the attachments for scene-lifecycle tracking.
    attachments = mountSceneAutoMountEntries(scene, host.getTree(), host.getDocumentResolver());
}

void DefaultGameUIController::onSceneDeactivated(Scene& scene, GameUIHost& host)
{
    (void)host;
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
    WidgetAttachment attachment = host.getTree().attachToLayer(WidgetTree::ELayer::Content, widget);
    if (attachment.valid()) {
        // World-scoped: unmounted with the scene lifecycle, so re-entering a
        // world never accumulates widgets.
        _sceneAttachments[&world].push_back(attachment);
    }
    return attachment;
}

} // namespace ya
