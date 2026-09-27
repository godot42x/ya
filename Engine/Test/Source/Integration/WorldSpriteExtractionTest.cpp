#include "Core/Reflection/DeferredInitializer.h"
#include "ECS/Component/2D/Sprite2DComponent.h"
#include "ECS/Entity.h"
#include "GameRuntime/Render/RenderFrameExtractor.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

namespace ya
{

namespace
{

constexpr float kQuarterTurn = 1.5707963267948966f;

void ensureReflectionReady()
{
    static bool bInitialized = false;
    if (!bInitialized) {
        reflection::DeferredInitializerQueue::instance().executeAll();
        bInitialized = true;
    }
}

WorldSpriteCandidate makeCandidate(const glm::vec3& center, int32_t layer, int32_t sortOrder, float alpha)
{
    WorldSpriteCandidate candidate{};
    candidate.worldCenter  = center;
    candidate.layer        = layer;
    candidate.sortOrder    = sortOrder;
    candidate.tint         = glm::vec4(1.0f, 1.0f, 1.0f, alpha);
    candidate.bTranslucent = alpha < 1.0f;
    return candidate;
}

/// One Scene snapshot holding `candidates`, as extraction would have produced it.
std::shared_ptr<const SceneSnapshot> snapshotWith(std::vector<WorldSpriteCandidate> candidates)
{
    auto snapshot          = std::make_shared<SceneSnapshot>();
    snapshot->worldSprites = std::move(candidates);
    return snapshot;
}

RenderFrameExtractor::ViewPrepareInput viewAt(const glm::vec3& cameraPos)
{
    RenderFrameExtractor::ViewPrepareInput input{};
    input.view       = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    input.cameraPos  = cameraPos;
    input.viewExtent = Extent2D{.width = 1280, .height = 720};
    return input;
}

} // namespace

TEST(WorldSpriteExtractionTest, CandidateCarriesTransformUvAndTranslucency)
{
    Sprite2DComponent sprite;
    sprite.size      = {2.0f, 4.0f};
    sprite.uvRect    = {0.1f, 0.2f, 0.9f, 0.8f};
    sprite.bFlipU    = true;
    sprite.bFlipV    = false;
    sprite.tint      = {0.5f, 0.25f, 0.75f, 1.0f};
    sprite.layer     = 3;
    sprite.sortOrder = 5;

    // 90 degrees about Z with half scale on X: the world axes must carry
    // rotation and scale, and the authored size rides with them.
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) *
                            glm::rotate(glm::mat4(1.0f), kQuarterTurn, glm::vec3(0.0f, 0.0f, 1.0f)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 1.0f, 1.0f));

    const WorldSpriteCandidate candidate = RenderFrameExtractor::buildSpriteCandidate(world, sprite, 42u);

    EXPECT_EQ(candidate.worldCenter, glm::vec3(1.0f, 2.0f, 3.0f));
    // Local +X turned into world +Y, scaled by size.x * scaleX = 2 * 0.5.
    EXPECT_NEAR(candidate.axisX.x, 0.0f, 1e-5f);
    EXPECT_NEAR(candidate.axisX.y, 1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(candidate.axisX), 1.0f, 1e-5f);
    // Local +Y turned into world -X, scaled by size.y = 4.
    EXPECT_NEAR(candidate.axisY.x, -4.0f, 1e-5f);
    EXPECT_NEAR(candidate.axisY.y, 0.0f, 1e-5f);
    EXPECT_NEAR(glm::length(candidate.axisY), 4.0f, 1e-5f);

    // Flip U swaps the atlas window's u pair; flip V is off, so v stays.
    EXPECT_EQ(candidate.uvRect, glm::vec4(0.9f, 0.2f, 0.1f, 0.8f));
    EXPECT_EQ(candidate.tint, glm::vec4(0.5f, 0.25f, 0.75f, 1.0f));
    EXPECT_FALSE(candidate.bTranslucent);
    EXPECT_EQ(candidate.entityId, 42u);
    EXPECT_EQ(candidate.layer, 3);
    EXPECT_EQ(candidate.sortOrder, 5);
}

TEST(WorldSpriteExtractionTest, TintAlphaDecidesTheBlendPolicy)
{
    Sprite2DComponent sprite;
    sprite.tint = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);

    const auto half = RenderFrameExtractor::buildSpriteCandidate(glm::mat4(1.0f), sprite, 1u);
    EXPECT_TRUE(half.bTranslucent);

    sprite.tint.a = 1.0f;
    const auto opaque = RenderFrameExtractor::buildSpriteCandidate(glm::mat4(1.0f), sprite, 1u);
    EXPECT_FALSE(opaque.bTranslucent);
}

TEST(WorldSpriteExtractionTest, SpritesWithoutAResolvedTextureAreNeverExtracted)
{
    ensureReflectionReady();

    Scene scene("SpriteExtraction");

    auto* unset = scene.createNode3D("Unset", scene.getRootNode());
    ASSERT_NE(unset, nullptr);
    unset->getEntity()->addComponent<Sprite2DComponent>();

    auto* loading = scene.createNode3D("Loading", scene.getRootNode());
    ASSERT_NE(loading, nullptr);
    auto* loadingSprite = loading->getEntity()->addComponent<Sprite2DComponent>();
    loadingSprite->image.fromPath("Content/Sprites/hero.png");
    ASSERT_FALSE(loadingSprite->image.textureRef.isLoaded());

    auto* hidden = scene.createNode3D("Hidden", scene.getRootNode());
    ASSERT_NE(hidden, nullptr);
    hidden->getEntity()->addComponent<Sprite2DComponent>()->bVisible = false;

    auto* empty = scene.createNode3D("Empty", scene.getRootNode());
    ASSERT_NE(empty, nullptr);
    empty->getEntity()->addComponent<Sprite2DComponent>()->size = glm::vec2(0.0f, 1.0f);

    SceneSnapshot snapshot;
    RenderFrameExtractor::extractSceneSnapshot(
        RenderFrameExtractor::SceneExtractInput{.scene = &scene, .terrainProcessor = nullptr},
        snapshot);

    // A sprite whose texture is unset, still loading or failed is not drawn, and
    // there is no substitute image to fall back on.
    EXPECT_TRUE(snapshot.worldSprites.empty());
}

TEST(WorldSpriteExtractionTest, ViewOrderPaintsOpaqueFirstThenLayerThenFarToNear)
{
    auto snapshot = snapshotWith({
        makeCandidate(glm::vec3(0.0f, 0.0f, 0.0f), 0, 0, 0.5f),  // [0] blended, nearest
        makeCandidate(glm::vec3(0.0f, 0.0f, 4.0f), 0, 0, 1.0f),  // [1] opaque, near
        makeCandidate(glm::vec3(0.0f, 0.0f, -4.0f), 0, 0, 1.0f), // [2] opaque, far
        makeCandidate(glm::vec3(0.0f, 0.0f, 0.0f), 7, 0, 1.0f),  // [3] opaque, top layer
    });

    RenderFrameData frame;
    RenderFrameExtractor::prepareView(viewAt(glm::vec3(0.0f, 0.0f, 10.0f)), snapshot, frame);

    ASSERT_EQ(frame.worldSprites.size(), 4u);
    // Opaque before blended, then the higher layer last, then far to near.
    const std::vector<uint32_t> expectedOrder{2u, 1u, 3u, 0u};
    for (size_t index = 0; index < expectedOrder.size(); ++index) {
        EXPECT_EQ(&frame.worldSprites[index], &snapshot->worldSprites[expectedOrder[index]])
            << "sprite " << index << " is not the expected candidate";
    }
}

TEST(WorldSpriteExtractionTest, ViewGateDropsSpritesItMustNotDraw)
{
    WorldSpriteCandidate gizmoOnly = makeCandidate(glm::vec3(0.0f), 0, 0, 1.0f);
    gizmoOnly.features             = toMask(ERenderFeature::Gizmo);
    WorldSpriteCandidate hosted = makeCandidate(glm::vec3(1.0f), 0, 0, 1.0f);
    hosted.hostEntityId         = 77u;
    WorldSpriteCandidate authored = makeCandidate(glm::vec3(2.0f), 0, 0, 1.0f);

    auto snapshot = snapshotWith({gizmoOnly, hosted, authored});

    RenderFrameExtractor::ViewPrepareInput input = viewAt(glm::vec3(0.0f, 0.0f, 10.0f));
    input.viewFeatures                           = toMask(ERenderFeature::Game);
    input.viewOwner                              = static_cast<entt::entity>(77u);

    RenderFrameData frame;
    RenderFrameExtractor::prepareView(input, snapshot, frame);

    ASSERT_EQ(frame.worldSprites.size(), 1u);
    EXPECT_EQ(&frame.worldSprites[0], &snapshot->worldSprites[2]);
    // Gating belongs to the View: the shared snapshot keeps all three.
    EXPECT_EQ(snapshot->worldSprites.size(), 3u);
}

TEST(WorldSpriteExtractionTest, TwoViewsShareTheSnapshotAndOwnOnlyTheirOrder)
{
    auto snapshot = snapshotWith({
        makeCandidate(glm::vec3(0.0f, 0.0f, 3.0f), 0, 0, 1.0f),
        makeCandidate(glm::vec3(0.0f, 0.0f, -3.0f), 0, 0, 1.0f),
    });

    RenderFrameData frontView;
    RenderFrameData backView;
    RenderFrameExtractor::prepareView(viewAt(glm::vec3(0.0f, 0.0f, 10.0f)), snapshot, frontView);
    RenderFrameExtractor::prepareView(viewAt(glm::vec3(0.0f, 0.0f, -10.0f)), snapshot, backView);

    EXPECT_EQ(frontView.sceneSnapshot.get(), snapshot.get());
    EXPECT_EQ(backView.sceneSnapshot.get(), snapshot.get());
    ASSERT_EQ(frontView.worldSprites.size(), 2u);
    ASSERT_EQ(backView.worldSprites.size(), 2u);

    // Same candidates, opposite depth order: the order is the View's.
    EXPECT_EQ(&frontView.worldSprites[0], &snapshot->worldSprites[1]);
    EXPECT_EQ(&backView.worldSprites[0], &snapshot->worldSprites[0]);
    EXPECT_EQ(&frontView.worldSprites[0], &backView.worldSprites[1]);
}

} // namespace ya
