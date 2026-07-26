#include "Editor/Input/EditorInputNode.h"

#include "Editor/EditorLayer.h"
#include "Runtime/Application/App.h"
#include "Runtime/GUI/GuiSystem.h"

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
    FInputRouteContext& context,
    const FInputEvent& event)
{
    if (app.isStopped() || !context.router.isMouseCaptured()) {
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
    if (!app.isStopped()) {
        return {};
    }

    layer.onEvent(event);

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
    if (app.isStopped()) {
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

    reply = routeCapturedViewportInput(*_app, context, event);
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
