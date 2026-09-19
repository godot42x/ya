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
/// It declares those views only while an editor viewport is on screen, so a
/// hidden viewport records no world graph. Everything is expressed by declaring
/// a view or not declaring one, so the editor never asks the runtime to switch
/// world rendering off, and the runtime never guesses which camera the editor
/// meant.
class EditorViewProducer final : public ISceneViewProducer
{
  public:
    /// The editor's owner id, and the local ids it mints inside it. The editor
    /// names its own owner, so its authoring viewport and the game's world
    /// viewport can both be "the primary View" without colliding -- which is the
    /// point of owner-scoped identity. Values name a product, not a registration
    /// slot; `App::addSceneViewProducer` asserts registered owners stay distinct.
    static constexpr SceneViewOwnerId kViewOwner        = 2;
    /// The editor's authoring viewport.
    static constexpr uint32_t         kAuthoringLocalId = 1;
    /// The selected camera's preview inset.
    static constexpr uint32_t         kPreviewLocalId   = 2;

    /// Where the preview sits inside the authoring viewport, in viewport pixels.
    /// The preview View renders at this size and the editor's viewport chrome
    /// shows it at this rect, so the placement is stated once.
    [[nodiscard]] static Rect2D previewRect(const Rect2D& authoringRect);

    void bind(App& app, EditorLayer& layer)
    {
        _app   = &app;
        _layer = &layer;
    }

    [[nodiscard]] SceneViewOwnerId viewOwner() const override { return kViewOwner; }
    [[nodiscard]] SceneViewKey     authoringKey() const { return viewKey(kAuthoringLocalId); }
    [[nodiscard]] SceneViewKey     previewKey() const { return viewKey(kPreviewLocalId); }

    void collectSceneViews(const SceneViewCollectContext& context,
                           SceneViewCollector&            collector) override;

  private:
    App*         _app   = nullptr;
    EditorLayer* _layer = nullptr;
};

} // namespace ya
