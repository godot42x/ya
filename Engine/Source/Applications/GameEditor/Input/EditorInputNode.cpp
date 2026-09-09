#include "GameEditor/Input/EditorInputNode.h"

#include "Core/Input/InputManager.h"
#include "Core/KeyCode.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/EditorWindowSession.h"
#include "GameRuntime/App.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Widgets/UIElement.h"
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
    bool               pointerInViewport = false;
    bool               viewportFocused  = false;
    bool               viewportOverlayDragging = false;
    bool               textInput        = false;
    bool               widgetTreeChrome = false;
};

FEditorInputSnapshot buildSnapshot(App& app, EditorLayer& layer, EditorWindowSession* session, const FInputEvent& event)
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

    if (session) {
        snapshot.widgetTreeChrome = true;
        snapshot.chromeResult     = session->dispatchEvent(event, windowPoint);
        snapshot.textInput        = session->wantsTextInput();
        snapshot.pointerInViewport =
            session->isPointInViewport(windowPoint) || session->isViewportHovered();
        snapshot.viewportFocused         = session->isViewportFocused();
        snapshot.viewportOverlayDragging = session->isViewportOverlayActive();
    }
    else {
        snapshot.pointerInViewport = layer.isViewportHovered();
        snapshot.viewportFocused   = layer.isViewportFocused();
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

FInputReply routeChromeInput(const FEditorInputSnapshot& snapshot, bool looking)
{
    if (!snapshot.widgetTreeChrome) {
        return {};
    }
    if (snapshot.textInput && snapshot.keyboardEvent && !looking) {
        return FInputReply{.handled = true};
    }
    // Look is an InputManager session: chrome may have already seen the event
    // (tree dispatch runs first). Exclusive chrome must not stop the camera
    // while RMB look is held, but it must stop otherwise so dock splitters /
    // inspector keep ownership of their pointer capture.
    if (snapshot.chromeResult == EWidgetRouteResult::HandledExclusive && !looking) {
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
    const FInputEvent& event,
    bool looking,
    bool wantPointer,
    bool wantKeys)
{
    if (app.isRuntimeMode() && !layer.isViewportMode2D()) {
        return {};
    }

    if (snapshot.viewportOverlayDragging && snapshot.pointerEvent && !looking) {
        layer.onEvent(event);
        return FInputReply{.handled = true};
    }

    layer.onEvent(event);

    if (layer.isViewportMode2D()) {
        if ((snapshot.pointerEvent && snapshot.pointerInViewport) ||
            (snapshot.keyboardEvent && snapshot.viewportFocused && !snapshot.textInput)) {
            return FInputReply{.handled = true};
        }
        return {};
    }

    if ((snapshot.pointerEvent && wantPointer) || (snapshot.keyboardEvent && wantKeys)) {
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
    if (!app.isRuntimeMode() || layer.isViewportMode2D()) {
        return {};
    }

    if (snapshot.pointerEvent && snapshot.pointerInViewport) {
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

    if (snapshot.keyboardEvent && snapshot.viewportFocused && !snapshot.textInput) {
        app.getInputManager().processEvent(event);
        return FInputReply{.handled = true};
    }

    return {};
}

FInputReply routeGameUIInput(App& app, EditorLayer& layer, const FInputEvent& event)
{
    if (app.isStopped() || layer.isViewportMode2D()) {
        return {};
    }

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

void deliverMatchingRelease(InputManager& inputManager, const FInputEvent& event)
{
    const EEvent::T eventType = event.getEventType();
    if (eventType == EEvent::KeyReleased) {
        const EKey::T key = static_cast<const KeyReleasedEvent&>(event).getKeyCode();
        if (inputManager.isKeyPressed(key)) {
            inputManager.processEvent(event);
        }
        return;
    }
    if (eventType == EEvent::MouseButtonReleased) {
        const EMouse::T button = static_cast<const MouseButtonReleasedEvent&>(event).GetMouseButton();
        if (inputManager.isMouseButtonPressed(button)) {
            inputManager.processEvent(event);
        }
    }
}

} // namespace

void EditorInputNode::bind(App& app, EditorLayer& layer, EditorWindowSession* session)
{
    _app     = &app;
    _layer   = &layer;
    _session = session;
}

void EditorInputNode::unbind()
{
    _bLooking     = false;
    _bFeedingKeys = false;
    _session      = nullptr;
    _layer        = nullptr;
    _app          = nullptr;
}

FInputReply EditorInputNode::route(FInputRouteContext& context, const FInputEvent& event)
{
    if (!_app || !_layer) {
        return {};
    }

    InputManager& inputManager = _app->getInputManager();
    deliverMatchingRelease(inputManager, event);

    const EEvent::T eventType = event.getEventType();
    if (eventType == EEvent::MouseButtonReleased &&
        static_cast<const MouseButtonReleasedEvent&>(event).GetMouseButton() == EMouse::Right) {
        _bLooking = false;
    }

    const FEditorInputSnapshot snapshot = buildSnapshot(*_app, *_layer, _session, event);

    if (eventType == EEvent::MouseButtonPressed) {
        const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
        if (press.GetMouseButton() == EMouse::Right && snapshot.pointerInViewport &&
            snapshot.chromeResult != EWidgetRouteResult::HandledExclusive) {
            _bLooking = true;
        }
    }

    const bool wantPointer = _bLooking || snapshot.pointerInViewport;
    const bool wantKeys =
        !snapshot.textInput && (_bLooking || snapshot.viewportFocused || snapshot.pointerInViewport);

    if (_bFeedingKeys && !wantKeys) {
        inputManager.cancelHeldKeys();
    }
    _bFeedingKeys = wantKeys;

    FInputReply reply = routeCommandInput(context, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeCapturedViewportInput(*_app, *_layer, context, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeChromeInput(snapshot, _bLooking);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeGameUIInput(*_app, *_layer, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeViewportToolInput(
        *_app, *_layer, snapshot, event, _bLooking, wantPointer, wantKeys);
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
    _bLooking     = false;
    _bFeedingKeys = false;
    if (_app) {
        _app->getInputManager().cancelInput();
    }
}

std::optional<ECursorType> EditorInputNode::getCursor() const
{
    if (!_session) {
        return std::nullopt;
    }

    if (WidgetTree* tree = _session->tree()) {
        if (const UIElement* hovered = tree->getHovered()) {
            const ECursorType chrome = hovered->getCursor();
            if (chrome != ECursorType::Arrow || !_session->isViewportHovered()) {
                return chrome;
            }
        }
    }

    if (_session->isViewportHovered() && _app) {
        if (GameUIHost* host = _app->getGameUIHost(); host && host->getMountedScene()) {
            if (const UIElement* hovered = host->getTree().getHovered()) {
                return hovered->getCursor();
            }
        }
    }

    return ECursorType::Arrow;
}

} // namespace ya
