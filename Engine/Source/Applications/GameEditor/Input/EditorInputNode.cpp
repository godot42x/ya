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
    EditorLayer&,
    FInputRouteContext& context,
    const FInputEvent& event)
{
    if (app.isStopped() || !context.router.isMouseCaptured()) {
        return {};
    }

    // A click in the viewport captures the pointer for look and then this
    // session owns every later key. Escape is the game's cancel key, and the
    // editor's stop-play key when the game does not take it, so it has to
    // fall through.
    if (isEscapeKey(event)) {
        return {};
    }

    // Captured Play is the game's pointer. The editor does not keep these keys.
    app.writeGameplayInput(event);
    return FInputReply{.handled = true};
}

FInputReply routeViewportToolInput(
    App& app,
    EditorLayer& layer,
    InputManager& editorInput,
    const FEditorInputSnapshot& snapshot,
    const FInputEvent& event,
    bool looking,
    bool wantPointer,
    bool wantKeys)
{
    // Play owns viewport keys until Detach. Detach uses the same path as
    // authoring: the editor camera reads InputManager, and a click picks.
    if (app.isRuntimeMode() && !layer.isRuntimeInputDetached()) {
        return {};
    }

    if (snapshot.viewOverlayDragging && snapshot.pointerEvent && !looking) {
        layer.onEvent(event);
        return FInputReply{.handled = true};
    }

    layer.onEvent(event);

    if ((snapshot.pointerEvent && wantPointer) || (snapshot.keyboardEvent && wantKeys)) {
        // Escape still stops Play. The editor camera does not consume it.
        if (app.isRuntimeMode() && isEscapeKey(event)) {
            return {};
        }
        editorInput.processEvent(event);
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
    if (!app.isRuntimeMode() || layer.isRuntimeInputDetached()) {
        return {};
    }

    if (snapshot.pointerEvent && snapshot.pointerInViewport) {
        if (app.blocksGameplayInput(event)) {
            return FInputReply{.handled = true};
        }
        app.writeGameplayInput(event);

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

    // Escape reaches the game even when the viewport widget is not the focused
    // editor control, so Play can open its own menu. A text field still wins.
    // Any other key stays on the viewport-focus gate, and an Escape the game
    // does not take falls through to the editor's stop-play policy.
    if (snapshot.keyboardEvent && !snapshot.textInput &&
        (snapshot.viewportFocused || isEscapeKey(event))) {
        if (app.blocksGameplayInput(event)) {
            FInputReply reply{.handled = true};
            // The menu is on screen now; a grabbed cursor would hide it from
            // the player. Releasing here is the same empty request grave uses.
            if (context.router.isMouseCaptured()) {
                reply.pointerCapture = FPointerCaptureRequest{};
            }
            return reply;
        }
        if (!snapshot.viewportFocused) {
            return {};
        }
        app.writeGameplayInput(event);
        const bool bConsumed = app.dispatchWorldKey(event);
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
    // Detach is for picking the world under the cursor, so a game panel must
    // not take the click first.
    if (layer.isRuntimeInputDetached() && app.isRuntimeMode()) {
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

void deliverMatchingRelease(App& app, InputManager& editorInput, const FInputEvent& event)
{
    InputManager& gameInput = app.getInputManager();
    const EEvent::T eventType = event.getEventType();
    if (eventType == EEvent::KeyReleased) {
        const EKey::T key = static_cast<const KeyReleasedEvent&>(event).getKeyCode();
        if (editorInput.isKeyPressed(key)) {
            editorInput.processEvent(event);
        }
        if (gameInput.isKeyPressed(key)) {
            app.writeGameplayInput(event);
        }
        return;
    }
    if (eventType == EEvent::MouseButtonReleased) {
        const EMouse::T button = static_cast<const MouseButtonReleasedEvent&>(event).GetMouseButton();
        if (editorInput.isMouseButtonPressed(button)) {
            editorInput.processEvent(event);
        }
        if (gameInput.isMouseButtonPressed(button)) {
            app.writeGameplayInput(event);
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
    _bLooking         = false;
    _bFeedingKeys     = false;
    _bFeedingGameKeys = false;
    _dragRouter       = nullptr;
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

    deliverMatchingRelease(*_app, _editorInput, event);

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
    // Play owns gameplay keys until Detach. The editor camera's WASD is a
    // separate state, so leaving the viewport must not clear the player's keys.
    const bool bEditorOwnsKeys = !_app->isRuntimeMode() || _layer->isRuntimeInputDetached();
    const bool feedEditorKeys  = bEditorOwnsKeys && wantKeys;

    if (_bFeedingKeys && !feedEditorKeys) {
        _editorInput.cancelHeldKeys();
    }
    _bFeedingKeys = feedEditorKeys;
    const bool feedGameKeys = !bEditorOwnsKeys && wantKeys;
    if (_bFeedingGameKeys && !feedGameKeys) {
        _app->getInputManager().cancelHeldKeys();
    }
    _bFeedingGameKeys = feedGameKeys;

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
        *_app, *_layer, _editorInput, snapshot, event, _bLooking, wantPointer, wantKeys);
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

    _bLooking         = false;
    _bFeedingKeys     = false;
    _bFeedingGameKeys = false;
    // Focus leaving a window is how a unique pointer drag crosses OS windows.
    // Only tear the session down when this input universe itself is ending.
    if (_dragRouter && (reason == EInputCancelReason::ModuleDetached ||
                        reason == EInputCancelReason::AppStateChanged)) {
        _dragRouter->cancel();
    }
    if (reason == EInputCancelReason::WindowFocusLost) {
        reconcilePointerSessionsWithPlatform(reason);
    }
    _editorInput.cancelInput();
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
