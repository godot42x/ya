#pragma once

#include "Core/Base.h"
#include "Render3D/Common/SceneViewProducer.h"

namespace ya
{

struct App;

/// The game's world viewport. While the App is running a Scene it declares the
/// primary view from the game camera; in authoring states it declares nothing,
/// because the editor owns that viewport and declares it from the editor
/// camera. Which of the two declares is therefore decided by who is showing the
/// viewport, not by a global flag the editor has to remember to flip.
class YA_GAME_RUNTIME_API RuntimeGameViewProducer final : public ISceneViewProducer
{
  public:
    /// The game's owner id. Values name a product, not a registration slot: a
    /// key that shifted because somebody registered a producer earlier would not
    /// be a stable key. `App::addSceneViewProducer` asserts registered owners
    /// stay distinct.
    static constexpr SceneViewOwnerId kViewOwner = 1;
    /// The game's world viewport inside that owner. The editor's authoring
    /// viewport is a different owner declaring its own primary View, so neither
    /// has to know the other exists.
    static constexpr uint32_t         kDisplayRootLocalId = 1;

    void bind(App& app) { _app = &app; }

    [[nodiscard]] SceneViewOwnerId viewOwner() const override { return kViewOwner; }
    [[nodiscard]] SceneViewKey     displayRootKey() const { return viewKey(kDisplayRootLocalId); }

    void collectSceneViews(const SceneViewCollectContext& context,
                           SceneViewCollector&            collector) override;

  private:
    App* _app = nullptr;
};

} // namespace ya
