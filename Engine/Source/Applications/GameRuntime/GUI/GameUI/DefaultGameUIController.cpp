#include "GameRuntime/GUI/GameUI/DefaultGameUIController.h"

#include "Core/Log.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "GUI/Layout/UILayout.h"

#include "Scene/Core/Scene.h"

namespace ya
{

namespace
{

FCanvasSlotArgs makeCanvasAttachArgsFromWidget_DefaultGameUIController(const UIElement& widget)
{
    FCanvasSlotArgs args;
    args.anchorMin = widget._anchorMin;
    args.anchorMax = widget._anchorMax;
    args.offset    = widget.getPosition();
    if (widget._bAutoSize) {
        if (widget._anchorMin.x == widget._anchorMax.x) {
            args.widthSizeMode = EWidgetSizeMode::Auto;
        }
        if (widget._anchorMin.y == widget._anchorMax.y) {
            args.heightSizeMode = EWidgetSizeMode::Auto;
        }
    }
    else {
        args.fixedSize = widget.getSize();
    }
    return args;
}

} // namespace

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
    // Single mount path shared with the editor canvas preview; the
    // controller keeps the attachments for scene-lifecycle tracking.
    attachments = mountSceneAutoMountEntries(scene, host.getTree());
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
    WidgetAttachment attachment = host.getTree().attachToLayer(WidgetTree::ELayer::Content,
                                                                widget,
                                                                makeCanvasAttachArgsFromWidget_DefaultGameUIController(*widget));
    if (attachment.valid()) {
        // World-scoped: unmounted with the scene lifecycle, so re-entering a
        // world never accumulates widgets.
        _sceneAttachments[&world].push_back(attachment);
    }
    return attachment;
}

} // namespace ya
