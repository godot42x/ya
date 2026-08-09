#include "Host/GUI/GameUI/GameUIHost.h"

#include "Core/Log.h"

#include "Host/GUI/GameUI/DefaultGameUIController.h"

#include "Scene/Core/Scene.h"

#include <algorithm>

namespace ya
{

GameUIHost::GameUIHost() : _controller(std::make_unique<DefaultGameUIController>())
{
}

GameUIHost::~GameUIHost() = default;

void GameUIHost::setPresentation(const Rect2D& viewportPx, const glm::vec2& framebufferScale)
{
    _viewportPx         = viewportPx;
    _framebufferScale   = framebufferScale;
    const float width   = std::max(viewportPx.extent.x, 1.0f) / std::max(framebufferScale.x, 0.01f);
    const float height  = std::max(viewportPx.extent.y, 1.0f) / std::max(framebufferScale.y, 0.01f);
    _tree.setLogicalExtent(Extent2D::fromVec2({width, height}));
}

void GameUIHost::setController(std::unique_ptr<IGameUIController> controller)
{
    if (!controller) {
        YA_CORE_WARN("GameUIHost::setController: null controller ignored");
        return;
    }
    _controller = std::move(controller);
}

void GameUIHost::onSceneActivated(Scene& scene)
{
    if (_mountedScene == &scene) {
        return;
    }
    if (_mountedScene) {
        onSceneDeactivated(*_mountedScene);
    }
    _mountedScene = &scene;
    _controller->onSceneActivated(scene, *this);
}

void GameUIHost::onSceneDeactivated(Scene& scene)
{
    if (_mountedScene != &scene) {
        // Not the presented world (or already unmounted): still let the
        // controller clean up anything it created for this scene.
        _controller->onSceneDeactivated(scene, *this);
        return;
    }
    _controller->onSceneDeactivated(scene, *this);
    _mountedScene = nullptr;
}

WidgetAttachment GameUIHost::addToWorld(Scene& world, const UIElementRef& widget)
{
    if (_mountedScene != &world) {
        YA_CORE_ERROR("GameUIHost::addToWorld: world '{}' is not the presented scene; "
                      "refusing to mount to another tree",
                      world.getName());
        return {};
    }
    return _controller->addToWorld(world, widget, *this);
}

EWidgetRouteResult GameUIHost::dispatchEvent(const Event& event, const glm::vec2& windowPoint)
{
    const glm::vec2 max = _viewportPx.pos + _viewportPx.extent;
    const bool bInViewport =
        windowPoint.x >= _viewportPx.pos.x && windowPoint.x <= max.x &&
        windowPoint.y >= _viewportPx.pos.y && windowPoint.y <= max.y;
    if (!bInViewport) {
        return EWidgetRouteResult::NotHandled;
    }
    const glm::vec2 logicalPoint =
        (windowPoint - _viewportPx.pos) / glm::max(_framebufferScale, glm::vec2(0.01f));
    WidgetEventContext ctx;
    ctx.logicalPoint = logicalPoint;
    return _tree.dispatchEvent(event, ctx);
}

UIFrameSnapshot GameUIHost::buildSnapshot()
{
    UIFrameBuildContext ctx{
        .uiScale = _framebufferScale,
        .offset  = _viewportPx.pos,
    };
    return _tree.buildSnapshot(ctx);
}

} // namespace ya
