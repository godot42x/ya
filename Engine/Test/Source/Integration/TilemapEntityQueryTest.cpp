// `map:entityAt(x, y)` — the cell query that movement blocking and "whom do I
// face" share (rpg-prototype R2a). Actors are entities with a sprite and a
// script; everything else (camera, bare transforms) never answers.

#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Component/2D/Sprite2DComponent.h"
#include "ECS/Component/2D/TilemapComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Scene3D/Node3D.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace ya
{

namespace
{

class TilemapEntityQueryTest : public ::testing::Test
{
  protected:
    Scene             _scene{"Town"};
    Entity*           _ground = nullptr;
    TilemapComponent* _map    = nullptr;

    void SetUp() override
    {
        // Idempotent for the process (first registration wins; the body
        // captures nothing), and safe whether or not an earlier test built
        // the TilemapComponent export already.
        registerGameplayScriptFunctions();
        Node3D* node   = _scene.createNode3D("Ground");
        _ground        = node->getEntity();
        _map           = _ground->addComponent<TilemapComponent>();
        _map->width    = 4;
        _map->height   = 4;
        _map->cellSize = glm::vec2(1.0f, 1.0f);
        _map->_editWidth  = 4;
        _map->_editHeight = 4;
        _map->layers.push_back(TilemapLayer{.name = "Ground", .zOffset = 0.0f, .cells = std::vector<int32_t>(16, 1)});
    }

    /// An actor: a body with a sprite, optionally carrying a script.
    Entity* actorAt(const char* name, float x, float y, bool bScripted = true)
    {
        Node3D* node   = _scene.createNode3D(name);
        Entity* entity = node->getEntity();
        entity->getComponent<TransformComponent>()->setPosition({x, y, 0.1f});
        entity->addComponent<Sprite2DComponent>();
        if (bScripted) {
            entity->addComponent<LuaScriptComponent>();
        }
        return entity;
    }

    script::ScriptValue entityAt(int32_t x, int32_t y)
    {
        const std::vector<script::ScriptValue> args = {script::ScriptValue{int64_t{x}}, script::ScriptValue{int64_t{y}}};
        return script::callMethod(script::componentRef(_ground, type_index_v<TilemapComponent>), "entityAt", args);
    }
};

TEST_F(TilemapEntityQueryTest, ActorInCellIsFound)
{
    Entity* npc = actorAt("Npc", 2.5f, 1.5f);
    const script::ScriptValue found = entityAt(2, 1);
    ASSERT_TRUE(std::holds_alternative<script::ScriptRef>(found));
    EXPECT_EQ(std::get<script::ScriptRef>(found), script::entityRef(npc));

    EXPECT_TRUE(std::holds_alternative<std::monostate>(entityAt(3, 1)));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(entityAt(-1, 1)));
}

TEST_F(TilemapEntityQueryTest, BodiesWithoutScriptsDoNotAnswer)
{
    actorAt("Statue", 1.5f, 2.5f, false); // sprite, no script
    actorAt("Camera", 3.5f, 3.5f, false); // transform only
    EXPECT_TRUE(std::holds_alternative<std::monostate>(entityAt(1, 2)));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(entityAt(3, 3)));
}

} // namespace
} // namespace ya
