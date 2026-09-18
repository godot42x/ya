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
    /// View identity of the editor's camera preview. Editor-owned: the host does
    /// not mint ids for views it does not show.
    static constexpr SceneViewId kPreviewViewId = 2;

    /// Where the preview sits inside the authoring viewport, in viewport pixels.
    /// The preview View renders at this size and the editor's viewport chrome
    /// shows it at this rect, so the placement is stated once.
    [[nodiscard]] static Rect2D previewRect(const Rect2D& authoringRect);

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
