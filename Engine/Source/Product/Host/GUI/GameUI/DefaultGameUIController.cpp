#include "Host/GUI/GameUI/DefaultGameUIController.h"

#include "Core/Log.h"

#include "Host/GUI/GameUI/GameUIHost.h"

#include "Scene/Core/Scene.h"

namespace ya
{

void DefaultGameUIController::onSceneActivated(Scene& scene, GameUIHost& host)
{
    auto& attachments = _sceneAttachments[&scene];
    attachments.clear();

    for (const auto& entry : scene.getWidgetEntries()) {
        if (!entry.autoMount) {
            continue;
        }
        if (!entry.inlineDocument) {
            YA_CORE_WARN("DefaultGameUIController: entry '{}' references document '{}' which is not "
                         "resolved yet (yaui asset loading lands with the resource pipeline)",
                         entry.entryId, entry.documentPath);
            continue;
        }

        UIElementRef widget = entry.inlineDocument->instantiate();
        if (!widget) {
            YA_CORE_ERROR("DefaultGameUIController: entry '{}' failed to instantiate", entry.entryId);
            continue;
        }
        widget->_zOrder = entry.zOrder;
        entry.overrides.applyTo(*widget);

        WidgetAttachment attachment = host.getTree().attachToLayer(WidgetTree::ELayer::Content, widget);
        if (attachment.valid()) {
            attachments.push_back(std::move(attachment));
        }
    }
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
