#pragma once

#include "Render3D/Common/SceneViewDesc.h"

#include <cstdint>
#include <span>
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
    /// Resolution the host viewport renders at, in pixels. A render setting, not
    /// the window's client area and not a swapchain image: a producer that fills
    /// the host viewport declares this and the presentation pass stretches the
    /// result onto whatever surface is showing it. A producer with its own
    /// geometry (an editor panel) ignores this and declares its own rect.
    Extent2D renderResolution{};
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

    /// Who owns the Views this producer declares. The producer names itself
    /// rather than being handed an id at registration: the owner is a property
    /// of who is showing the View, and an id derived from registration order
    /// would stop being a stable key the moment somebody registered a producer
    /// earlier in the list. `App::addSceneViewProducer` asserts the registered
    /// owners stay distinct.
    [[nodiscard]] virtual SceneViewOwnerId viewOwner() const = 0;

    /// This producer's key for one of its Views. Local ids are the producer's
    /// own business; they only have to be stable for as long as the View is.
    [[nodiscard]] SceneViewKey viewKey(uint32_t local) const
    {
        return SceneViewKey{.owner = viewOwner(), .local = local};
    }

    /// The stable local ids this producer owns. A producer registers the Views
    /// behind these ids when it is attached to the app, so a tick that declares
    /// none of them hides the View without destroying it; removing the
    /// producer unregisters every id here. Dynamic per-tick declarations stay
    /// in collectSceneViews(); this list is only the View lifecycle.
    [[nodiscard]] virtual std::span<const uint32_t> ownedViewLocalIds() const = 0;

    virtual void collectSceneViews(const SceneViewCollectContext& context,
                                   SceneViewCollector&            collector) = 0;
};

} // namespace ya
