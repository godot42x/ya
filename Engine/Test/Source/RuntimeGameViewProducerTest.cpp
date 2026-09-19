#include "AppModuleTestAccess.h"

#include "GameRuntime/App.h"
#include "GameRuntime/Lifecycle/RuntimeGameViewProducer.h"

#include "Render3D/Common/RenderFeatures.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

SceneViewCollectContext makeGameContext(Scene& scene)
{
    return SceneViewCollectContext{
        .activeScene    = &scene,
        .renderResolution = {.width = 1280u, .height = 720u},
        .hostTick       = 3,
        .deltaTime      = 1.0f / 60.0f,
    };
}

} // namespace

TEST(RuntimeGameViewProducerTest, GameViewportDeclaresAuthoredContentOnly)
{
    App app;
    AppModuleTestAccess::setAppState(app, AppState::Runtime);

    RuntimeGameViewProducer producer;
    producer.bind(app);

    Scene scene("Game");

    SceneViewCollector collector;
    producer.collectSceneViews(makeGameContext(scene), collector);

    ASSERT_EQ(collector.views().size(), 1u);
    const SceneViewDesc& primary = collector.views().front();
    EXPECT_EQ(primary.scene, &scene);
    // The game owns its own View identity; it does not share the editor's
    // "primary" slot. The key is what the output tables are keyed on.
    EXPECT_EQ(primary.viewId, producer.hostViewportKey().viewId());
    EXPECT_NE(primary.viewId, 0u);
    // Generated companions are editor furniture; this viewport belongs to the
    // game, and nothing an editor switched on reaches it.
    EXPECT_TRUE(rendersFeature(toMask(ERenderFeature::Game), primary.features));
    EXPECT_FALSE(rendersFeature(toMask(ERenderFeature::Gizmo), primary.features));
}

TEST(RuntimeGameViewProducerTest, AuthoringStatesDeclareNothing)
{
    App app;
    AppModuleTestAccess::setAppState(app, AppState::Stopped);

    RuntimeGameViewProducer producer;
    producer.bind(app);

    Scene              scene("Authoring");
    SceneViewCollector collector;
    producer.collectSceneViews(makeGameContext(scene), collector);

    // The editor owns the viewport in authoring states, and it declares its own.
    EXPECT_TRUE(collector.views().empty());
}

} // namespace ya
