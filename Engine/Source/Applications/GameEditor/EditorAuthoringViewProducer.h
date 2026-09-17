#pragma once

#include "Core/Base.h"
#include "Render3D/Common/SceneViewProducer.h"

namespace ya
{

struct App;
struct EditorLayer;

/// The editor's authoring viewport. It declares the primary view from the editor
/// camera while the editor owns the viewport -- that is, whenever the game is
/// not the thing being shown -- and declares nothing in the 2D canvas workspace.
///
/// Both answers are expressed by declaring a view or not declaring one, so the
/// editor never has to tell the runtime to switch world rendering off, and the
/// runtime never has to guess which camera the editor meant.
class EditorAuthoringViewProducer final : public ISceneViewProducer
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
