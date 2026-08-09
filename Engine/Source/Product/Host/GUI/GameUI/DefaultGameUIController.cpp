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
        std::shared_ptr<UIDocument> document = entry.inlineDocument;
        if (!document && !entry.documentPath.empty()) {
            document = host.getDocumentResolver().load(entry.documentPath);
        }
        if (!document) {
            YA_CORE_ERROR("DefaultGameUIController: entry '{}' has no resolvable document "
                          "(path '{}')",
                          entry.entryId, entry.documentPath);
            continue;
        }

        UIElementRef widget = document->instantiate();
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
