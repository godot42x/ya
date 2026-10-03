#include "Core/Math/Ray.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Scene2D/Sprite2DComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/RayCastMousePickingSystem.h"
#include "ECS/Systems/TransformSystem.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

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

Sprite2DComponent* addSprite(Scene& scene, Node*& outNode, const std::string& name)
{
    outNode = scene.createNode3D(name, scene.getRootNode());
    EXPECT_NE(outNode, nullptr);
    Entity* entity = outNode->getEntity();
    EXPECT_NE(entity, nullptr);
    return entity->addComponent<Sprite2DComponent>();
}

void place(Entity* entity, const glm::vec3& position)
{
    auto* transform = entity->getComponent<TransformComponent>();
    transform->setPosition(position);
    TransformSystem::computeWorldMatrix(transform);
}

} // namespace

TEST(Sprite2DComponentTest, RoundTripCloneDuplicateAndDestroy)
{
    ensureReflectionReady();

    Scene scene("SpriteScene");
    Node* node = nullptr;
    Sprite2DComponent* sprite = addSprite(scene, node, "Hero");
    ASSERT_NE(sprite, nullptr);

    sprite->bVisible  = true;
    sprite->size      = {2.0f, 3.5f};
    sprite->uvRect    = {0.1f, 0.2f, 0.8f, 0.9f};
    sprite->bFlipU    = true;
    sprite->bFlipV    = false;
    sprite->tint      = {0.2f, 0.4f, 0.6f, 0.8f};
    sprite->layer     = 4;
    sprite->sortOrder = 7;
    sprite->pickId    = 11;
    sprite->image.fromPath("Content/Sprites/hero.png");

    EXPECT_FALSE(spriteIsDrawable(*sprite));

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();
    EXPECT_NE(saved.dump().find("Content/Sprites/hero.png"), std::string::npos);
    EXPECT_NE(saved.dump().find("Sprite2DComponent"), std::string::npos);

    // Loading a missing texture asks TextureLibrary for a checkerboard. The
    // saved path is the authoring reference; reload the other fields with the
    // path cleared so this test does not need a device.
    nlohmann::json reload = saved;
    for (auto& entityJson : reload["entities"]) {
        entityJson["components"]["Sprite2DComponent"]["image"]["textureRef"]["_path"] = "";
    }
    Scene loaded("SpriteLoaded");
    SceneSerializer loadedSerializer(&loaded);
    loadedSerializer.deserialize(reload);
    Node* loadedNode = loaded.findNodeByPath("/Hero");
    ASSERT_NE(loadedNode, nullptr);
    auto* loadedSprite = loadedNode->getEntity()->getComponent<Sprite2DComponent>();
    ASSERT_NE(loadedSprite, nullptr);
    EXPECT_EQ(loadedSprite->size, glm::vec2(2.0f, 3.5f));
    EXPECT_EQ(loadedSprite->uvRect, glm::vec4(0.1f, 0.2f, 0.8f, 0.9f));
    EXPECT_TRUE(loadedSprite->bFlipU);
    EXPECT_FALSE(loadedSprite->bFlipV);
    EXPECT_EQ(loadedSprite->tint, glm::vec4(0.2f, 0.4f, 0.6f, 0.8f));
    EXPECT_EQ(loadedSprite->layer, 4);
    EXPECT_EQ(loadedSprite->sortOrder, 7);
    EXPECT_EQ(loadedSprite->pickId, 11);
    EXPECT_TRUE(loadedSprite->image.textureRef.getPath().empty());
    EXPECT_FALSE(spriteIsDrawable(*loadedSprite));

    stdptr<Scene> cloned = scene.clone();
    ASSERT_TRUE(cloned);
    Node* clonedNode = cloned->findNodeByPath("/Hero");
    ASSERT_NE(clonedNode, nullptr);
    auto* clonedSprite = clonedNode->getEntity()->getComponent<Sprite2DComponent>();
    ASSERT_NE(clonedSprite, nullptr);
    EXPECT_NE(clonedSprite, sprite);
    EXPECT_EQ(clonedSprite->size, sprite->size);
    EXPECT_EQ(clonedSprite->sortOrder, sprite->sortOrder);
    EXPECT_EQ(clonedSprite->image.textureRef.getPath(), sprite->image.textureRef.getPath());

    Node* duplicate = scene.duplicateNode(node, scene.getRootNode());
    ASSERT_NE(duplicate, nullptr);
    EXPECT_NE(duplicate->getEntity(), node->getEntity());
    auto* duplicatedSprite = duplicate->getEntity()->getComponent<Sprite2DComponent>();
    ASSERT_NE(duplicatedSprite, nullptr);
    EXPECT_EQ(duplicatedSprite->layer, 4);
    EXPECT_EQ(duplicatedSprite->pickId, 11);
    EXPECT_EQ(duplicatedSprite->image.textureRef.getPath(), "Content/Sprites/hero.png");

    scene.destroyNode(node);
    EXPECT_EQ(scene.findNodeByPath("/Hero"), nullptr);
    int remaining = 0;
    scene.getRegistry().view<Sprite2DComponent>().each([&](entt::entity, Sprite2DComponent&) {
        ++remaining;
    });
    EXPECT_EQ(remaining, 1);
}

TEST(Sprite2DComponentTest, UnloadedTextureIsNotDrawable)
{
    Sprite2DComponent unset;
    EXPECT_FALSE(spriteIsDrawable(unset));

    Sprite2DComponent pending;
    pending.image.fromPath("Content/Sprites/missing.png");
    EXPECT_FALSE(spriteIsDrawable(pending));

    Sprite2DComponent hidden;
    hidden.bVisible = false;
    hidden.image.fromPath("Content/Sprites/missing.png");
    EXPECT_FALSE(spriteIsDrawable(hidden));
}

TEST(Sprite2DComponentTest, RayHitsLocalQuadAndPrefersFrontSprite)
{
    ensureReflectionReady();

    Scene scene("SpritePick");
    Node* backNode = nullptr;
    Node* frontNode = nullptr;
    Sprite2DComponent* back = addSprite(scene, backNode, "Back");
    Sprite2DComponent* front = addSprite(scene, frontNode, "Front");
    ASSERT_NE(back, nullptr);
    ASSERT_NE(front, nullptr);
    back->size = {2.0f, 2.0f};
    front->size = {2.0f, 2.0f};
    back->sortOrder = 1;
    front->sortOrder = 9;
    place(backNode->getEntity(), {0.0f, 0.0f, 0.0f});
    place(frontNode->getEntity(), {0.0f, 0.0f, 2.0f});

    const Ray toward{glm::vec3{0.0f, 0.0f, 5.0f}, glm::vec3{0.0f, 0.0f, -1.0f}};
    const auto hit = RayCastMousePickingSystem::raycast(&scene, toward);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, frontNode->getEntity());

    const Ray miss{glm::vec3{4.0f, 0.0f, 5.0f}, glm::vec3{0.0f, 0.0f, -1.0f}};
    EXPECT_FALSE(RayCastMousePickingSystem::raycast(&scene, miss).has_value());

    place(frontNode->getEntity(), {0.0f, 0.0f, 0.0f});
    back->layer = 0;
    front->layer = 0;
    back->sortOrder = 1;
    front->sortOrder = 3;
    const auto tied = RayCastMousePickingSystem::raycast(&scene, toward);
    ASSERT_TRUE(tied.has_value());
    EXPECT_EQ(tied->entity, frontNode->getEntity());

    front->bVisible = false;
    const auto hidden = RayCastMousePickingSystem::raycast(&scene, toward);
    ASSERT_TRUE(hidden.has_value());
    EXPECT_EQ(hidden->entity, backNode->getEntity());
}

} // namespace ya
