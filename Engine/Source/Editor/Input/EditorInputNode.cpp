#include "Editor/Input/EditorInputNode.h"

#include "Editor/EditorLayer.h"
#include "UI/Scene/UISceneRenderer.h"
#include "Host/App.h"
#include "Host/GUI/GuiSystem.h"

namespace ya
{

namespace
{

struct FEditorInputSnapshot
{
    EventProcessState guiProcessState = EventProcessState::Continue;
    FGuiInputClaim    guiClaim{};
    bool              pointerEvent     = false;
    bool              keyboardEvent    = false;
    bool              viewportMouse    = false;
    bool              viewportKeyboard = false;
};

FEditorInputSnapshot buildSnapshot(EditorLayer& layer, const FInputEvent& event)
{
    FEditorInputSnapshot snapshot;
    snapshot.guiProcessState = GuiSystem::get().processEvent(event);
    snapshot.guiClaim        = GuiSystem::get().describeInputClaim(event);
    snapshot.pointerEvent    = event.isInCategory(EEventCategory::Mouse) ||
                            event.isInCategory(EEventCategory::MouseButton);
    snapshot.keyboardEvent   = event.isInCategory(EEventCategory::Keyboard);
    snapshot.viewportMouse   = layer.isViewportHovered() || layer.isViewportFocused();
    snapshot.viewportKeyboard = layer.isViewportFocused();
    return snapshot;
}

FInputReply routeCommandInput(FInputRouteContext& context, const FInputEvent& event)
{
    if (event.getEventType() != EEvent::KeyReleased) {
        return {};
    }

    const auto& keyEvent = static_cast<const KeyReleasedEvent&>(event);
    if (keyEvent.getKeyCode() == EKey::Escape) {
        context.app.requestQuit();
        return FInputReply{.handled = true};
    }

    if (keyEvent.getKeyCode() != EKey::K_GRAVE || !context.router.isMouseCaptured()) {
        return {};
    }

    return FInputReply{
        .handled        = true,
        .pointerCapture = FPointerCaptureRequest{},
    };
}

FInputReply routeGuiInput(const FEditorInputSnapshot& snapshot, const FInputEvent& event)
{
    if (snapshot.guiProcessState != EventProcessState::Continue || snapshot.guiClaim.wantsEvent(event)) {
        return FInputReply{.handled = true};
    }
    return {};
}

FInputReply routeCapturedViewportInput(
    App& app,
    EditorLayer& layer,
    FInputRouteContext& context,
    const FInputEvent& event)
{
    if (app.isStopped() || layer.isViewportMode2D() || !context.router.isMouseCaptured()) {
        return {};
    }

    app.getInputManager().processEvent(event);
    return FInputReply{.handled = true};
}

FInputReply routeViewportToolInput(
    App& app,
    EditorLayer& layer,
    const FEditorInputSnapshot& snapshot,
    const FInputEvent& event)
{
    // The 2D workspace always routes input to editor authoring, even during a
    // play session. The 3D workspace keeps the old rule: edit/sim authoring is
    // handled here, full runtime hands the viewport over to the game.
    if (app.isRuntimeMode() && !layer.isViewportMode2D()) {
        return {};
    }

    layer.onEvent(event);

    if (layer.isViewportMode2D()) {
        if ((snapshot.pointerEvent && snapshot.viewportMouse) ||
            (snapshot.keyboardEvent && snapshot.viewportKeyboard && !snapshot.guiClaim.text)) {
            return FInputReply{.handled = true};
        }
        return {};
    }

    if (snapshot.pointerEvent && snapshot.viewportMouse) {
        app.getInputManager().processEvent(event);
        return FInputReply{.handled = true};
    }

    if (snapshot.keyboardEvent && snapshot.viewportKeyboard && !snapshot.guiClaim.text) {
        app.getInputManager().processEvent(event);
        return FInputReply{.handled = true};
    }

    return {};
}

FInputReply routeGameplayViewportInput(
    App& app,
    EditorLayer& layer,
    const FEditorInputSnapshot& snapshot,
    const FInputEvent& event)
{
    // Game input and pointer capture only exist in full runtime (PIE).
    // Simulation keeps the editor camera and never captures the viewport mouse.
    if (!app.isRuntimeMode() || layer.isViewportMode2D()) {
        return {};
    }

    if (snapshot.pointerEvent && snapshot.viewportMouse) {
        app.getInputManager().processEvent(event);

        std::optional<FPointerCaptureRequest> pointerCapture;
        if (event.getEventType() == EEvent::MouseButtonPressed) {
            const Rect2D& mouseRect = layer.getViewportMouseRect();
            pointerCapture          = FPointerCaptureRequest{
                         .relative    = true,
                         .hideCursor  = true,
                         .confine     = mouseRect.extent.x > 0.0f && mouseRect.extent.y > 0.0f,
                         .confinement = mouseRect,
            };
        }

        return FInputReply{
            .handled        = true,
            .pointerCapture = pointerCapture,
        };
    }

    if (snapshot.keyboardEvent && snapshot.viewportKeyboard && !snapshot.guiClaim.text) {
        app.getInputManager().processEvent(event);
        return FInputReply{.handled = true};
    }

    return {};
}

FInputReply routeGameUIInput(App& app, EditorLayer& layer, const FInputEvent& event)
{
    // The 2D workspace is authoring-only: game UI stays inert so canvas
    // editing tools own every viewport event (Godot-style 2D editor).
    if (app.isStopped() || layer.isViewportMode2D()) {
        return {};
    }

    // Game UI (Node2D) picking runs before gameplay: an exclusive Stop hit
    // keeps the event away from the game (mode semantics live in App).
    const EUIRouteResult result = app.dispatchUIInputEvent(event);
    if (result == EUIRouteResult::NotHandled) {
        return {};
    }
    return FInputReply{.handled = true};
}

FInputReply routeModulePostInput(FInputRouteContext& context, const FInputEvent& event)
{
    return FInputReply{
        .handled = context.router.routeUnhandledInput(event),
    };
}

bool shouldStopRouting(const FInputReply& reply)
{
    return reply.handled || reply.pointerCapture.has_value();
}

} // namespace

void EditorInputNode::bind(App& app, EditorLayer& layer)
{
    _app   = &app;
    _layer = &layer;
}

void EditorInputNode::unbind()
{
    _layer = nullptr;
    _app   = nullptr;
}

FInputReply EditorInputNode::route(FInputRouteContext& context, const FInputEvent& event)
{
    if (!_app || !_layer) {
        return {};
    }

    const FEditorInputSnapshot snapshot = buildSnapshot(*_layer, event);
    FInputReply reply = routeCommandInput(context, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeCapturedViewportInput(*_app, *_layer, context, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeGameUIInput(*_app, *_layer, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeViewportToolInput(*_app, *_layer, snapshot, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeGameplayViewportInput(*_app, *_layer, snapshot, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeGuiInput(snapshot, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    return routeModulePostInput(context, event);
}

void EditorInputNode::cancelInput(FInputRouteContext& context, EInputCancelReason reason)
{
    (void)context;
    (void)reason;
    if (_app) {
        _app->getInputManager().cancelInput();
    }
}

} // namespace ya
