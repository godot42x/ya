#include "GameEditor/Input/EditorInputNode.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/EditorSurface.h"
#include "GameRuntime/App.h"
#include "GUI/Widgets/WidgetTree.h"

#include <glm/glm.hpp>

namespace ya
{

namespace
{

struct FEditorInputSnapshot
{
    EWidgetRouteResult chromeResult     = EWidgetRouteResult::NotHandled;
    bool               pointerEvent     = false;
    bool               keyboardEvent    = false;
    bool               viewportMouse    = false;
    bool               viewportKeyboard = false;
    bool               viewportOverlayActive = false;
    bool               textInput        = false;
    bool               widgetTreeChrome = false;
};

FEditorInputSnapshot buildSnapshot(App& app, EditorLayer& layer, EditorSurface* surface, const FInputEvent& event)
{
    FEditorInputSnapshot snapshot;
    snapshot.pointerEvent  = event.isInCategory(EEventCategory::Mouse) ||
                            event.isInCategory(EEventCategory::MouseButton);
    snapshot.keyboardEvent = event.isInCategory(EEventCategory::Keyboard);

    glm::vec2 windowPoint = app.getLastMousePos();
    if (event.getEventType() == EEvent::MouseMoved) {
        const auto& move = static_cast<const MouseMoveEvent&>(event);
        windowPoint      = {move.getX(), move.getY()};
    }

    if (surface) {
        snapshot.widgetTreeChrome = true;
        snapshot.chromeResult     = surface->dispatchEvent(event, windowPoint);
        snapshot.textInput        = surface->wantsTextInput();
        snapshot.viewportMouse =
            surface->isViewportHovered() || surface->isViewportFocused() ||
            layer.isViewportHovered() || layer.isViewportFocused();
        snapshot.viewportKeyboard = surface->isViewportFocused() || layer.isViewportFocused();
        snapshot.viewportOverlayActive = surface->isViewportOverlayActive();
    }
    else {
        snapshot.viewportMouse   = layer.isViewportHovered() || layer.isViewportFocused();
        snapshot.viewportKeyboard = layer.isViewportFocused();
    }
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

FInputReply routeChromeInput(const FEditorInputSnapshot& snapshot)
{
    if (!snapshot.widgetTreeChrome) {
        return {};
    }
    if (snapshot.textInput && snapshot.keyboardEvent) {
        return FInputReply{.handled = true};
    }
    if (snapshot.chromeResult == EWidgetRouteResult::HandledExclusive && !snapshot.viewportMouse) {
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

    if (snapshot.viewportOverlayActive && snapshot.pointerEvent) {
        layer.onEvent(event);
        return FInputReply{.handled = true};
    }

    layer.onEvent(event);

    if (layer.isViewportMode2D()) {
        if ((snapshot.pointerEvent && snapshot.viewportMouse) ||
            (snapshot.keyboardEvent && snapshot.viewportKeyboard && !snapshot.textInput)) {
            return FInputReply{.handled = true};
        }
        return {};
    }

    if (snapshot.pointerEvent && snapshot.viewportMouse) {
        app.getInputManager().processEvent(event);
        return FInputReply{.handled = true};
    }

    if (snapshot.keyboardEvent && snapshot.viewportKeyboard && !snapshot.textInput) {
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

    if (snapshot.keyboardEvent && snapshot.viewportKeyboard && !snapshot.textInput) {
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

    // Game UI picking runs before gameplay: an exclusive Stop hit keeps the
    // event away from the game (mode semantics live in App).
    const EWidgetRouteResult result = app.dispatchUIInputEvent(event);
    if (result == EWidgetRouteResult::NotHandled) {
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

void EditorInputNode::bind(App& app, EditorLayer& layer, EditorSurface* surface)
{
    _app     = &app;
    _layer   = &layer;
    _surface = surface;
}

void EditorInputNode::unbind()
{
    _surface = nullptr;
    _layer   = nullptr;
    _app     = nullptr;
}

FInputReply EditorInputNode::route(FInputRouteContext& context, const FInputEvent& event)
{
    if (!_app || !_layer) {
        return {};
    }

    const FEditorInputSnapshot snapshot = buildSnapshot(*_app, *_layer, _surface, event);
    FInputReply reply = routeCommandInput(context, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeCapturedViewportInput(*_app, *_layer, context, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeChromeInput(snapshot);
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
