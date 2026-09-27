#include "GUI/Host/GUIWindowManager.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIWindowPlacement.h"
#include "GUI/Host/OsClipboard.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/Os/OsCursor.h"
#include "Core/Os/OsEvent.h"
#include "GUI/Host/GUIWindowPresent.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/PresentFrame.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "Render2D/Render2D.h"

#include <algorithm>
#include <format>
#include <memory>

namespace ya
{

namespace
{

Extent2D nativeLogicalExtent(INativeWindow& window)
{
    int width  = 1;
    int height = 1;
    window.getWindowSize(width, height);
    return Extent2D{
        .width  = static_cast<uint32_t>(std::max(width, 1)),
        .height = static_cast<uint32_t>(std::max(height, 1)),
    };
}

void applyHoveredCursor(const UIElement* hovered)
{
    OsCursor::set(hovered ? hovered->getCursor() : ECursorType::Arrow);
}

} // namespace

GUIWindowManager::~GUIWindowManager()
{
    shutdown();
}

bool GUIWindowManager::init()
{
    if (_bInitialized) {
        return true;
    }
    if (!_nativeWindows.init()) {
        return false;
    }
    _bInitialized = true;
    return true;
}

void GUIWindowManager::shutdown()
{
    for (auto& session : _sessions) {
        if (session) {
            destroyOwnedSession(*session);
        }
    }
    _sessions.clear();
    _focusedId = 0;
    _nativeWindows.shutdown();
    _bInitialized = false;
}

GUIWindowId GUIWindowManager::createSession(const FGUIWindowHostConfig& config,
                                            IGUIAppDelegate&            delegate,
                                            IRender*                    render)
{
    if (!_bInitialized && !init()) {
        return 0;
    }

    const EWindowChromeMode requested = config.bDragOverlay
                                            ? EWindowChromeMode::ClientDrawn
                                            : config.chromeMode.value_or(defaultWindowChromeMode());
    const EWindowChromeMode resolved  = resolveWindowChromeMode(requested);

    INativeWindow* native = _nativeWindows.createWindow(WindowCreateInfo{
        .renderAPI          = render ? render->getAPI() : config.renderAPI,
        .title              = config.title,
        .width              = config.width,
        .height             = config.height,
        .scale              = config.scale,
        .bResizable         = config.bDragOverlay ? false : config.bResizable,
        .bBorderless        = config.bDragOverlay || resolved == EWindowChromeMode::ClientDrawn,
        .bAlwaysOnTop       = config.bDragOverlay,
        .bTransparent       = config.bDragOverlay,
        .bNotFocusable      = config.bDragOverlay,
        .bUtility           = config.bDragOverlay,
        .bMousePassthrough  = config.bDragOverlay,
    });
    if (!native) {
        return 0;
    }

    auto session          = std::make_unique<GUIWindowSession>();
    session->windowId     = native->getWindowID();
    session->config       = config;
    session->delegate     = &delegate;
    session->native       = native;
    session->chromeState  = config.bDragOverlay ? FWindowChromeState{.mode = EWindowChromeMode::ClientDrawn}
                                                : applyWindowChrome(*native, requested, config.bResizable);
    if (config.bDragOverlay) {
        (void)applyNativeClickThrough(*native, true);
    }
    if (config.bHasPosition || config.bMaximized || config.monitorIndex >= 0) {
        FWindowScreenPlacement placement = queryWindowScreenPlacement(*native);
        if (config.bHasPosition) {
            placement.x          = config.posX;
            placement.y          = config.posY;
            placement.bHasOrigin = true;
        }
        placement.w            = static_cast<int>(std::max(config.width, 1u));
        placement.h            = static_cast<int>(std::max(config.height, 1u));
        if (config.monitorIndex >= 0) {
            placement.monitorIndex = config.monitorIndex;
        }
        placement.bMaximized   = config.bMaximized;
        (void)applyWindowScreenPlacement(*native, placement);
    }
    session->ownedTree    = std::make_unique<WidgetTree>(nativeLogicalExtent(*native));
    session->ownedTree->publishDpiScale(native->getDpiScale());
    bindSdlClipboard(*session->ownedTree);
    delegate.buildUI(*session->ownedTree);
    session->presentPassSlot   = Render2D::acquirePassSlot();
    session->offscreenPassSlot = Render2D::acquirePassSlot();

    if (render) {
        // Same policy the device's startup window used: a later window is a
        // second surface, not a second kind of window (see
        // makeHostWindowSurfaceDesc).
        session->surfaceId  = render->createSurfaceContext(*native, makeHostWindowSurfaceDesc(config));
        session->present    = render->findSurface(session->surfaceId);
        if (!session->present) {
            YA_CORE_ERROR("GUIWindowManager: the device cannot present to extra window '{}' (its surface was refused)",
                          config.title);
            Render2D::releasePassSlot(session->presentPassSlot);
            Render2D::releasePassSlot(session->offscreenPassSlot);
            session->presentPassSlot   = kInvalidRender2DPassSlot;
            session->offscreenPassSlot = kInvalidRender2DPassSlot;
            session->ownedTree.reset();
            session->native = nullptr;
            _nativeWindows.destroyWindow(native->getWindowID());
            return 0;
        }
        session->presentResources.render  = render;
        session->presentResources.present = session->present;
        rebuildGuiSurfacePresentation(session->presentResources,
                                      std::format("GUIExtra_{}", session->windowId).c_str(),
                                      /*bWaitForGpu=*/true);
    }

    const GUIWindowId id = session->windowId;
    if (!config.bDragOverlay) {
        _focusedId = id;
    }
    _sessions.push_back(std::move(session));
    return id;
}

void GUIWindowManager::requestClose(GUIWindowId id)
{
    if (GUIWindowSession* session = findOwnedSession(id)) {
        session->bCloseRequested = true;
    }
}

bool GUIWindowManager::destroySession(GUIWindowId id)
{
    for (auto it = _sessions.begin(); it != _sessions.end(); ++it) {
        if (*it && (*it)->windowId == id) {
            destroyOwnedSession(**it);
            _sessions.erase(it);
            if (_focusedId == id) {
                _focusedId = _sessions.empty() ? 0 : _sessions.front()->windowId;
            }
            return true;
        }
    }
    return false;
}

IGUIWindowSession* GUIWindowManager::findSession(GUIWindowId id)
{
    return findOwnedSession(id);
}

const IGUIWindowSession* GUIWindowManager::findSession(GUIWindowId id) const
{
    return findOwnedSession(id);
}

WidgetTree* GUIWindowManager::findTree(GUIWindowId id) const
{
    const GUIWindowSession* session = findOwnedSession(id);
    return session ? session->ownedTree.get() : nullptr;
}

std::unique_ptr<WidgetTree> GUIWindowManager::takeTree(GUIWindowId id)
{
    GUIWindowSession* session = findOwnedSession(id);
    if (!session) {
        return {};
    }
    return std::move(session->ownedTree);
}

bool GUIWindowManager::adoptTree(GUIWindowId id, std::unique_ptr<WidgetTree> tree)
{
    GUIWindowSession* session = findOwnedSession(id);
    if (!session || !tree) {
        return false;
    }
    session->ownedTree = std::move(tree);
    if (session->native && session->ownedTree) {
        session->ownedTree->setLogicalExtent(nativeLogicalExtent(*session->native));
        session->ownedTree->publishDpiScale(session->native->getDpiScale());
    }
    return true;
}

INativeWindow* GUIWindowManager::findNative(GUIWindowId id) const
{
    const GUIWindowSession* session = findOwnedSession(id);
    return session ? session->native : nullptr;
}

const UIFrameSnapshot* GUIWindowManager::findSnapshot(GUIWindowId id) const
{
    const GUIWindowSession* session = findOwnedSession(id);
    return session ? &session->ownedSnapshot : nullptr;
}

void GUIWindowManager::setFocusedWindow(GUIWindowId id)
{
    if (id == 0) {
        _focusedId = 0;
        return;
    }
    if (findOwnedSession(id)) {
        _focusedId = id;
    }
}

bool GUIWindowManager::dispatchEvent(const Event& event)
{
    const GUIWindowId id = guiEventWindowId(event);
    if (id == 0) {
        if (_focusedId == 0) {
            return false;
        }
        if (GUIWindowSession* focused = findOwnedSession(_focusedId)) {
            dispatchToSession(*focused, event);
            return true;
        }
        return false;
    }

    GUIWindowSession* session = findOwnedSession(id);
    if (!session) {
        return false;
    }
    if (session->isHostOverlay()) {
        return true;
    }
    dispatchToSession(*session, event);
    return true;
}

void GUIWindowManager::tickAll(float dt)
{
    flushPendingCloses();
    tickTrees(dt);
}

void GUIWindowManager::tickTrees(float dt)
{
    for (auto& session : _sessions) {
        if (!session || !session->ownedTree) {
            continue;
        }
        if (session->native) {
            session->ownedTree->setLogicalExtent(nativeLogicalExtent(*session->native));
        }
        if (session->delegate && session->delegate->shouldRequestClose()) {
            session->bCloseRequested = true;
            continue;
        }
        if (session->delegate) {
            session->delegate->updateUI();
        }
        session->ownedTree->tick(dt);
        // One DPI publish per snapshot (see WidgetTree::publishDpiScale): the
        // window's current scale when there is a window; a session without one
        // (overlay host) keeps what its tree already carries.
        session->ownedTree->publishDpiScale(session->native ? session->native->getDpiScale()
                                                            : session->ownedTree->getDpiScale());
        session->ownedSnapshot = session->ownedTree->buildSnapshot(UIFrameBuildContext{});
    }
}

void GUIWindowManager::recordAll(FFrameSubmission& submission)
{
    for (auto& session : _sessions) {
        if (!session || !session->present) {
            continue;
        }
        recordGuiSnapshot(session->presentResources,
                          session->ownedSnapshot,
                          session->ownedTree ? session->ownedTree->getLogicalExtent() : Extent2D{},
                          session->presentPassSlot,
                          session->bMinimized || (session->native && session->native->isHidden()),
                          session->bSwapchainRecreatePending,
                          submission);
    }
}

void GUIWindowManager::renderAll()
{
    FFrameSubmission submission;
    recordAll(submission);
    IRender* render = nullptr;
    for (const auto& session : _sessions) {
        if (session && session->presentResources.render) {
            render = session->presentResources.render;
            break;
        }
    }
    if (render) {
        (void)submission.submitAndPresent(*render);
    }
}

void GUIWindowManager::setDeferredCloseWindows(GUIWindowId a, GUIWindowId b)
{
    _deferCloseA = a;
    _deferCloseB = b;
}

bool GUIWindowManager::isHostOverlay(GUIWindowId id) const
{
    const GUIWindowSession* session = findOwnedSession(id);
    return session && session->isHostOverlay();
}

GUIWindowId GUIWindowManager::findDraggingWindowId() const
{
    for (const auto& session : _sessions) {
        if (session && session->ownedTree && session->ownedTree->isDragging() &&
            !session->isHostOverlay()) {
            return session->windowId;
        }
    }
    return 0;
}

GUIWindowId GUIWindowManager::findCapturingWindowId() const
{
    for (const auto& session : _sessions) {
        if (session && session->ownedTree && session->ownedTree->getPointerCapture()) {
            return session->windowId;
        }
    }
    return 0;
}

GUIWindowId GUIWindowManager::findModalWindowId() const
{
    for (const auto& session : _sessions) {
        if (session && session->ownedTree && session->ownedTree->hasModalPopup()) {
            return session->windowId;
        }
    }
    return 0;
}

void GUIWindowManager::forEachWindow(
    const std::function<void(GUIWindowId, WidgetTree*, INativeWindow*)>& fn) const
{
    if (!fn) {
        return;
    }
    for (const auto& session : _sessions) {
        if (session) {
            fn(session->windowId, session->ownedTree.get(), session->native);
        }
    }
}

void GUIWindowManager::forEachSession(const std::function<void(IGUIWindowSession&)>& fn) const
{
    if (!fn) {
        return;
    }
    for (const auto& session : _sessions) {
        if (session) {
            fn(*session);
        }
    }
}

void GUIWindowManager::flushPendingCloses()
{
    for (size_t i = 0; i < _sessions.size();) {
        if (_sessions[i] && _sessions[i]->bCloseRequested) {
            const GUIWindowId id = _sessions[i]->windowId;
            if (id != 0 && (id == _deferCloseA || id == _deferCloseB)) {
                ++i;
                continue;
            }
            destroyOwnedSession(*_sessions[i]);
            _sessions.erase(_sessions.begin() + static_cast<std::ptrdiff_t>(i));
            if (_focusedId == id) {
                _focusedId = _sessions.empty() ? 0 : _sessions.front()->windowId;
            }
        }
        else {
            ++i;
        }
    }
}

GUIWindowSession* GUIWindowManager::findOwnedSession(GUIWindowId id)
{
    for (auto& session : _sessions) {
        if (session && session->windowId == id) {
            return session.get();
        }
    }
    return nullptr;
}

const GUIWindowSession* GUIWindowManager::findOwnedSession(GUIWindowId id) const
{
    for (const auto& session : _sessions) {
        if (session && session->windowId == id) {
            return session.get();
        }
    }
    return nullptr;
}

void GUIWindowManager::destroyOwnedSession(GUIWindowSession& session)
{
    // The device owns the surface, so this releases it rather than letting a
    // member destructor decide when a swapchain may be torn down. It happens
    // after the present resources that reference the surface's images are gone
    // and after this window's in-flight work has completed -- the ordering the
    // teardown rules require (`app_teardown_order_and_instance_lock`).
    IRender* render = session.presentResources.render;
    if (session.present) {
        session.present->waitInFlight();
    }
    session.presentResources.commandBuffers.clear();
    session.presentResources.presentationTargets.clear();
    session.presentResources.present = nullptr;
    session.presentResources.render  = nullptr;
    if (render && session.surfaceId.valid()) {
        (void)render->destroySurfaceContext(session.surfaceId);
    }
    session.present   = nullptr;
    session.surfaceId = {};
    Render2D::releasePassSlot(session.presentPassSlot);
    Render2D::releasePassSlot(session.offscreenPassSlot);
    session.presentPassSlot   = kInvalidRender2DPassSlot;
    session.offscreenPassSlot = kInvalidRender2DPassSlot;
    session.ownedSnapshot     = {};
    session.ownedTree.reset();
    session.delegate = nullptr;
    if (session.native) {
        const GUIWindowId id = session.windowId;
        clearWindowChrome(*session.native);
        session.native       = nullptr;
        _nativeWindows.destroyWindow(id);
    }
    session.windowId = 0;
}

void GUIWindowManager::dispatchToSession(GUIWindowSession& session, const Event& event)
{
    switch (event.getEventType()) {
    case EEvent::WindowClose:
        session.bCloseRequested = true;
        return;
    case EEvent::WindowFocus:
        _focusedId = session.windowId;
        return;
    case EEvent::WindowFocusLost: {
        if (_focusedId == session.windowId) {
            _focusedId = 0;
        }
        if (session.ownedTree) {
            // Capture/drag may outlive this window's key-focus (cross-window
            // drag). Hover, tooltip and ordinary pointer-over must not.
            session.ownedTree->clearPointerOverState();
            // The platform stops delivering this pointer stream to a window
            // that lost key focus. The physical button state decides whether a
            // cached press can still be completed or is already over: a
            // release we will never receive must not poison the next click.
            session.ownedTree->reconcilePointerButtons(OsEventPump::queryGlobalMouse().buttonMask,
                                                      "window lost key focus");
            if (session.ownedTree->isDragging() || session.ownedTree->getPointerCapture()) {
                return;
            }
            MouseMoveEvent leave(-1000000.0f, -1000000.0f);
            leave._windowID = session.windowId;
            WidgetEventContext ctx;
            ctx.logicalPoint = {leave.getX(), leave.getY()};
            (void)session.ownedTree->dispatchEvent(leave, ctx);
            session.lastMouseX = leave.getX();
            session.lastMouseY = leave.getY();
        }
        return;
    }
    case EEvent::WindowMinimize:
        session.bMinimized                = true;
        session.bSwapchainRecreatePending = true;
        return;
    case EEvent::WindowRestore:
        session.bMinimized                = false;
        session.bSwapchainRecreatePending = true;
        return;
    case EEvent::WindowResize: {
        const auto& resize                 = static_cast<const WindowResizeEvent&>(event);
        session.bMinimized                 = resize.GetWidth() == 0 || resize.GetHeight() == 0;
        session.bSwapchainRecreatePending  = true;
        if (session.ownedTree) {
            session.ownedTree->setLogicalExtent(Extent2D{
                .width  = std::max(resize.GetWidth(), 1u),
                .height = std::max(resize.GetHeight(), 1u),
            });
        }
        if (session.native) {
            session.native->refreshDpiScale();
            session.chromeState = applyWindowChrome(*session.native, session.chromeState.mode, session.config.bResizable);
            if (session.ownedTree) {
                session.ownedTree->publishDpiScale(session.native->getDpiScale());
            }
        }
        return;
    }
    case EEvent::KeyPressed: {
        const auto& key = static_cast<const KeyPressedEvent&>(event);
        if (session.config.bEscapeQuits && key.getKeyCode() == EKey::Escape && !key.isRepeat()) {
            session.bCloseRequested = true;
            return;
        }
        break;
    }
    default:
        break;
    }

    if (!session.ownedTree) {
        return;
    }

    float mouseX = session.lastMouseX;
    float mouseY = session.lastMouseY;
    if (event.getEventType() == EEvent::MouseMoved) {
        const auto& move = static_cast<const MouseMoveEvent&>(event);
        mouseX           = move.getX();
        mouseY           = move.getY();
        session.lastMouseX = mouseX;
        session.lastMouseY = mouseY;
    }

    if (event.getEventType() == EEvent::MouseButtonPressed && session.native) {
        const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
        if (press.GetMouseButton() == EMouse::Left && press.clickCount() >= 2 &&
            handleWindowChromeTitleDoubleClick(*session.native, mouseX, mouseY)) {
            return;
        }
    }

    WidgetEventContext ctx;
    ctx.logicalPoint                = {mouseX, mouseY};
    const EWidgetRouteResult result = session.ownedTree->dispatchEvent(event, ctx);
    if (session.delegate) {
        session.delegate->onRoutedEvent(event, result);
    }
    if (event.getEventType() == EEvent::MouseMoved ||
        event.getEventType() == EEvent::MouseButtonPressed ||
        event.getEventType() == EEvent::MouseButtonReleased) {
        applyHoveredCursor(session.ownedTree->getHovered());
    }
}

} // namespace ya
