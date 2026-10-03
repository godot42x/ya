#include "Core/Common/AssetTypeRegistry.h"
#include "Core/Common/SpriteAnimationSet.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "ECS/Entity.h"
#include "Scene/Core/Scene.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "Scene2D/Sprite2DComponent.h"
#include "Scene2D/SpriteAnimationComponent.h"
#include "Scene2D/SpriteAnimationSystem.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace ya
{

namespace
{

void ensureReflectionReady()
{
    static bool bInitialized = false;
    if (!bInitialized) {
        reflection::DeferredInitializerQueue::instance().executeAll();
        bInitialized = true;
    }
}

constexpr const char* kHeroAsset = "sprite-anim-hero";

std::shared_ptr<SpriteAnimationSet> makeHeroSet()
{
    auto set     = std::make_shared<SpriteAnimationSet>();
    set->columns = 3;
    set->rows    = 4;
    set->clips   = {
        {.name = "idle", .frames = {1}, .fps = 1.0f, .bLoop = true},
        {.name = "walk", .frames = {0, 1, 2, 1}, .fps = 10.0f, .bLoop = true},
        {.name = "swing", .frames = {3, 4, 5}, .fps = 10.0f, .bLoop = false},
    };
    return set;
}

// A 3x4 hero sheet registered in memory: frame = row * 3 + column, the walk
// cycle revisits the stand frame.
struct FHeroFixture
{
    Scene                     scene{"SpriteAnimationScene"};
    Sprite2DComponent*        sprite    = nullptr;
    SpriteAnimationComponent* animation = nullptr;

    FHeroFixture()
    {
        ensureReflectionReady();
        auto* store = AssetTypeRegistry::get().store<SpriteAnimationSet>();
        store->registerAsset(kHeroAsset, makeHeroSet());

        Node*   node   = scene.createNode3D("Hero", scene.getRootNode());
        Entity* entity = node->getEntity();
        sprite         = entity->addComponent<Sprite2DComponent>();
        animation      = entity->addComponent<SpriteAnimationComponent>();
        animation->animation = SpriteAnimationSetRef(kHeroAsset);
    }
};

} // namespace

TEST(SpriteAnimationTest, FrameRectIsRowMajorFromTheTopLeft)
{
    FHeroFixture hero;
    const glm::vec4 frame4 = hero.animation->frameRect(4); // column 1, row 1
    EXPECT_FLOAT_EQ(frame4.x, 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(frame4.y, 0.25f);
    EXPECT_FLOAT_EQ(frame4.z, 2.0f / 3.0f);
    EXPECT_FLOAT_EQ(frame4.w, 0.5f);

    EXPECT_EQ(hero.animation->frameRect(-1), glm::vec4(0.0f));
    EXPECT_EQ(hero.animation->frameRect(12), glm::vec4(0.0f));
}

TEST(SpriteAnimationTest, PlayShowsTheFirstFrameImmediately)
{
    FHeroFixture hero;
    EXPECT_TRUE(hero.animation->play("walk"));
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(0));
    EXPECT_TRUE(hero.animation->isPlaying());
    EXPECT_EQ(hero.animation->currentClip(), "walk");
}

TEST(SpriteAnimationTest, UnknownClipFailsAndLeavesTheSpriteAlone)
{
    FHeroFixture hero;
    const glm::vec4 before = hero.sprite->uvRect;
    EXPECT_FALSE(hero.animation->play("fly"));
    EXPECT_EQ(hero.sprite->uvRect, before);
    EXPECT_FALSE(hero.animation->isPlaying());
}

TEST(SpriteAnimationTest, PlayingTheRunningClipDoesNotRestartIt)
{
    FHeroFixture hero;
    hero.animation->play("walk");
    hero.animation->advance(0.25f); // frame 2 of {0, 1, 2, 1}
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(2));

    hero.animation->play("walk");
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(2));

    hero.animation->play("idle");
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(1));
}

TEST(SpriteAnimationTest, LoopWrapsAndNonLoopHoldsTheLastFrame)
{
    FHeroFixture hero;
    hero.animation->play("walk");
    hero.animation->advance(0.45f); // frame index 4 wraps to 0
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(0));
    EXPECT_TRUE(hero.animation->isPlaying());

    hero.animation->play("swing");
    hero.animation->advance(1.0f);
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(5));
    EXPECT_FALSE(hero.animation->isPlaying());

    // A finished one-shot restarts when played again.
    EXPECT_TRUE(hero.animation->play("swing"));
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(3));
    EXPECT_TRUE(hero.animation->isPlaying());
}

TEST(SpriteAnimationTest, SetFrameStopsAndShowsThatFrame)
{
    FHeroFixture hero;
    hero.animation->play("walk");
    hero.animation->setFrame(7);
    EXPECT_FALSE(hero.animation->isPlaying());
    EXPECT_TRUE(hero.animation->currentClip().empty());
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(7));
    hero.animation->advance(1.0f);
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(7));
}

TEST(SpriteAnimationTest, FirstAdvanceStartsTheAuthoredClip)
{
    FHeroFixture hero;
    hero.animation->clip = "walk";
    hero.animation->advance(0.15f); // starts at 0, then 0.15s -> index 1
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(1));
    EXPECT_EQ(hero.animation->currentClip(), "walk");
}

TEST(SpriteAnimationTest, SystemAdvancesOnlyWhenThePolicyAllows)
{
    FHeroFixture hero;
    hero.animation->clip = "walk";
    bool bPlaying        = false;

    SpriteAnimationSystem system;
    system._sceneProvider = [&hero]() { return &hero.scene; };
    system._tickPolicy    = [&bPlaying]() { return bPlaying; };

    const glm::vec4 authored = hero.sprite->uvRect;
    system.onUpdate(0.25f);
    EXPECT_EQ(hero.sprite->uvRect, authored) << "an edited scene keeps its authored uvRect";

    bPlaying = true;
    system.onUpdate(0.0f); // starts the clip
    system.onUpdate(0.25f);
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(2));
}

TEST(SpriteAnimationTest, UnloadedAssetDoesNotAdvanceAndWarnsOnce)
{
    ensureReflectionReady();
    Scene   scene{"UnloadedAnimation"};
    Node*   node   = scene.createNode3D("Hero", scene.getRootNode());
    Entity* entity = node->getEntity();
    auto*   sprite = entity->addComponent<Sprite2DComponent>();
    auto*   animation = entity->addComponent<SpriteAnimationComponent>();
    animation->animation = SpriteAnimationSetRef("Content/Animations/__sprite_anim_unloaded.yaanim.json");
    animation->clip      = "walk";

    const glm::vec4 authored = sprite->uvRect;
    animation->advance(0.1f);
    animation->advance(0.1f);
    EXPECT_FALSE(animation->play("walk"));
    EXPECT_FALSE(animation->play("walk"));
    EXPECT_EQ(sprite->uvRect, authored);
    EXPECT_FALSE(animation->isPlaying());
}

TEST(SpriteAnimationTest, ReloadResolvesThePlayingClipByName)
{
    FHeroFixture hero;
    hero.animation->play("walk");
    hero.animation->advance(0.25f);
    EXPECT_EQ(hero.sprite->uvRect, hero.animation->frameRect(2));

    // Same slot, new document: walk moves from index 1 to the only clip, so
    // the cached index would read off the end.
    auto narrowed     = std::make_shared<SpriteAnimationSet>();
    narrowed->columns = 1;
    narrowed->rows    = 1;
    narrowed->clips   = {{.name = "walk", .frames = {0}, .fps = 10.0f, .bLoop = true}};
    auto* store = AssetTypeRegistry::get().store<SpriteAnimationSet>();
    store->registerAsset(kHeroAsset, narrowed);
    hero.animation->advance(0.0f);
    EXPECT_EQ(hero.animation->currentClip(), "walk");
    EXPECT_EQ(hero.sprite->uvRect, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    // invalidate drops the entry. The ref keeps the old slot until rebind;
    // the replacement is a different sheet and walk's frame is no longer 0.
    store->invalidate(kHeroAsset);
    auto shifted     = std::make_shared<SpriteAnimationSet>();
    shifted->columns = 2;
    shifted->rows    = 1;
    shifted->clips   = {{.name = "walk", .frames = {1}, .fps = 10.0f, .bLoop = true}};
    store->registerAsset(kHeroAsset, shifted);
    hero.animation->animation.rebind();
    hero.animation->advance(0.0f);
    EXPECT_EQ(hero.animation->currentClip(), "walk");
    EXPECT_EQ(hero.sprite->uvRect, glm::vec4(0.5f, 0.0f, 1.0f, 1.0f));
}

TEST(SpriteAnimationTest, EditResetsThePlayhead)
{
    FHeroFixture hero;
    hero.animation->clip = "walk";
    hero.animation->play("idle");
    EXPECT_EQ(hero.animation->currentClip(), "idle");

    hero.animation->onEdit();
    EXPECT_FALSE(hero.animation->isPlaying());
    EXPECT_TRUE(hero.animation->currentClip().empty());
    hero.animation->advance(0.15f);
    EXPECT_EQ(hero.animation->currentClip(), "walk");
}

TEST(SpriteAnimationTest, AuthoredFieldsRoundTripAndRuntimeStateDoesNot)
{
    FHeroFixture hero;
    hero.animation->clip = "idle";
    hero.animation->play("walk");

    SceneSerializer      serializer(&hero.scene);
    const nlohmann::json saved = serializer.serialize();
    const std::string    text  = saved.dump();
    EXPECT_NE(text.find("SpriteAnimationComponent"), std::string::npos);
    EXPECT_NE(text.find(kHeroAsset), std::string::npos);
    EXPECT_NE(text.find("\"clip\""), std::string::npos);
    EXPECT_EQ(text.find("\"columns\""), std::string::npos);
    EXPECT_EQ(text.find("\"clips\""), std::string::npos);
    EXPECT_EQ(text.find("\"swing\""), std::string::npos);
    EXPECT_EQ(text.find("_elapsed"), std::string::npos);
    EXPECT_EQ(text.find("_playingClip"), std::string::npos);
    EXPECT_EQ(text.find("_clipIndex"), std::string::npos);

    Scene loaded("Loaded");
    SceneSerializer loader(&loaded);
    loader.deserialize(saved);
    Node* node = loaded.findNodeByPath("/Hero");
    ASSERT_NE(node, nullptr);
    auto* copy = node->getEntity()->getComponent<SpriteAnimationComponent>();
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->animation.getPath(), kHeroAsset);
    EXPECT_EQ(copy->animation._handle, hero.animation->animation._handle);
    EXPECT_EQ(copy->clip, "idle");
    EXPECT_FALSE(copy->isPlaying());
    EXPECT_TRUE(copy->currentClip().empty());
}

TEST(SpriteAnimationTest, TwoEntitiesShareOneAssetSlot)
{
    FHeroFixture first;
    FHeroFixture second;
    ASSERT_NE(first.animation->animation._handle, nullptr);
    EXPECT_EQ(first.animation->animation._handle, second.animation->animation._handle);
    EXPECT_EQ(first.animation->animation.get(), second.animation->animation.get());
}

} // namespace ya
