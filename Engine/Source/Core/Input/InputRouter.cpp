#include "Core/Input/InputRouter.h"

#include "Core/Input/InputManager.h"
#include "Core/Log.h"

namespace ya
{

namespace
{

bool isInputEvent(const FInputEvent& event)
{
    return event.isInCategory(EEventCategory::Input);
}

} // namespace

FInputReply GameInputRoot::route(const FInputEvent& event)
{
    if (_inputManager && isInputEvent(event)) {
        _inputManager->processEvent(event);
    }
    return {};
}

void GameInputRoot::cancelInput(EInputCancelReason reason)
{
    (void)reason;
    if (_inputManager) {
        _inputManager->cancelInput();
    }
}

InputRouter::FRootRegistration::FRootRegistration(FRootRegistration&& other) noexcept
    : _owner(other._owner)
    , _id(other._id)
{
    other._owner = nullptr;
    other._id    = 0;
}

InputRouter::FRootRegistration& InputRouter::FRootRegistration::operator=(FRootRegistration&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    reset();
    _owner       = other._owner;
    _id          = other._id;
    other._owner = nullptr;
    other._id    = 0;
    return *this;
}

InputRouter::FRootRegistration::~FRootRegistration()
{
    reset();
}

void InputRouter::FRootRegistration::reset()
{
    if (_owner && _id != 0) {
        _owner->unregisterRoot(_id);
    }
    _owner = nullptr;
    _id    = 0;
}

void InputRouter::setDefaultRoot(IHostInputRoot& root)
{
    IHostInputRoot* previousRoot = getActiveRoot();
    _defaultRoot                 = &root;
    handleRootTransition(previousRoot, getActiveRoot());
}

InputRouter::FRootRegistration InputRouter::registerRoot(IHostInputRoot& root)
{
    const uint64_t id = _nextRootId++;
    IHostInputRoot* previousRoot = getActiveRoot();
    _rootStack.push_back({.id = id, .root = &root});
    handleRootTransition(previousRoot, getActiveRoot());
    return FRootRegistration(this, id);
}

bool InputRouter::routeEvent(const FInputEvent& event)
{
    if (!isInputEvent(event)) {
        return false;
    }

    IHostInputRoot* root = getActiveRoot();
    if (!root) {
        return false;
    }

    const FInputReply reply = root->route(event);
    applyReply(reply);
    return reply.handled;
}

void InputRouter::cancelInput(EInputCancelReason reason)
{
    applyPointerCapture({});

    if (IHostInputRoot* root = getActiveRoot()) {
        root->cancelInput(reason);
    }
}

void InputRouter::unregisterRoot(uint64_t id)
{
    IHostInputRoot* previousRoot = getActiveRoot();

    for (auto it = _rootStack.begin(); it != _rootStack.end(); ++it) {
        if (it->id == id) {
            _rootStack.erase(it);
            break;
        }
    }

    handleRootTransition(previousRoot, getActiveRoot());
}

void InputRouter::applyReply(const FInputReply& reply)
{
    if (reply.pointerCapture.has_value()) {
        applyPointerCapture(*reply.pointerCapture);
    }
}

void InputRouter::applyPointerCapture(const FPointerCaptureRequest& request)
{
    const FPointerCaptureState nextState{
        .relative   = request.relative,
        .hideCursor = request.hideCursor,
        .confine    = request.confine,
        .confinement = request.confine ? toSDLRect(request.confinement) : SDL_Rect{0, 0, 0, 0},
    };

    const bool bWasCaptured = _pointerCapture.isCaptured();
    const bool bWillCapture = nextState.isCaptured();

    if (_window) {
        if (nextState.confine) {
            if (!SDL_SetWindowMouseRect(_window, &nextState.confinement)) {
                YA_CORE_WARN("InputRouter: failed to confine mouse to rect: {}", SDL_GetError());
            }
        }
        else if (_pointerCapture.confine) {
            if (!SDL_SetWindowMouseRect(_window, nullptr)) {
                YA_CORE_WARN("InputRouter: failed to clear mouse confinement: {}", SDL_GetError());
            }
        }

        if (_pointerCapture.relative != nextState.relative) {
            if (!SDL_SetWindowRelativeMouseMode(_window, nextState.relative)) {
                YA_CORE_WARN("InputRouter: failed to set relative mouse mode to {}: {}",
                             nextState.relative,
                             SDL_GetError());
            }
        }

        if (bWasCaptured != bWillCapture) {
            if (!SDL_SetWindowMouseGrab(_window, bWillCapture)) {
                YA_CORE_WARN("InputRouter: failed to set mouse grab to {}: {}", bWillCapture, SDL_GetError());
            }
        }
    }

    if (_pointerCapture.hideCursor != nextState.hideCursor) {
        if (nextState.hideCursor) {
            SDL_HideCursor();
        }
        else {
            SDL_ShowCursor();
        }
    }

    _pointerCapture = nextState;

    if (bWasCaptured && !bWillCapture) {
        if (IHostInputRoot* root = getActiveRoot()) {
            root->cancelInput(EInputCancelReason::CaptureReleased);
        }
    }
}

void InputRouter::handleRootTransition(IHostInputRoot* previousRoot, IHostInputRoot* nextRoot)
{
    if (previousRoot == nextRoot) {
        return;
    }

    applyPointerCapture({});

    if (previousRoot) {
        previousRoot->cancelInput(EInputCancelReason::RootChanged);
    }
}

IHostInputRoot* InputRouter::getActiveRoot() const
{
    if (!_rootStack.empty()) {
        return _rootStack.back().root;
    }
    return _defaultRoot;
}

SDL_Rect InputRouter::toSDLRect(const Rect2D& rect)
{
    return SDL_Rect{
        .x = static_cast<int>(rect.pos.x),
        .y = static_cast<int>(rect.pos.y),
        .w = static_cast<int>(rect.extent.x),
        .h = static_cast<int>(rect.extent.y),
    };
}

} // namespace ya
