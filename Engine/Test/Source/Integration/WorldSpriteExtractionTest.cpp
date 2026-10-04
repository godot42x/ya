#include "Core/Reflection/DeferredInitializer.h"
#include "Scene2D/Sprite2DComponent.h"
#include "Scene2D/SpriteDrawOrder.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "GameRuntime/Render/RenderFrameExtractor.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"

#include <cmath>
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

WorldSpriteCandidate makeCandidate(const glm::vec3& center,
                                     int32_t           layer,
                                     int32_t           sortOrder,
                                     float             alpha,
                                     bool              bYSort    = false,
                                     uint32_t          entityId  = 0,
                                     uint32_t          sequence  = 0)
{
    WorldSpriteCandidate candidate{};
    candidate.worldCenter = center;
    candidate.sortPointY  = center.y;
    candidate.drawKey     = makeSpriteDrawKey(layer, bYSort, center.y, sortOrder, entityId, sequence);
    candidate.tint        = glm::vec4(1.0f, 1.0f, 1.0f, alpha);
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
    EXPECT_EQ(candidate.drawKey.entityId, 42u);
    EXPECT_EQ(candidate.drawKey.layer, 3);
    EXPECT_EQ(candidate.drawKey.order, 5);
    EXPECT_EQ(candidate.drawKey.ySortRank, 0);
    EXPECT_FLOAT_EQ(candidate.sortPointY, 2.0f);
}

TEST(WorldSpriteExtractionTest, TintAlphaDoesNotChangeTheDrawKey)
{
    Sprite2DComponent sprite;
    sprite.tint = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);

    const auto half = RenderFrameExtractor::buildSpriteCandidate(glm::mat4(1.0f), sprite, 1u);
    sprite.tint.a = 1.0f;
    const auto opaque = RenderFrameExtractor::buildSpriteCandidate(glm::mat4(1.0f), sprite, 1u);
    EXPECT_FLOAT_EQ(half.tint.a, 0.5f);
    EXPECT_FLOAT_EQ(opaque.tint.a, 1.0f);
    EXPECT_TRUE(spriteDrawsBefore(half.drawKey, opaque.drawKey) == false);
    EXPECT_TRUE(spriteDrawsBefore(opaque.drawKey, half.drawKey) == false);
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

TEST(WorldSpriteExtractionTest, ViewOrderFollowsThePainterKeyNotCameraDepth)
{
    auto snapshot = snapshotWith({
        makeCandidate(glm::vec3(0.0f, 2.0f, 0.0f), 0, 0, 1.0f, true, 1),  // [0] y-sort, higher on screen
        makeCandidate(glm::vec3(0.0f, 0.0f, 4.0f), 0, 0, 1.0f, true, 2),  // [1] y-sort, lower on screen
        makeCandidate(glm::vec3(0.0f, 0.0f, -4.0f), 1, 0, 0.5f, false, 3), // [2] next layer, translucent
        makeCandidate(glm::vec3(0.0f, 9.0f, 1.0f), 0, 5, 1.0f, false, 4),  // [3] no y-sort, paints first
    });

    RenderFrameData nearCamera;
    RenderFrameData farCamera;
    RenderFrameExtractor::prepareView(viewAt(glm::vec3(0.0f, 0.0f, 10.0f)), snapshot, nearCamera);
    RenderFrameExtractor::prepareView(viewAt(glm::vec3(0.0f, 0.0f, -10.0f)), snapshot, farCamera);

    ASSERT_EQ(nearCamera.worldSprites.size(), 4u);
    // Rank 0 before rank 1, then smaller world y (larger yKey) last, then the higher layer.
    const std::vector<uint32_t> expectedOrder{3u, 0u, 1u, 2u};
    for (size_t index = 0; index < expectedOrder.size(); ++index) {
        EXPECT_EQ(&nearCamera.worldSprites[index], &snapshot->worldSprites[expectedOrder[index]])
            << "sprite " << index << " is not the expected candidate";
        EXPECT_EQ(&farCamera.worldSprites[index], &nearCamera.worldSprites[index]);
    }
}

TEST(WorldSpriteExtractionTest, SwappingZDoesNotChangeOrderSwappingOrderDoes)
{
    auto snapshot = snapshotWith({
        makeCandidate(glm::vec3(1.0f, 2.0f, 0.2f), 0, 1, 1.0f, false, 1),
        makeCandidate(glm::vec3(1.0f, 2.0f, 4.0f), 0, 3, 1.0f, false, 2),
    });

    const auto orderOf = [](const std::shared_ptr<const SceneSnapshot>& source) {
        RenderFrameData frame;
        RenderFrameExtractor::prepareView(viewAt(glm::vec3(0.0f, 0.0f, 10.0f)), source, frame);
        std::vector<uint32_t> ids;
        for (const WorldSpriteCandidate& sprite : frame.worldSprites) {
            ids.push_back(sprite.drawKey.entityId);
        }
        return ids;
    };

    const std::vector<uint32_t> before = orderOf(snapshot);
    EXPECT_EQ(before, (std::vector<uint32_t>{1u, 2u}));

    auto swappedZ = snapshotWith({
        makeCandidate(glm::vec3(1.0f, 2.0f, 4.0f), 0, 1, 1.0f, false, 1),
        makeCandidate(glm::vec3(1.0f, 2.0f, 0.2f), 0, 3, 1.0f, false, 2),
    });
    EXPECT_EQ(orderOf(swappedZ), before);

    auto swappedOrder = snapshotWith({
        makeCandidate(glm::vec3(1.0f, 2.0f, 0.2f), 0, 3, 1.0f, false, 1),
        makeCandidate(glm::vec3(1.0f, 2.0f, 4.0f), 0, 1, 1.0f, false, 2),
    });
    EXPECT_EQ(orderOf(swappedOrder), (std::vector<uint32_t>{2u, 1u}));
}

TEST(WorldSpriteExtractionTest, YSortCoversTheLowerSpriteAndIgnoresYWhenOff)
{
    auto sorted = snapshotWith({
        makeCandidate(glm::vec3(0.0f, 3.0f, 0.1f), 0, 0, 1.0f, true, 1),
        makeCandidate(glm::vec3(0.0f, 1.0f, 0.9f), 0, 0, 1.0f, true, 2),
    });
    auto swapped = snapshotWith({
        makeCandidate(glm::vec3(0.0f, 1.0f, 0.1f), 0, 0, 1.0f, true, 1),
        makeCandidate(glm::vec3(0.0f, 3.0f, 0.9f), 0, 0, 1.0f, true, 2),
    });
    auto unsorted = snapshotWith({
        makeCandidate(glm::vec3(0.0f, 3.0f, 0.1f), 0, 1, 1.0f, false, 1),
        makeCandidate(glm::vec3(0.0f, 1.0f, 0.9f), 0, 4, 1.0f, false, 2),
    });

    const auto backEntity = [](const std::shared_ptr<const SceneSnapshot>& source) {
        RenderFrameData frame;
        RenderFrameExtractor::prepareView(viewAt(glm::vec3(0.0f)), source, frame);
        return frame.worldSprites[0].drawKey.entityId;
    };
    const auto frontEntity = [](const std::shared_ptr<const SceneSnapshot>& source) {
        RenderFrameData frame;
        RenderFrameExtractor::prepareView(viewAt(glm::vec3(0.0f)), source, frame);
        return frame.worldSprites[frame.worldSprites.size() - 1].drawKey.entityId;
    };

    EXPECT_EQ(backEntity(sorted), 1u);
    EXPECT_EQ(frontEntity(sorted), 2u);
    EXPECT_EQ(frontEntity(swapped), 1u);
    // Y-sort off: the lower sprite does not jump in front. order does.
    EXPECT_EQ(frontEntity(unsorted), 2u);
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

    // The painter key ignores the camera, so both views share one order.
    EXPECT_EQ(&frontView.worldSprites[0], &snapshot->worldSprites[0]);
    EXPECT_EQ(&backView.worldSprites[0], &snapshot->worldSprites[0]);
    EXPECT_EQ(&frontView.worldSprites[1], &backView.worldSprites[1]);
}

TEST(WorldSpriteExtractionTest, PixelSnapLandsCandidateCentersOnTheTexelGrid)
{
    const float step = 1.0f / 16.0f;
    std::vector<WorldSpriteCandidate> sprites{
        makeCandidate(glm::vec3(1.03f, -2.2f, 0.4f), 0, 0, 1.0f),
        makeCandidate(glm::vec3(0.5f, 0.75f, 0.1f), 0, 0, 1.0f),
    };
    const glm::vec3 unsnapped = sprites[0].worldCenter;

    RenderFrameExtractor::snapSpriteCandidatesToTexelGrid(sprites, 0.0f);
    EXPECT_EQ(sprites[0].worldCenter, unsnapped);

    RenderFrameExtractor::snapSpriteCandidatesToTexelGrid(sprites, step);
    for (const WorldSpriteCandidate& sprite : sprites) {
        const float qx = sprite.worldCenter.x / step;
        const float qy = sprite.worldCenter.y / step;
        EXPECT_NEAR(qx, std::round(qx), 1e-4f);
        EXPECT_NEAR(qy, std::round(qy), 1e-4f);
    }
    EXPECT_NEAR(sprites[0].worldCenter.z, 0.4f, 1e-5f);
    // Already on the 1/16 grid (player feet): snapping is a no-op.
    EXPECT_NEAR(sprites[1].worldCenter.x, 0.5f, 1e-5f);
    EXPECT_NEAR(sprites[1].worldCenter.y, 0.75f, 1e-5f);
}

TEST(WorldSpriteExtractionTest, DefaultPivotKeepsTheCentreOnTheEntity)
{
    Sprite2DComponent sprite;
    sprite.size = {2.0f, 4.0f};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    const WorldSpriteCandidate candidate = RenderFrameExtractor::buildSpriteCandidate(world, sprite, 1u);
    EXPECT_EQ(spriteQuadCenterOffset(sprite), glm::vec2(0.0f));
    EXPECT_EQ(candidate.worldCenter, glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(WorldSpriteExtractionTest, BottomPivotPutsTheBottomEdgeOnTheEntity)
{
    Sprite2DComponent sprite;
    sprite.size  = {2.0f, 4.0f};
    sprite.pivot = {0.5f, 0.0f};
    const glm::vec3 entityPos{3.0f, 4.0f, 1.0f};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), entityPos);
    const WorldSpriteCandidate candidate = RenderFrameExtractor::buildSpriteCandidate(world, sprite, 1u);

    EXPECT_EQ(spriteQuadCenterOffset(sprite), glm::vec2(0.0f, 2.0f));
    EXPECT_NEAR(candidate.worldCenter.x, 3.0f, 1e-5f);
    EXPECT_NEAR(candidate.worldCenter.y, 6.0f, 1e-5f);
    EXPECT_NEAR(candidate.worldCenter.z, 1.0f, 1e-5f);
    const glm::vec3 bottomEdge = candidate.worldCenter + candidate.axisY * -0.5f;
    EXPECT_NEAR(bottomEdge.x, entityPos.x, 1e-5f);
    EXPECT_NEAR(bottomEdge.y, entityPos.y, 1e-5f);
    EXPECT_NEAR(bottomEdge.z, entityPos.z, 1e-5f);
}

TEST(WorldSpriteExtractionTest, PivotFollowsRotatedAndScaledAxes)
{
    Sprite2DComponent sprite;
    sprite.size  = {1.0f, 1.5f};
    sprite.pivot = {0.25f, 0.0f};
    // 90 degrees about Z, scale (2, 3, 1). Local +X becomes world +Y, local +Y becomes world -X.
    const glm::vec3 entityPos{5.0f, 6.0f, 7.0f};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), entityPos) *
                            glm::rotate(glm::mat4(1.0f), kQuarterTurn, glm::vec3(0.0f, 0.0f, 1.0f)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 1.0f));
    const WorldSpriteCandidate candidate = RenderFrameExtractor::buildSpriteCandidate(world, sprite, 1u);

    EXPECT_NEAR(spriteQuadCenterOffset(sprite).x, 0.25f, 1e-5f);
    EXPECT_NEAR(spriteQuadCenterOffset(sprite).y, 0.75f, 1e-5f);
    // centre = entity + axisX*(0.5-pivot.x) + axisY*(0.5-pivot.y)
    EXPECT_NEAR(candidate.worldCenter.x, 2.75f, 1e-4f);
    EXPECT_NEAR(candidate.worldCenter.y, 6.5f, 1e-4f);
    EXPECT_NEAR(candidate.worldCenter.z, 7.0f, 1e-4f);
    const glm::vec3 pivotOnQuad = candidate.worldCenter
                                + candidate.axisX * (sprite.pivot.x - 0.5f)
                                + candidate.axisY * (sprite.pivot.y - 0.5f);
    EXPECT_NEAR(pivotOnQuad.x, entityPos.x, 1e-4f);
    EXPECT_NEAR(pivotOnQuad.y, entityPos.y, 1e-4f);
    EXPECT_NEAR(pivotOnQuad.z, entityPos.z, 1e-4f);
}

TEST(WorldSpriteExtractionTest, IntegerTexelPivotOffsetSnapsWithTheCentre)
{
    // 1 x 1.5 at 16 px/unit is 16 x 24 texels. Pivot offset 0.25 units is 4 texels.
    constexpr float kPixelsPerUnit = 16.0f;
    const float step = 1.0f / kPixelsPerUnit;
    Sprite2DComponent sprite;
    sprite.size  = {1.0f, 1.5f};
    sprite.pivot = {0.25f, 0.5f};
    const glm::vec3 entityPos{1.03f, -2.2f, 0.4f};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), entityPos);
    const WorldSpriteCandidate built = RenderFrameExtractor::buildSpriteCandidate(world, sprite, 1u);
    const glm::vec2 offset = spriteQuadCenterOffset(sprite);
    EXPECT_NEAR(offset.x, 0.25f, 1e-6f);
    EXPECT_NEAR(offset.y, 0.0f, 1e-6f);

    std::vector<WorldSpriteCandidate> sprites{built};
    RenderFrameExtractor::snapSpriteCandidatesToTexelGrid(sprites, step);
    const glm::vec3 snapped = sprites[0].worldCenter;

    const auto onGrid = [&](float value) {
        const float quanta = value / step;
        EXPECT_NEAR(quanta, std::round(quanta), 1e-4f);
    };
    onGrid(snapped.x);
    onGrid(snapped.y);
    EXPECT_NEAR(snapped.z, entityPos.z, 1e-5f);
    // Even texel counts: snapping the centre puts every edge on a texel boundary.
    onGrid(snapped.x - sprite.size.x * 0.5f);
    onGrid(snapped.x + sprite.size.x * 0.5f);
    onGrid(snapped.y - sprite.size.y * 0.5f);
    onGrid(snapped.y + sprite.size.y * 0.5f);

    // The offset is a whole number of texels, so snapping the centre is the same
    // as snapping the pivot (the entity) and adding the offset back.
    const glm::vec3 snappedPivot = snapWorldXY(entityPos, step, 0.0f, 0.0f);
    EXPECT_NEAR(snapped.x, snappedPivot.x + offset.x, 1e-4f);
    EXPECT_NEAR(snapped.y, snappedPivot.y + offset.y, 1e-4f);
}

TEST(WorldSpriteExtractionTest, YSortKeyUsesTheSnappedPivotNotTheQuadCentre)
{
    constexpr float kPixelsPerUnit = 16.0f;
    const float step = 1.0f / kPixelsPerUnit;
    Sprite2DComponent sprite;
    sprite.size  = {1.0f, 1.5f};
    sprite.pivot = {0.5f, 0.0f};
    sprite.bYSort = true;
    const glm::vec3 entityPos{1.03f, -2.2f, 0.4f};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), entityPos);
    std::vector<WorldSpriteCandidate> sprites{
        RenderFrameExtractor::buildSpriteCandidate(world, sprite, 7u),
    };
    EXPECT_NEAR(sprites[0].sortPointY, entityPos.y, 1e-5f);
    EXPECT_GT(std::abs(sprites[0].worldCenter.y - sprites[0].sortPointY), 0.1f);

    RenderFrameExtractor::snapSpriteCandidatesToTexelGrid(sprites, step);
    const glm::vec3 snappedPivot = snapWorldXY(entityPos, step, 0.0f, 0.0f);
    EXPECT_NEAR(sprites[0].sortPointY, snappedPivot.y, 1e-4f);
    EXPECT_NEAR(sprites[0].drawKey.yKey, -snappedPivot.y, 1e-4f);
    EXPECT_EQ(sprites[0].drawKey.ySortRank, 1);
    EXPECT_NE(sprites[0].worldCenter.y, sprites[0].sortPointY);
}

} // namespace ya
