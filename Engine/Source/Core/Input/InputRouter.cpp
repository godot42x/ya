#include "Core/Input/InputRouter.h"

#include "Core/Input/InputManager.h"
#include "Core/Log.h"
#include "Runtime/Application/App.h"

namespace ya
{

namespace
{

bool isInputEvent(const FInputEvent& event)
{
    return event.isInCategory(EEventCategory::Input);
}

} // namespace

FInputReply GameInputNode::route(FInputRouteContext& context, const FInputEvent& event)
{
    if (_inputManager && isInputEvent(event)) {
        _inputManager->processEvent(event);
    }
    return FInputReply{
        .handled = context.router.routeUnhandledInput(event),
    };
}

void GameInputNode::cancelInput(FInputRouteContext& context, EInputCancelReason reason)
{
    (void)context;
    (void)reason;
    if (_inputManager) {
        _inputManager->cancelInput();
    }
}

InputRouter::FNodeRegistration::FNodeRegistration(FNodeRegistration&& other) noexcept
    : _owner(other._owner)
    , _id(other._id)
{
    other._owner = nullptr;
    other._id    = 0;
}

InputRouter::FNodeRegistration& InputRouter::FNodeRegistration::operator=(FNodeRegistration&& other) noexcept
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

InputRouter::FNodeRegistration::~FNodeRegistration()
{
    reset();
}

void InputRouter::FNodeRegistration::reset()
{
    if (_owner && _id != 0) {
        _owner->unregisterNode(_id);
    }
    _owner = nullptr;
    _id    = 0;
}

void InputRouter::setDefaultNode(IInputNode& node)
{
    IInputNode* previousNode = getActiveNode();
    _defaultNode             = &node;
    handleNodeTransition(previousNode, getActiveNode());
}

InputRouter::FNodeRegistration InputRouter::registerNode(IInputNode& node)
{
    const uint64_t id = _nextNodeId++;
    IInputNode* previousNode = getActiveNode();
    _nodeStack.push_back({.id = id, .node = &node});
    handleNodeTransition(previousNode, getActiveNode());
    return FNodeRegistration(this, id);
}

bool InputRouter::routeEvent(const FInputEvent& event)
{
    if (!isInputEvent(event)) {
        return false;
    }

    IInputNode* node = getActiveNode();
    if (!node || !_app) {
        return false;
    }

    FInputRouteContext context = makeRouteContext();
    const FInputReply  reply   = node->route(context, event);
    applyReply(reply);
    return reply.handled;
}

bool InputRouter::routeUnhandledInput(const FInputEvent& event)
{
    if (!_app) {
        return false;
    }

    if (_app->dispatchInputModuleEvent(event)) {
        return true;
    }
    if (_app->dispatchInputFallbackEvent(event)) {
        return true;
    }
    return false;
}

void InputRouter::cancelInput(EInputCancelReason reason)
{
    applyPointerCapture({});

    if (IInputNode* node = getActiveNode(); node && _app) {
        FInputRouteContext context = makeRouteContext();
        node->cancelInput(context, reason);
    }
}

void InputRouter::unregisterNode(uint64_t id)
{
    IInputNode* previousNode = getActiveNode();

    for (auto it = _nodeStack.begin(); it != _nodeStack.end(); ++it) {
        if (it->id == id) {
            _nodeStack.erase(it);
            break;
        }
    }

    handleNodeTransition(previousNode, getActiveNode());
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
        .relative    = request.relative,
        .hideCursor  = request.hideCursor,
        .confine     = request.confine,
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
        if (IInputNode* node = getActiveNode(); node && _app) {
            FInputRouteContext context = makeRouteContext();
            node->cancelInput(context, EInputCancelReason::CaptureReleased);
        }
    }
}

void InputRouter::handleNodeTransition(IInputNode* previousNode, IInputNode* nextNode)
{
    if (previousNode == nextNode) {
        return;
    }

    applyPointerCapture({});

    if (previousNode && _app) {
        FInputRouteContext context = makeRouteContext();
        previousNode->cancelInput(context, EInputCancelReason::NodeChanged);
    }
}

FInputRouteContext InputRouter::makeRouteContext()
{
    YA_CORE_ASSERT(_app, "InputRouter requires a bound App before routing input");
    return FInputRouteContext{
        .app    = *_app,
        .router = *this,
    };
}

IInputNode* InputRouter::getActiveNode() const
{
    if (!_nodeStack.empty()) {
        return _nodeStack.back().node;
    }
    return _defaultNode;
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
