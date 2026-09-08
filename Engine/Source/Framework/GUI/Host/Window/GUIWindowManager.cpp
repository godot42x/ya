#include "GUI/Host/GUIWindowManager.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "GUI/Widgets/UIElement.h"
#include "Render/Resources/FontManager.h"

#include <SDL3/SDL.h>

#include <algorithm>

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
    const ECursorType cursor = hovered ? hovered->getCursor() : ECursorType::Arrow;
    static ECursorType active = ECursorType::Arrow;
    static SDL_Cursor* arrow  = nullptr;
    static SDL_Cursor* ibeam  = nullptr;
    static SDL_Cursor* resizeEW = nullptr;
    static SDL_Cursor* resizeNS = nullptr;
    if (cursor == active && arrow != nullptr) {
        return;
    }
    active = cursor;
    if (!arrow) {
        arrow    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        ibeam    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
        resizeEW = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
        resizeNS = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    }
    SDL_Cursor* sdlCursor = arrow;
    switch (cursor) {
    case ECursorType::IBeam:
        sdlCursor = ibeam;
        break;
    case ECursorType::ResizeEastWest:
        sdlCursor = resizeEW;
        break;
    case ECursorType::ResizeNorthSouth:
        sdlCursor = resizeNS;
        break;
    case ECursorType::Arrow:
    default:
        sdlCursor = arrow;
        break;
    }
    if (sdlCursor) {
        SDL_SetCursor(sdlCursor);
    }
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

GUIWindowId GUIWindowManager::create(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate)
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
        if (!slot || !slot->tree || slot->bMinimized) {
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

void GUIWindowManager::flushPendingCloses()
{
    for (size_t i = 0; i < _slots.size();) {
        if (_slots[i] && _slots[i]->bCloseRequested) {
            const GUIWindowId id = _slots[i]->id;
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
        return;
    case EEvent::WindowRestore:
        slot.bMinimized = false;
        return;
    case EEvent::WindowResize: {
        const auto& resize = static_cast<const WindowResizeEvent&>(event);
        slot.bMinimized    = resize.GetWidth() == 0 || resize.GetHeight() == 0;
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
