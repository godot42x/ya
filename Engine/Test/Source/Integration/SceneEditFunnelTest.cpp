// Unified edit funnel regression guards (resource-handle-events H2). Every
// in-place component write path routes through Scene::notifyComponentEdited,
// whose trampoline turns the write into registry.patch -> entt on_update plus
// one SceneBus broadcast; component creation/removal broadcast through the
// mutation funnels. Derived-work processors listen to exactly these.

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "ECS/ECSRegistry.h"
#include "ECS/SceneBus.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(SceneEditFunnelTest, AddRemoveBroadcastThroughMutationFunnel)
{
    int added = 0;
    int removed = 0;
    DelegateHandle addedHandle =
        SceneBus::get().onComponentAdded.addLambda(
            [&](entt::registry&, entt::entity, ya::type_index_t type) {
                if (type == type_index_v<TransformComponent>) {
                    ++added;
                }
            });
    DelegateHandle removedHandle =
        SceneBus::get().onComponentRemoved.addLambda(
            [&](entt::registry&, entt::entity, ya::type_index_t type) {
                if (type == type_index_v<TransformComponent>) {
                    ++removed;
                }
            });

    {
        stdptr<Scene> scene{new Scene("FunnelBusScene")};
        Node* node = scene->createNode("Probe");
        Entity* entity = node->getEntity();
        ASSERT_NE(entity, nullptr);

        // createNode already attached the entity's default components, so the
        // broadcast for them fired before this point; the funnel signal is
        // what the deltas below count.
        const int addedBefore = added;
        const int removedBefore = removed;

        entity->addComponent<TransformComponent>();
        EXPECT_EQ(added, addedBefore + 1);

        scene->removeComponent<TransformComponent>(entity->getHandle());
        EXPECT_EQ(removed, removedBefore + 1);
    }

    SceneBus::get().onComponentAdded.remove(addedHandle);
    SceneBus::get().onComponentRemoved.remove(removedHandle);
}

TEST(SceneEditFunnelTest, NotifyComponentEditedTriggersPatchAndBroadcast)
{
    int edits = 0;
    int enttUpdates = 0;
    DelegateHandle editedHandle =
        SceneBus::get().onComponentEdited.addLambda(
            [&](entt::registry& registry, entt::entity entity, ya::type_index_t type) {
                if (type == type_index_v<TransformComponent> &&
                    registry.valid(entity)) {
                    ++edits;
                }
            });

    {
        stdptr<Scene> scene{new Scene("FunnelEditScene")};
        Node* node = scene->createNode("Probe");
        Entity* entity = node->getEntity();
        ASSERT_NE(entity, nullptr);
        entity->addComponent<TransformComponent>();

        auto& registry = scene->getRegistry();
        struct UpdateCounter
        {
            int* count;
            void onUpdate(entt::registry&, entt::entity) { ++*count; }
        };
        UpdateCounter counter{&enttUpdates};
        entt::connection updateConnection =
            registry.on_update<TransformComponent>().connect<&UpdateCounter::onUpdate>(&counter);

        EXPECT_EQ(edits, 0);
        EXPECT_EQ(enttUpdates, 0);
        scene->notifyComponentEdited(entity->getHandle(), type_index_v<TransformComponent>);
        EXPECT_EQ(edits, 1);
        EXPECT_EQ(enttUpdates, 1);

        // Unknown component types and invalid entities are inert no-ops.
        struct Unregistered {};
        (void)sizeof(Unregistered);
        scene->notifyComponentEdited(entity->getHandle(), 987654321u);
        scene->notifyComponentEdited(static_cast<entt::entity>(entt::null),
                                     type_index_v<TransformComponent>);
        EXPECT_EQ(edits, 1);
        EXPECT_EQ(enttUpdates, 1);

        // The connection disconnects before the scene's registry dies
        // (reverse declaration order in this block); no explicit release.
    }

    SceneBus::get().onComponentEdited.remove(editedHandle);
}

} // namespace ya
