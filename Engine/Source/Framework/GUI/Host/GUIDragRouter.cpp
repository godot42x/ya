#include "GUI/Host/GUIDragRouter.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GUI/Host/GUIAppHost.h"

#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Core/Os/OsEvent.h"
#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/DragDropOperation.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"

#include <algorithm>
#include <cmath>

namespace ya
{

namespace
{

constexpr float kOverlayWidth  = 168.0f;
constexpr float kOverlayHeight = 32.0f;
constexpr int   kOverlayCursorOffset = 12;

[[nodiscard]] bool isFarPointer(glm::vec2 point)
{
    return point.x < -10000.0f || point.y < -10000.0f;
}

[[nodiscard]] bool isPointerOrKey(EEvent::T type)
{
    switch (type) {
    case EEvent::MouseMoved:
    case EEvent::MouseScrolled:
    case EEvent::MouseButtonPressed:
    case EEvent::MouseButtonReleased:
    case EEvent::KeyPressed:
    case EEvent::KeyReleased:
    case EEvent::KeyTyped:
        return true;
    default:
        return false;
    }
}

struct FDragOverlayDelegate final : IGUIAppDelegate
{
    std::string label;
    std::shared_ptr<UIText> text;

    void buildUI(WidgetTree& tree) override
    {
        auto root = std::make_shared<UIBorder>("DragOverlayRoot");
        root->setStyleKey("drag.ghost");
        FCanvasSlotArgs fill;
        fill.anchorMin = {0.0f, 0.0f};
        fill.anchorMax = {1.0f, 1.0f};
        (void)tree.attachToLayer(WidgetTree::ELayer::Content, root, fill);
        text = std::make_shared<UIText>("DragOverlayLabel");
        text->setText(label);
        text->_fontSize = 13;
        text->_hAlign    = EWidgetAlignH::Center;
        text->_vAlign    = EWidgetAlignV::Center;
        root->addDetachedChild(text);
    }

    void updateUI() override
    {
        if (text) {
            text->setText(label);
        }
    }
};

} // namespace

void GUIDragRouter::bindPrimary(uint32_t id, WidgetTree* tree, INativeWindow* native)
{
    _primaryId     = id;
    _primaryTree   = tree;
    _primaryNative = native;
}

void GUIDragRouter::bindExtras(GUIWindowManager* extras)
{
    _extras = extras;
}

void GUIDragRouter::bindRender(IRender* render)
{
    _render = render;
}

void GUIDragRouter::bindWindow(uint32_t id, WidgetTree* tree)
{
    if (id == 0 || !tree) {
        return;
    }
    _windows[id] = tree;
}

void GUIDragRouter::unbind()
{
    if (isActive()) {
        cancel();
    }
    destroyOverlay();
    setMouseCapture(false);
    _primaryId     = 0;
    _primaryTree   = nullptr;
    _primaryNative = nullptr;
    _extras        = nullptr;
    _render        = nullptr;
    _windows.clear();
    _captureWindowId = 0;
    _captureTree     = nullptr;
}

WidgetTree* GUIDragRouter::findTree(uint32_t id) const
{
    if (id == 0) {
        return nullptr;
    }
    if (const auto it = _windows.find(id); it != _windows.end()) {
        return it->second;
    }
    if (id == _primaryId) {
        return _primaryTree;
    }
    return _extras ? _extras->findTree(id) : nullptr;
}

INativeWindow* GUIDragRouter::findNative(uint32_t id) const
{
    if (id == 0) {
        return nullptr;
    }
    if (id == _primaryId) {
        return _primaryNative;
    }
    return _extras ? _extras->findNative(id) : nullptr;
}

void GUIDragRouter::forEachBoundWindow(
    const std::function<void(uint32_t, WidgetTree*, INativeWindow*)>& fn) const
{
    if (!fn) {
        return;
    }
    fn(_primaryId, _primaryTree, _primaryNative);
    for (const auto& [id, tree] : _windows) {
        fn(id, tree, nullptr);
    }
    if (_extras) {
        _extras->forEachWindow([&](uint32_t id, WidgetTree* tree, INativeWindow* native) {
            if (!isOverlayId(id)) {
                fn(id, tree, native);
            }
        });
    }
}

bool GUIDragRouter::isActive() const
{
    return _sourceTree != nullptr && _sourceTree->isDragging();
}

bool GUIDragRouter::isCaptureActive() const
{
    return _captureTree != nullptr && _captureTree->getPointerCapture() != nullptr;
}

uint32_t GUIDragRouter::sourceId() const
{
    return isActive() ? _sourceWindowId : 0;
}

uint32_t GUIDragRouter::hoverId() const
{
    return isActive() ? _hoverWindowId : 0;
}

uint32_t GUIDragRouter::captureId() const
{
    return isCaptureActive() ? _captureWindowId : 0;
}

uint32_t GUIDragRouter::modalWindowId() const
{
    return findModalWindowId();
}

uint32_t GUIDragRouter::findDraggingWindowId() const
{
    if (_primaryTree && _primaryTree->isDragging()) {
        return _primaryId;
    }
    for (const auto& [id, tree] : _windows) {
        if (tree && tree->isDragging()) {
            return id;
        }
    }
    return _extras ? _extras->findDraggingWindowId() : 0;
}

uint32_t GUIDragRouter::findCapturingWindowId() const
{
    if (_primaryTree && _primaryTree->getPointerCapture()) {
        return _primaryId;
    }
    for (const auto& [id, tree] : _windows) {
        if (tree && tree->getPointerCapture()) {
            return id;
        }
    }
    return _extras ? _extras->findCapturingWindowId() : 0;
}

uint32_t GUIDragRouter::findModalWindowId() const
{
    if (_primaryTree && _primaryTree->hasModalPopup()) {
        return _primaryId;
    }
    for (const auto& [id, tree] : _windows) {
        if (tree && tree->hasModalPopup()) {
            return id;
        }
    }
    return _extras ? _extras->findModalWindowId() : 0;
}

uint32_t GUIDragRouter::textInputWindowId() const
{
    auto wants = [this](uint32_t id) -> bool {
        WidgetTree* tree = findTree(id);
        return tree && tree->wantsTextInput();
    };

    const uint32_t modalId = findModalWindowId();
    if (modalId != 0 && wants(modalId)) {
        return modalId;
    }
    if (_extras) {
        const uint32_t extraFocus = _extras->focusedWindowId();
        if (wants(extraFocus)) {
            return extraFocus;
        }
    }
    if (wants(_primaryId)) {
        return _primaryId;
    }

    uint32_t found = 0;
    forEachBoundWindow([&](uint32_t id, WidgetTree* tree, INativeWindow*) {
        if (found == 0 && tree && tree->wantsTextInput()) {
            found = id;
        }
    });
    return found;
}

ECursorType GUIDragRouter::cursor() const
{
    if (isCaptureActive()) {
        if (const UIElement* captured = _captureTree->getPointerCapture()) {
            return captured->getCursor();
        }
    }
    WidgetTree* hover = nullptr;
    if (isActive() && _hoverTree) {
        hover = _hoverTree;
    }
    else if (_lastPointerWindow != 0) {
        hover = findTree(_lastPointerWindow);
    }
    if (!hover) {
        hover = _primaryTree;
    }
    if (hover) {
        if (const UIElement* hovered = hover->getHovered()) {
            return hovered->getCursor();
        }
        if (const UIElement* drop = hover->getDropTarget()) {
            return drop->getCursor();
        }
    }
    return ECursorType::Arrow;
}

void GUIDragRouter::runAfterDrag(std::function<void()> fn)
{
    if (!fn) {
        return;
    }
    if (!isActive()) {
        fn();
        return;
    }
    _afterDrag.push_back(std::move(fn));
}

void GUIDragRouter::runQueuedAfterDrag()
{
    auto callbacks = std::move(_afterDrag);
    _afterDrag.clear();
    for (auto& fn : callbacks) {
        if (fn) {
            fn();
        }
    }
}

void GUIDragRouter::adoptSource()
{
    if (isActive()) {
        return;
    }

    const uint32_t foundId = findDraggingWindowId();
    WidgetTree*    found   = foundId != 0 ? findTree(foundId) : nullptr;
    if (!found && _primaryTree && _primaryTree->isDragging()) {
        found = _primaryTree;
    }
    if (!found) {
        return;
    }

    _sourceWindowId = foundId != 0 ? foundId : _primaryId;
    _sourceTree     = found;
    _hoverWindowId  = _sourceWindowId;
    _hoverTree      = found;
    setMouseCapture(true);
}

void GUIDragRouter::adoptCapture()
{
    if (isCaptureActive()) {
        return;
    }

    const uint32_t foundId = findCapturingWindowId();
    WidgetTree*    found   = foundId != 0 ? findTree(foundId) : nullptr;
    if (!found && _primaryTree && _primaryTree->getPointerCapture()) {
        found = _primaryTree;
    }
    if (!found) {
        _captureWindowId = 0;
        _captureTree     = nullptr;
        return;
    }

    _captureWindowId = foundId != 0 ? foundId : _primaryId;
    _captureTree     = found;
}

void GUIDragRouter::sync()
{
    if (_sourceTree && !_sourceTree->isDragging()) {
        if (_hoverTree && _hoverTree != _sourceTree) {
            _hoverTree->clearExternalDropHover();
        }
        _sourceWindowId = 0;
        _hoverWindowId  = 0;
        _sourceTree     = nullptr;
        _hoverTree      = nullptr;
        clearDesktopOverlayState();
        runQueuedAfterDrag();
    }
    else {
        adoptSource();
        if (isActive()) {
            setMouseCapture(true);
        }
    }

    if (_captureTree && !_captureTree->getPointerCapture()) {
        _captureWindowId = 0;
        _captureTree     = nullptr;
    }
    adoptCapture();
}

void GUIDragRouter::syncTextInput()
{
    const uint32_t imeId = textInputWindowId();
    forEachBoundWindow([&](uint32_t id, WidgetTree*, INativeWindow* native) {
        if (!native) {
            return;
        }
        if (imeId != 0 && id == imeId) {
            native->startTextInput();
        }
        else {
            native->stopTextInput();
        }
    });
}

void GUIDragRouter::finish(EDragFinishResult result)
{
    if (_hoverTree && _hoverTree != _sourceTree) {
        _hoverTree->clearExternalDropHover();
    }
    const uint32_t sourceId = _sourceWindowId;
    if (_sourceTree) {
        _sourceTree->finishDrag(result);
    }
    _sourceWindowId = 0;
    _hoverWindowId  = 0;
    _sourceTree     = nullptr;
    _hoverTree      = nullptr;
    clearDesktopOverlayState();
    if (result != EDragFinishResult::Dropped) {
        if (INativeWindow* native = findNative(sourceId)) {
            if (native->isHidden()) {
                (void)native->show();
            }
        }
    }
    runQueuedAfterDrag();
}

void GUIDragRouter::cancel()
{
    if (_hoverTree && _hoverTree != _sourceTree) {
        _hoverTree->clearExternalDropHover();
    }
    restoreHiddenSourceWindow();
    if (_sourceTree) {
        _sourceTree->cancelDrag();
    }
    _sourceWindowId = 0;
    _hoverWindowId  = 0;
    _sourceTree     = nullptr;
    _hoverTree      = nullptr;
    clearDesktopOverlayState();
    runQueuedAfterDrag();
}

void GUIDragRouter::applyDeferredCloses()
{
    if (!_extras) {
        return;
    }
    if (isActive()) {
        _extras->setDeferredCloseWindows(_sourceWindowId, _hoverWindowId);
    }
    else {
        _extras->setDeferredCloseWindows(0, 0);
    }
    _extras->flushPendingCloses();
}

void GUIDragRouter::rememberPointer(const Event& event)
{
    const EEvent::T type = event.getEventType();
    if (type == EEvent::MouseMoved) {
        const auto& move   = static_cast<const MouseMoveEvent&>(event);
        _lastPointer       = {move.getX(), move.getY()};
        _lastPointerWindow = move.getWindowID();
        return;
    }
    if (type == EEvent::MouseButtonPressed || type == EEvent::MouseButtonReleased ||
        type == EEvent::MouseScrolled) {
        const uint32_t id = guiEventWindowId(event);
        if (id != 0) {
            _lastPointerWindow = id;
        }
    }
}

glm::vec2 GUIDragRouter::pointerForEvent(const Event& event) const
{
    if (event.getEventType() == EEvent::MouseMoved) {
        const auto& move = static_cast<const MouseMoveEvent&>(event);
        return {move.getX(), move.getY()};
    }
    return _lastPointer;
}

glm::vec2 GUIDragRouter::toWindowLocal(uint32_t fromId, glm::vec2 fromLocal, uint32_t toId) const
{
    if (fromId == 0 || toId == 0 || fromId == toId || isFarPointer(fromLocal)) {
        return fromLocal;
    }
    INativeWindow* from = findNative(fromId);
    INativeWindow* to   = findNative(toId);
    if (!from || !to) {
        return fromLocal;
    }
    int fx = 0;
    int fy = 0;
    int tx = 0;
    int ty = 0;
    if (!from->getWindowPosition(fx, fy) || !to->getWindowPosition(tx, ty)) {
        return fromLocal;
    }
    return {
        fromLocal.x + static_cast<float>(fx - tx),
        fromLocal.y + static_cast<float>(fy - ty),
    };
}

void GUIDragRouter::setHoverWindow(uint32_t id, WidgetTree* tree, glm::vec2 point)
{
    if (_hoverTree && _hoverTree != _sourceTree && _hoverTree != tree) {
        _hoverTree->clearExternalDropHover();
    }

    const bool bForeign = tree != nullptr && tree != _sourceTree;
    if (bForeign && id != _hoverWindowId) {
        ++_boundaryEnterCount;
    }

    _hoverWindowId = id;
    _hoverTree     = tree;

    const UIDragDropOperation* operation =
        _sourceTree ? _sourceTree->getDragOperation() : nullptr;
    if (bForeign && operation) {
        tree->setExternalDropHover(*operation, point);
    }
}

bool GUIDragRouter::routeModal(const Event& event)
{
    const uint32_t modalId = findModalWindowId();
    if (modalId == 0) {
        return false;
    }

    const EEvent::T type    = event.getEventType();
    const uint32_t  eventId = guiEventWindowId(event);
    if (!isPointerOrKey(type)) {
        return false;
    }
    if (eventId == 0) {
        WidgetTree* modalTree = findTree(modalId);
        if (!modalTree) {
            return false;
        }
        WidgetEventContext ctx;
        ctx.logicalPoint = pointerForEvent(event);
        (void)modalTree->dispatchEvent(event, ctx);
        return true;
    }
    return eventId != modalId;
}

bool GUIDragRouter::routeCapture(const Event& event)
{
    if (!isCaptureActive()) {
        return false;
    }

    const EEvent::T type    = event.getEventType();
    const uint32_t  eventId = guiEventWindowId(event);
    if (type != EEvent::MouseMoved && type != EEvent::MouseButtonReleased &&
        type != EEvent::MouseButtonPressed && type != EEvent::MouseScrolled &&
        type != EEvent::WindowMouseLeave) {
        return false;
    }

    if (type == EEvent::WindowMouseLeave) {
        return eventId == _captureWindowId;
    }

    const glm::vec2 point = pointerForEvent(event);
    if (isFarPointer(point)) {
        return eventId == _captureWindowId || eventId == 0;
    }
    if (eventId == 0 || eventId == _captureWindowId) {
        return false;
    }

    WidgetEventContext ctx;
    ctx.logicalPoint = toWindowLocal(eventId, point, _captureWindowId);
    (void)_captureTree->dispatchEvent(event, ctx);
    return true;
}

bool GUIDragRouter::routeDrag(const Event& event)
{
    const EEvent::T type    = event.getEventType();
    const uint32_t  eventId = guiEventWindowId(event);

    if (type == EEvent::KeyPressed) {
        const auto& key = static_cast<const KeyPressedEvent&>(event);
        if (key.getKeyCode() == EKey::Escape && !key.isRepeat()) {
            cancel();
            return true;
        }
        return false;
    }

    auto noteLeave = [this](uint32_t leaveId) {
        ++_boundaryLeaveCount;
        if (_hoverWindowId != leaveId) {
            return;
        }
        if (_hoverTree && _hoverTree != _sourceTree) {
            _hoverTree->clearExternalDropHover();
        }
        _hoverWindowId = 0;
        _hoverTree     = nullptr;
    };

    if (type == EEvent::WindowMouseLeave) {
        // Capture keeps delivering source-window events while the cursor is
        // already over another OS window; a leave here is not a real drop
        // cancel and must not clear the hover target.
        if (!isOverlayId(eventId) && !_bMouseCaptured) {
            noteLeave(eventId);
        }
        return true;
    }

    if (type == EEvent::MouseMoved) {
        const glm::vec2 point = pointerForEvent(event);
        if (isFarPointer(point)) {
            noteLeave(eventId);
            syncDesktopOverlay({});
            return eventId != _sourceWindowId;
        }
        const glm::vec2 screen = dragScreenPoint(eventId, point);
        const uint32_t  hit    = hitTestWindow(eventId, point);
        syncHiddenSourceWindow(hit);
        if (hit == 0) {
            if (_hoverWindowId != 0) {
                noteLeave(_hoverWindowId);
            }
            if (_sourceTree) {
                _sourceTree->setSourceDragChromeVisible(false);
            }
            _bWantsDesktopOverlay = true;
            syncDesktopOverlay(screen);
            return true;
        }
        _bWantsDesktopOverlay = false;
        destroyOverlay();
        WidgetTree* tree = findTree(hit);
        if (!tree || tree == _sourceTree) {
            if (_hoverTree && _hoverTree != _sourceTree) {
                _hoverTree->clearExternalDropHover();
            }
            _hoverWindowId = _sourceWindowId;
            _hoverTree     = _sourceTree;
            if (_sourceTree) {
                _sourceTree->setSourceDragChromeVisible(true);
            }
            return false;
        }
        if (_sourceTree) {
            _sourceTree->setSourceDragChromeVisible(false);
        }
        const glm::vec2 local = findNative(hit) ? screenToLocal(hit, screen)
                                                : toWindowLocal(eventId != 0 ? eventId : _sourceWindowId, point, hit);
        setHoverWindow(hit, tree, local);
        return true;
    }

    if (type == EEvent::MouseButtonReleased) {
        const glm::vec2 point  = pointerForEvent(event);
        const glm::vec2 screen = dragScreenPoint(eventId, point);
        const uint32_t  hit    = hitTestWindow(eventId, point);
        WidgetTree*     tree   = hit != 0 ? findTree(hit) : nullptr;
        if (!tree || tree == _sourceTree) {
            if (hit == 0) {
                finish(EDragFinishResult::NoTarget);
                return true;
            }
            return false;
        }
        const UIDragDropOperation* operation = _sourceTree->getDragOperation();
        const glm::vec2            local     = findNative(hit) ? screenToLocal(hit, screen)
                                                    : toWindowLocal(eventId != 0 ? eventId : _sourceWindowId, point, hit);
        const bool bDropped = operation && tree->dropExternal(*operation, local);
        finish(bDropped ? EDragFinishResult::Dropped : EDragFinishResult::NoTarget);
        return true;
    }

    if (type == EEvent::MouseButtonPressed) {
        WidgetTree* tree = eventId != 0 ? findTree(eventId) : nullptr;
        if (!tree || tree == _sourceTree) {
            return false;
        }
        cancel();
        return true;
    }

    return false;
}

bool GUIDragRouter::route(const Event& event)
{
    rememberPointer(event);
    sync();

    if (routeModal(event)) {
        return true;
    }
    if (isActive()) {
        return routeDrag(event);
    }
    return routeCapture(event);
}

bool GUIDragRouter::isOverlayId(uint32_t id) const
{
    return id != 0 && (_overlayId == id || (_extras && _extras->isHostOverlay(id)));
}

glm::vec2 GUIDragRouter::toScreen(uint32_t fromId, glm::vec2 fromLocal) const
{
    INativeWindow* native = findNative(fromId);
    if (!native) {
        return fromLocal;
    }
    int x = 0;
    int y = 0;
    if (!native->getWindowPosition(x, y)) {
        return fromLocal;
    }
    return {fromLocal.x + static_cast<float>(x), fromLocal.y + static_cast<float>(y)};
}

glm::vec2 GUIDragRouter::screenToLocal(uint32_t toId, glm::vec2 screen) const
{
    INativeWindow* native = findNative(toId);
    if (!native) {
        return screen;
    }
    int x = 0;
    int y = 0;
    if (!native->getWindowPosition(x, y)) {
        return screen;
    }
    return {screen.x - static_cast<float>(x), screen.y - static_cast<float>(y)};
}

glm::vec2 GUIDragRouter::dragScreenPoint(uint32_t eventId, glm::vec2 local) const
{
    if (isOverlayId(eventId)) {
        eventId = _sourceWindowId;
    }
    const bool bForeign =
        eventId != 0 && eventId != _sourceWindowId && findNative(eventId) != nullptr;
    if (bForeign) {
        return toScreen(eventId, local);
    }
    if (isActive()) {
        const FOsMouseQuery global = OsEventPump::queryGlobalMouse();
        if (global.bValid) {
            return {global.x, global.y};
        }
    }
    return toScreen(eventId != 0 ? eventId : _sourceWindowId, local);
}

uint32_t GUIDragRouter::hitTestScreen(glm::vec2 screen) const
{
    uint32_t hit = 0;
    forEachBoundWindow([&](uint32_t id, WidgetTree*, INativeWindow* native) {
        if (!native || isOverlayId(id)) {
            return;
        }
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        if (!native->getWindowPosition(x, y)) {
            return;
        }
        native->getWindowSize(w, h);
        if (native->isHidden() || native->isMinimized()) {
            return;
        }
        if (screen.x >= static_cast<float>(x) && screen.x < static_cast<float>(x + w) &&
            screen.y >= static_cast<float>(y) && screen.y < static_cast<float>(y + h)) {
            hit = id;
        }
    });
    return hit;
}

uint32_t GUIDragRouter::hitTestWindow(uint32_t eventId, glm::vec2 local) const
{
    if (isOverlayId(eventId)) {
        eventId = _sourceWindowId;
    }

    bool bHasNative = false;
    forEachBoundWindow([&](uint32_t, WidgetTree*, INativeWindow* native) {
        if (native) {
            bHasNative = true;
        }
    });
    if (bHasNative) {
        return hitTestScreen(dragScreenPoint(eventId, local));
    }

    if (eventId == 0 || isOverlayId(eventId)) {
        return 0;
    }
    if (WidgetTree* tree = findTree(eventId)) {
        const Extent2D extent = tree->getLogicalExtent();
        if (local.x < 0.0f || local.y < 0.0f ||
            local.x >= static_cast<float>(std::max(extent.width, 1u)) ||
            local.y >= static_cast<float>(std::max(extent.height, 1u))) {
            return 0;
        }
    }
    return eventId;
}

void GUIDragRouter::setMouseCapture(bool capture)
{
    if (_bMouseCaptured == capture) {
        return;
    }
    INativeWindow* native = findNative(_sourceWindowId != 0 ? _sourceWindowId : _primaryId);
    if (!native) {
        native = _primaryNative;
    }
    if (native && native->setGlobalMouseCapture(capture)) {
        _bMouseCaptured = capture;
        return;
    }
    if (!capture) {
        _bMouseCaptured = false;
    }
}

void GUIDragRouter::destroyOverlay()
{
    if (_overlayId != 0 && _extras) {
        (void)_extras->destroySession(_overlayId);
    }
    _overlayId = 0;
    _overlayDelegate.reset();
    _overlayLabel.clear();
}

void GUIDragRouter::clearDesktopOverlayState()
{
    _bWantsDesktopOverlay = false;
    destroyOverlay();
    setMouseCapture(false);
}

void GUIDragRouter::syncHiddenSourceWindow(uint32_t hit)
{
    if (!_sourceTree || _sourceWindowId == 0) {
        return;
    }
    const UIDragDropOperation* operation = _sourceTree->getDragOperation();
    if (!operation || !operation->bHideSourceWindowOnLeave) {
        return;
    }
    INativeWindow* native = findNative(_sourceWindowId);
    if (!native) {
        return;
    }
    if (hit == _sourceWindowId) {
        return;
    }
    if (!native->isHidden()) {
        (void)native->hide();
    }
}

void GUIDragRouter::restoreHiddenSourceWindow()
{
    if (_sourceWindowId == 0) {
        return;
    }
    INativeWindow* native = findNative(_sourceWindowId);
    if (native && native->isHidden()) {
        (void)native->show();
    }
}

void GUIDragRouter::syncDesktopOverlay(glm::vec2 screen)
{
    _lastScreen = screen;
    if (!_bWantsDesktopOverlay || !_extras || !isActive()) {
        destroyOverlay();
        return;
    }

    const UIDragDropOperation* operation = _sourceTree ? _sourceTree->getDragOperation() : nullptr;
    const std::string label = operation ? operation->ghostLabel : std::string{};
    if (_overlayId == 0) {
        auto delegate = std::make_unique<FDragOverlayDelegate>();
        delegate->label = label;
        FGUIWindowHostConfig config;
        config.title         = "YA Drag Overlay";
        config.width         = static_cast<uint32_t>(kOverlayWidth);
        config.height        = static_cast<uint32_t>(kOverlayHeight);
        config.bResizable    = false;
        config.bVsync        = true;
        config.bEscapeQuits  = false;
        config.bDragOverlay  = true;
        config.chromeMode    = EWindowChromeMode::ClientDrawn;
        config.bHasPosition  = true;
        config.posX          = static_cast<int>(std::lround(screen.x)) + kOverlayCursorOffset;
        config.posY          = static_cast<int>(std::lround(screen.y)) + kOverlayCursorOffset;
        const GUIWindowId id = _extras->createSession(config, *delegate, _render);
        if (id == 0) {
            return;
        }
        _overlayDelegate = std::move(delegate);
        _overlayId       = id;
        _overlayLabel    = label;
        return;
    }

    if (auto* overlay = static_cast<FDragOverlayDelegate*>(_overlayDelegate.get())) {
        overlay->label = label;
    }
    if (INativeWindow* native = _extras->findNative(_overlayId)) {
        (void)native->setWindowPosition(static_cast<int>(std::lround(screen.x)) + kOverlayCursorOffset,
                                        static_cast<int>(std::lround(screen.y)) + kOverlayCursorOffset);
    }
}

} // namespace ya
