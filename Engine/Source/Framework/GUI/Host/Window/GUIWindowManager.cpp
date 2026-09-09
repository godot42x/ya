#include "GUI/Host/GUIWindowManager.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "GUI/Widgets/UIElement.h"
#include "Core/Os/OsCursor.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "Render/Resources/FontManager.h"
#include "Render2D/Render2D.h"

#include <algorithm>
#include <format>

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

uint32_t guiEventWindowId(const Event& event)
{
    switch (event.getEventType()) {
    case EEvent::WindowClose:
    case EEvent::WindowResize:
    case EEvent::WindowRestore:
    case EEvent::WindowMinimize:
    case EEvent::WindowFocus:
    case EEvent::WindowFocusLost:
    case EEvent::WindowMoved:
    case EEvent::WindowMouseEnter:
    case EEvent::WindowMouseLeave:
        return static_cast<const WindowEvent&>(event).getWindowID();
    case EEvent::MouseMoved:
        return static_cast<const MouseMoveEvent&>(event).getWindowID();
    case EEvent::MouseScrolled:
        return static_cast<const MouseScrolledEvent&>(event).getWindowID();
    case EEvent::MouseButtonPressed:
    case EEvent::MouseButtonReleased:
        return static_cast<const MouseButtonEvent&>(event).getWindowID();
    case EEvent::KeyPressed:
    case EEvent::KeyReleased:
    case EEvent::KeyTyped:
        return static_cast<const KeyEvent&>(event).getWindowID();
    default:
        return 0;
    }
}

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
    for (auto& slot : _slots) {
        if (slot) {
            destroySlot(*slot);
        }
    }
    _slots.clear();
    _focusedId = 0;
    _nativeWindows.shutdown();
    _bInitialized = false;
}

GUIWindowId GUIWindowManager::create(const FGUIWindowHostConfig& config,
                                     IGUIAppDelegate&            delegate,
                                     IRender*                    render)
{
    if (!_bInitialized && !init()) {
        return 0;
    }

    INativeWindow* native = _nativeWindows.createWindow(WindowCreateInfo{
        .renderAPI  = ERenderAPI::Vulkan,
        .title      = config.title,
        .width      = config.width,
        .height     = config.height,
        .scale      = config.scale,
        .bResizable = config.bResizable,
    });
    if (!native) {
        return 0;
    }

    auto slot          = std::make_unique<FSlot>();
    slot->id           = native->getWindowID();
    slot->config       = config;
    slot->delegate     = &delegate;
    slot->native       = native;
    slot->tree         = std::make_unique<WidgetTree>(nativeLogicalExtent(*native));
    slot->tree->setDpiScale(native->getDpiScale());
    // Tree-local in-memory clipboard. OS clipboard stays on the primary
    // GUIWindowHost so extra windows cannot overwrite each other's paste buffer.
    delegate.buildUI(*slot->tree);
    slot->presentPassSlot   = Render2D::acquirePassSlot();
    slot->offscreenPassSlot = Render2D::acquirePassSlot();

    if (render) {
        slot->ownedPresent = render->createSurfaceContext(*native);
        if (!slot->ownedPresent) {
            YA_CORE_ERROR("GUIWindowManager: failed to create surface context for extra window '{}'",
                          config.title);
            Render2D::releasePassSlot(slot->presentPassSlot);
            Render2D::releasePassSlot(slot->offscreenPassSlot);
            slot->presentPassSlot   = kInvalidRender2DPassSlot;
            slot->offscreenPassSlot = kInvalidRender2DPassSlot;
            slot->tree.reset();
            slot->native = nullptr;
            _nativeWindows.destroyWindow(native->getWindowID());
            return 0;
        }
        slot->presentResources.render  = render;
        slot->presentResources.present = slot->ownedPresent.get();
        rebuildGuiSurfacePresentation(slot->presentResources,
                                      std::format("GUIExtra_{}", slot->id).c_str(),
                                      /*bWaitForGpu=*/true);
    }

    const GUIWindowId id = slot->id;
    _focusedId           = id;
    _slots.push_back(std::move(slot));
    return id;
}

void GUIWindowManager::requestClose(GUIWindowId id)
{
    if (FSlot* slot = findSlot(id)) {
        slot->bCloseRequested = true;
    }
}

bool GUIWindowManager::destroy(GUIWindowId id)
{
    for (auto it = _slots.begin(); it != _slots.end(); ++it) {
        if (*it && (*it)->id == id) {
            destroySlot(**it);
            _slots.erase(it);
            if (_focusedId == id) {
                _focusedId = _slots.empty() ? 0 : _slots.front()->id;
            }
            return true;
        }
    }
    return false;
}

WidgetTree* GUIWindowManager::findTree(GUIWindowId id) const
{
    const FSlot* slot = findSlot(id);
    return slot ? slot->tree.get() : nullptr;
}

INativeWindow* GUIWindowManager::findNative(GUIWindowId id) const
{
    const FSlot* slot = findSlot(id);
    return slot ? slot->native : nullptr;
}

const UIFrameSnapshot* GUIWindowManager::findSnapshot(GUIWindowId id) const
{
    const FSlot* slot = findSlot(id);
    return slot ? &slot->snapshot : nullptr;
}

void GUIWindowManager::setFocusedWindow(GUIWindowId id)
{
    if (findSlot(id)) {
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
        if (FSlot* focused = findSlot(_focusedId)) {
            dispatchToSlot(*focused, event);
            return true;
        }
        return false;
    }

    FSlot* slot = findSlot(id);
    if (!slot) {
        return false;
    }
    dispatchToSlot(*slot, event);
    return true;
}

void GUIWindowManager::tickAll(float dt)
{
    flushPendingCloses();
    for (auto& slot : _slots) {
        if (!slot || !slot->tree) {
            continue;
        }
        if (slot->native) {
            slot->tree->setLogicalExtent(nativeLogicalExtent(*slot->native));
            slot->tree->setDpiScale(slot->native->getDpiScale());
        }
        if (slot->delegate && slot->delegate->shouldRequestClose()) {
            slot->bCloseRequested = true;
            continue;
        }
        if (slot->delegate) {
            slot->delegate->updateUI();
        }
        slot->tree->tick(dt);
        FontManager::get()->setActiveDpiScale(slot->tree->getDpiScale());
        slot->snapshot = slot->tree->buildSnapshot(UIFrameBuildContext{});
    }
}

void GUIWindowManager::renderAll()
{
    for (auto& slot : _slots) {
        if (!slot || !slot->ownedPresent) {
            continue;
        }
        presentGuiSnapshot(slot->presentResources,
                           slot->snapshot,
                           slot->tree ? slot->tree->getLogicalExtent() : Extent2D{},
                           slot->presentPassSlot,
                           slot->bMinimized,
                           slot->bSwapchainRecreatePending);
    }
}

void GUIWindowManager::setDeferredCloseWindows(GUIWindowId a, GUIWindowId b)
{
    _deferCloseA = a;
    _deferCloseB = b;
}

GUIWindowId GUIWindowManager::findDraggingWindowId() const
{
    for (const auto& slot : _slots) {
        if (slot && slot->tree && slot->tree->isDragging()) {
            return slot->id;
        }
    }
    return 0;
}

void GUIWindowManager::flushPendingCloses()
{
    for (size_t i = 0; i < _slots.size();) {
        if (_slots[i] && _slots[i]->bCloseRequested) {
            const GUIWindowId id = _slots[i]->id;
            if (id != 0 && (id == _deferCloseA || id == _deferCloseB)) {
                ++i;
                continue;
            }
            destroySlot(*_slots[i]);
            _slots.erase(_slots.begin() + static_cast<std::ptrdiff_t>(i));
            if (_focusedId == id) {
                _focusedId = _slots.empty() ? 0 : _slots.front()->id;
            }
        }
        else {
            ++i;
        }
    }
}

GUIWindowManager::FSlot* GUIWindowManager::findSlot(GUIWindowId id)
{
    for (auto& slot : _slots) {
        if (slot && slot->id == id) {
            return slot.get();
        }
    }
    return nullptr;
}

const GUIWindowManager::FSlot* GUIWindowManager::findSlot(GUIWindowId id) const
{
    for (const auto& slot : _slots) {
        if (slot && slot->id == id) {
            return slot.get();
        }
    }
    return nullptr;
}

void GUIWindowManager::destroySlot(FSlot& slot)
{
    if (slot.ownedPresent) {
        slot.ownedPresent->waitInFlight();
    }
    slot.presentResources.commandBuffers.clear();
    slot.presentResources.presentationTargets.clear();
    slot.presentResources.present = nullptr;
    slot.presentResources.render  = nullptr;
    slot.ownedPresent.reset();
    Render2D::releasePassSlot(slot.presentPassSlot);
    Render2D::releasePassSlot(slot.offscreenPassSlot);
    slot.presentPassSlot   = kInvalidRender2DPassSlot;
    slot.offscreenPassSlot = kInvalidRender2DPassSlot;
    slot.snapshot = {};
    slot.tree.reset();
    slot.delegate = nullptr;
    if (slot.native) {
        const GUIWindowId id = slot.id;
        slot.native          = nullptr;
        _nativeWindows.destroyWindow(id);
    }
    slot.id = 0;
}

void GUIWindowManager::dispatchToSlot(FSlot& slot, const Event& event)
{
    switch (event.getEventType()) {
    case EEvent::WindowClose:
        slot.bCloseRequested = true;
        return;
    case EEvent::WindowFocus:
        _focusedId = slot.id;
        return;
    case EEvent::WindowFocusLost: {
        if (_focusedId == slot.id) {
            _focusedId = 0;
        }
        if (slot.tree) {
            MouseMoveEvent leave(-1000000.0f, -1000000.0f);
            leave._windowID = slot.id;
            WidgetEventContext ctx;
            ctx.logicalPoint = {leave.getX(), leave.getY()};
            slot.tree->dispatchEvent(leave, ctx);
            slot.lastMouseX = leave.getX();
            slot.lastMouseY = leave.getY();
        }
        return;
    }
    case EEvent::WindowMinimize:
        slot.bMinimized = true;
        slot.bSwapchainRecreatePending = true;
        return;
    case EEvent::WindowRestore:
        slot.bMinimized = false;
        slot.bSwapchainRecreatePending = true;
        return;
    case EEvent::WindowResize: {
        const auto& resize = static_cast<const WindowResizeEvent&>(event);
        slot.bMinimized    = resize.GetWidth() == 0 || resize.GetHeight() == 0;
        slot.bSwapchainRecreatePending = true;
        if (slot.tree) {
            slot.tree->setLogicalExtent(Extent2D{
                .width  = std::max(resize.GetWidth(), 1u),
                .height = std::max(resize.GetHeight(), 1u),
            });
        }
        if (slot.native) {
            slot.native->refreshDpiScale();
            if (slot.tree) {
                slot.tree->setDpiScale(slot.native->getDpiScale());
            }
        }
        return;
    }
    case EEvent::KeyPressed: {
        const auto& key = static_cast<const KeyPressedEvent&>(event);
        if (slot.config.bEscapeQuits && key.getKeyCode() == EKey::Escape && !key.isRepeat()) {
            slot.bCloseRequested = true;
            return;
        }
        break;
    }
    default:
        break;
    }

    if (!slot.tree) {
        return;
    }

    float mouseX = slot.lastMouseX;
    float mouseY = slot.lastMouseY;
    if (event.getEventType() == EEvent::MouseMoved) {
        const auto& move = static_cast<const MouseMoveEvent&>(event);
        mouseX           = move.getX();
        mouseY           = move.getY();
        slot.lastMouseX  = mouseX;
        slot.lastMouseY  = mouseY;
    }

    WidgetEventContext ctx;
    ctx.logicalPoint                    = {mouseX, mouseY};
    const EWidgetRouteResult result     = slot.tree->dispatchEvent(event, ctx);
    if (slot.delegate) {
        slot.delegate->onRoutedEvent(event, result);
    }
    if (event.getEventType() == EEvent::MouseMoved ||
        event.getEventType() == EEvent::MouseButtonPressed ||
        event.getEventType() == EEvent::MouseButtonReleased) {
        applyHoveredCursor(slot.tree->getHovered());
    }
}

} // namespace ya
