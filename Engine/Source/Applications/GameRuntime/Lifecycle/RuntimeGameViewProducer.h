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
    void bind(App& app) { _app = &app; }

    void collectSceneViews(const SceneViewCollectContext& context,
                           SceneViewCollector&            collector) override;

  private:
    App* _app = nullptr;
};

} // namespace ya
