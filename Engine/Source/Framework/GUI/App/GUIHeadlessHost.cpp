#include "GUI/App/GUIHeadlessHost.h"

#include "Core/Log.h"

namespace ya
{

struct GUIHeadlessHost::FImpl
{
    FGUIHeadlessHostConfig config;
    IGUIAppDelegate*       delegate = nullptr;
    WidgetTree             tree;
    UIFrameSnapshot        lastSnapshot{};
    glm::vec2              lastPointer = {-1.0f, -1.0f};
    bool                   bInitialized = false;
    bool                   bQuitRequested = false;

    FImpl(const FGUIHeadlessHostConfig& inConfig, IGUIAppDelegate& inDelegate)
        : config(inConfig)
        , delegate(&inDelegate)
        , tree(inConfig.logicalExtent)
    {
    }
};

GUIHeadlessHost::GUIHeadlessHost(const FGUIHeadlessHostConfig& config, IGUIAppDelegate& delegate)
    : _impl(std::make_unique<FImpl>(config, delegate))
{
}

GUIHeadlessHost::~GUIHeadlessHost()
{
    shutdown();
}

bool GUIHeadlessHost::init()
{
    if (_impl->bInitialized) {
        return true;
    }
    if (_impl->config.logicalExtent.width == 0 || _impl->config.logicalExtent.height == 0) {
        YA_CORE_ERROR("GUIHeadlessHost: logical extent must be non-zero");
        return false;
    }

    _impl->tree.setLogicalExtent(_impl->config.logicalExtent);
    _impl->delegate->buildUI(_impl->tree);
    _impl->bInitialized = true;
    return true;
}

int GUIHeadlessHost::run()
{
    if (!_impl->bInitialized) {
        YA_CORE_ERROR("GUIHeadlessHost::run called before a successful init()");
        return 1;
    }
    AppKernel kernel({.eventSource = _impl->config.eventSource}, *this);
    return kernel.run(_impl->config.automation);
}

void GUIHeadlessHost::shutdown()
{
    if (!_impl) {
        return;
    }
    _impl->lastSnapshot = {};
    _impl->bInitialized = false;
}

WidgetTree& GUIHeadlessHost::getTree()
{
    return _impl->tree;
}

const UIFrameSnapshot& GUIHeadlessHost::getLastSnapshot() const
{
    return _impl->lastSnapshot;
}

void GUIHeadlessHost::injectEvent(const Event& event, const glm::vec2& logicalPoint)
{
    dispatchToTree(event, logicalPoint);
}

void GUIHeadlessHost::onInit() {}

void GUIHeadlessHost::onEvent(const Event& event)
{
    switch (event.getEventType()) {
    case EEvent::AppQuit:
    case EEvent::WindowClose:
        _impl->bQuitRequested = true;
        return;
    case EEvent::WindowResize: {
        const auto& resize = static_cast<const WindowResizeEvent&>(event);
        if (resize.GetWidth() != 0 && resize.GetHeight() != 0) {
            _impl->tree.setLogicalExtent({resize.GetWidth(), resize.GetHeight()});
        }
        return;
    }
    case EEvent::MouseMoved: {
        const auto& move = static_cast<const MouseMoveEvent&>(event);
        _impl->lastPointer = {move.getX(), move.getY()};
        dispatchToTree(event, _impl->lastPointer);
        return;
    }
    case EEvent::MouseButtonPressed:
    case EEvent::MouseButtonReleased:
    case EEvent::MouseScrolled:
        dispatchToTree(event, _impl->lastPointer);
        return;
    case EEvent::KeyPressed:
    case EEvent::KeyReleased:
    case EEvent::KeyTyped:
        dispatchToTree(event, {-1.0f, -1.0f});
        return;
    default:
        return;
    }
}

void GUIHeadlessHost::onTick(float /*dt*/)
{
    _impl->delegate->updateUI();
    _impl->lastSnapshot = _impl->tree.buildSnapshot(_impl->config.frameBuildContext);
    if (_impl->config.onSnapshot) {
        _impl->config.onSnapshot(_impl->lastSnapshot);
    }
}

void GUIHeadlessHost::onShutdown() {}

bool GUIHeadlessHost::shouldClose() const
{
    return _impl->bQuitRequested || _impl->delegate->shouldRequestClose();
}

void GUIHeadlessHost::dispatchToTree(const Event& event, const glm::vec2& logicalPoint)
{
    const EWidgetRouteResult result = _impl->tree.dispatchEvent(event, WidgetEventContext{
        .logicalPoint = logicalPoint,
    });
    _impl->delegate->onRoutedEvent(event, result);
}

} // namespace ya
