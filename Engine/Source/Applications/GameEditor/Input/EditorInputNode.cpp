#include "GameEditor/Input/EditorInputNode.h"

#include "Core/Input/InputManager.h"
#include "Core/KeyCode.h"
#include "Core/Os/OsEvent.h"
#include "GUI/Host/GUIDragRouter.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorWindowRegistry.h"
#include "GameRuntime/App.h"
#include "RHI/NativeWindow.h"
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
    bool               viewOverlayDragging = false;
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
        snapshot.viewOverlayDragging = session->isViewportOverlayActive();
    }
    else {
        snapshot.pointerInViewport = layer.isViewportHovered();
        snapshot.viewportFocused   = layer.isViewportFocused();
    }
    return snapshot;
}

bool isEscapeKey(const FInputEvent& event)
{
    switch (event.getEventType()) {
    case EEvent::KeyPressed:
        return static_cast<const KeyPressedEvent&>(event).getKeyCode() == EKey::Escape;
    case EEvent::KeyReleased:
        return static_cast<const KeyReleasedEvent&>(event).getKeyCode() == EKey::Escape;
    default:
        return false;
    }
}

FInputReply routeCommandInput(FInputRouteContext& context, const FInputEvent& event)
{
    if (event.getEventType() != EEvent::KeyReleased) {
        return {};
    }

    const auto& keyEvent = static_cast<const KeyReleasedEvent&>(event);
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
    const FInputEvent& event,
    bool looking,
    bool wantPointer,
    bool wantKeys)
{
    if (app.isRuntimeMode()) {
        return {};
    }

    if (snapshot.viewOverlayDragging && snapshot.pointerEvent && !looking) {
        layer.onEvent(event);
        return FInputReply{.handled = true};
    }

    layer.onEvent(event);

    if ((snapshot.pointerEvent && wantPointer) || (snapshot.keyboardEvent && wantKeys)) {
        app.getInputManager().processEvent(event);
        return FInputReply{.handled = true};
    }

    return {};
}

FInputReply routeGameplayViewportInput(
    App& app,
    EditorLayer& layer,
    FInputRouteContext& context,
    const FEditorInputSnapshot& snapshot,
    const FInputEvent& event)
{
    if (!app.isRuntimeMode()) {
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
        // The same game key step as a standalone run; an Escape the game
        // leaves goes on to the editor's Escape policy.
        const bool bConsumed = context.router.dispatchGameKey(event);
        return FInputReply{.handled = bConsumed || !isEscapeKey(event)};
    }

    return {};
}

/// Escape nothing before took: stops Play / Simulate. The editor never quits
/// on Escape, so it must not reach the standalone fallback (App quits there).
FInputReply routeEscapeInput(App& app, EditorWindowSession* session, const FInputEvent& event)
{
    if (!isEscapeKey(event)) {
        return {};
    }
    if (event.getEventType() == EEvent::KeyReleased && session && (app.isRuntimeMode() || app.isSimulationMode())) {
        (void)session->activeRoot().actions().execute("runtime.stop");
    }
    return FInputReply{.handled = true};
}

FInputReply routeGameUIInput(App& app, EditorLayer& layer, const FInputEvent& event)
{
    if (app.isStopped()) {
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

EditorWindowSession* EditorInputNode::session() const
{
    return _windows ? _windows->find(_windowId) : nullptr;
}

void EditorInputNode::syncDragRouter(uint32_t primaryNativeId, INativeWindow* native)
{
    if (!_dragRouter) {
        return;
    }
    WidgetTree* primaryTree = nullptr;
    if (_windows) {
        primaryTree = _windows->defaultSession().tree();
    }
    _dragRouter->bindPrimary(primaryNativeId, primaryTree, native);
    _dragRouter->bindExtras(_extraWindows);
    _dragRouter->adoptSource();
    _dragRouter->adoptCapture();
    _dragRouter->syncTextInput();
}

void EditorInputNode::bind(App& app,
                           EditorLayer& layer,
                           EditorWindowRegistry& windows,
                           EditorWindowId windowId,
                           GUIWindowManager* extraWindows,
                           GUIDragRouter* dragRouter)
{
    _app          = &app;
    _layer        = &layer;
    _windows      = &windows;
    _extraWindows = extraWindows;
    _dragRouter   = dragRouter;
    _windowId     = windowId;
}

void EditorInputNode::unbind()
{
    _bLooking     = false;
    _bFeedingKeys = false;
    _dragRouter   = nullptr;
    _extraWindows = nullptr;
    _windows      = nullptr;
    _windowId     = kDefaultEditorWindowId;
    _layer        = nullptr;
    _app          = nullptr;
}

FInputReply EditorInputNode::route(FInputRouteContext& context, const FInputEvent& event)
{
    if (!_app || !_layer) {
        return {};
    }

    uint32_t primaryNativeId = 0;
    INativeWindow* primaryNative = context.router.getWindow();
    if (primaryNative) {
        primaryNativeId = primaryNative->getWindowID();
    }
    syncDragRouter(primaryNativeId, primaryNative);
    if (_dragRouter && _dragRouter->route(event)) {
        _dragRouter->sync();
        return FInputReply{.handled = true};
    }

    if (_extraWindows && _extraWindows->dispatchEvent(event)) {
        syncDragRouter(primaryNativeId, primaryNative);
        if (_dragRouter) {
            _dragRouter->sync();
        }
        return FInputReply{.handled = true};
    }

    if (_dragRouter) {
        _dragRouter->sync();
    }

    InputManager& inputManager = _app->getInputManager();
    deliverMatchingRelease(inputManager, event);

    const EEvent::T eventType = event.getEventType();
    if (eventType == EEvent::MouseButtonReleased &&
        static_cast<const MouseButtonReleasedEvent&>(event).GetMouseButton() == EMouse::Right) {
        _bLooking = false;
    }

    const FEditorInputSnapshot snapshot = buildSnapshot(*_app, *_layer, session(), event);

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

    reply = routeGameplayViewportInput(*_app, *_layer, context, snapshot, event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    reply = routeEscapeInput(*_app, session(), event);
    if (shouldStopRouting(reply)) {
        return reply;
    }

    return routeModulePostInput(context, event);
}

void EditorInputNode::cancelInput(FInputRouteContext& context, EInputCancelReason reason)
{
    (void)context;

    if (reason == EInputCancelReason::PointerLeftWindow) {
        // Narrow cancel: the pointer stream stopped, the keyboard did not. Held
        // keys must survive the pointer leaving the viewport.
        reconcilePointerSessionsWithPlatform(reason);
        return;
    }

    _bLooking     = false;
    _bFeedingKeys = false;
    // Focus leaving a window is how a unique pointer drag crosses OS windows.
    // Only tear the session down when this input universe itself is ending.
    if (_dragRouter && (reason == EInputCancelReason::ModuleDetached ||
                        reason == EInputCancelReason::AppStateChanged)) {
        _dragRouter->cancel();
    }
    if (reason == EInputCancelReason::WindowFocusLost) {
        reconcilePointerSessionsWithPlatform(reason);
    }
    if (_app) {
        _app->getInputManager().cancelInput();
    }
}

void EditorInputNode::reconcilePointerSessionsWithPlatform(EInputCancelReason reason)
{
    // Key focus loss and the pointer leaving the window both end the delivery
    // of the platform's pointer stream to a session that never captured the
    // mouse. A press whose button is already physically up can therefore never
    // receive its release: the tree compares its cached session against the
    // real state and cancels the stale one, instead of holding a click that
    // never ended (what used to abort on the next press). A session whose
    // button is still held stays alive, which is how a drag crosses OS windows.
    const uint32_t osButtons = OsEventPump::queryGlobalMouse().buttonMask;
    const std::string_view cause =
        reason == EInputCancelReason::WindowFocusLost ? "editor window lost key focus"
                                                     : "pointer left the editor window";
    if (_windows) {
        _windows->forEach([&](EditorWindowSession& window) {
            if (WidgetTree* tree = window.tree()) {
                tree->reconcilePointerButtons(osButtons, cause);
            }
        });
    }
    if (_app) {
        if (GameUIHost* host = _app->getGameUIHost(); host && host->getMountedScene()) {
            host->getTree().reconcilePointerButtons(osButtons, cause);
        }
    }
}

std::optional<ECursorType> EditorInputNode::getCursor() const
{
    if (_dragRouter) {
        return _dragRouter->cursor();
    }

    EditorWindowSession* window = session();
    if (!window) {
        return std::nullopt;
    }

    if (WidgetTree* tree = window->tree()) {
        if (const UIElement* hovered = tree->getHovered()) {
            const ECursorType chrome = hovered->getCursor();
            if (chrome != ECursorType::Arrow || !window->isViewportHovered()) {
                return chrome;
            }
        }
    }

    if (window->isViewportHovered() && _app) {
        if (GameUIHost* host = _app->getGameUIHost(); host && host->getMountedScene()) {
            if (const UIElement* hovered = host->getTree().getHovered()) {
                return hovered->getCursor();
            }
        }
    }

    return ECursorType::Arrow;
}

} // namespace ya
