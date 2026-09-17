#pragma once

#include "Core/Base.h"
#include "Render3D/Common/SceneViewProducer.h"

namespace ya
{

struct App;
struct EditorLayer;

/// The editor's views.
///
/// It declares the authoring viewport's primary view from the editor camera
/// while the editor owns the viewport (not while the game is), plus the inset
/// preview of the camera the user selected, which is the editor's to choose.
/// Everything is expressed by declaring a view or not declaring one, so the
/// editor never asks the runtime to switch world rendering off, and the runtime
/// never guesses which camera the editor meant.
class EditorViewProducer final : public ISceneViewProducer
{
  public:
    void bind(App& app, EditorLayer& layer)
    {
        _app   = &app;
        _layer = &layer;
    }

    void collectSceneViews(const SceneViewCollectContext& context,
                           SceneViewCollector&            collector) override;

  private:
    App*         _app   = nullptr;
    EditorLayer* _layer = nullptr;
};

} // namespace ya
