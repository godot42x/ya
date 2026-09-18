#pragma once

#include "Render3D/Common/SceneViewDesc.h"

#include <cstdint>
#include <vector>

namespace ya
{

struct Scene;

/// Per-tick inputs a view owner may read while declaring. Frame-scope facts
/// live here instead of in a global the owner would have to mutate: a declarer
/// reads the tick, it does not write it.
struct SceneViewCollectContext
{
    /// Scene the host is currently working with, or null. A producer that wants
    /// Scene content reads its own Scene; this is only the host's default.
    Scene*   activeScene = nullptr;
    /// Host viewport geometry for this tick (surface client area). Not an OS
    /// window and not a swapchain image. A producer that needs an extent
    /// derives it from this rect rather than reading a second field that could
    /// disagree with it.
    Rect2D   viewportRect{};
    uint64_t hostTick  = 0;
    float    deltaTime = 0.0f;
};

/// Sink for this tick's declarations. Owners append what they want drawn; the
/// host feeds the result to the scheduler. It carries no policy of its own:
/// "do not draw" is expressed by not declaring a view, so there is no switch
/// that hides a view somebody else already asked for.
class SceneViewCollector
{
  public:
    void declare(const SceneViewDesc& desc) { _views.push_back(desc); }

    [[nodiscard]] const std::vector<SceneViewDesc>& views() const { return _views; }

  private:
    std::vector<SceneViewDesc> _views;
};

/// One owner of a View: the game's world viewport, the editor's authoring
/// viewport, a camera preview. Each declares the views it owns for this tick
/// and nothing else, so a view's existence is decided by whoever is showing it
/// rather than by a global flag some other layer wrote.
class ISceneViewProducer
{
  public:
    virtual ~ISceneViewProducer() = default;

    virtual void collectSceneViews(const SceneViewCollectContext& context,
                                   SceneViewCollector&            collector) = 0;
};

} // namespace ya
