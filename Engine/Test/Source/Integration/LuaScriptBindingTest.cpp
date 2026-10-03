// Lua view of the shared script export (rpg-prototype B1, D12).

#include "Core/Scripting/ScriptBindings.h"
#include "Scene2D/TilemapComponent.h"
#include "ECS/Entity.h"
#include "ECS/SceneBus.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/LuaScriptBinding.h"
#include "Core/Common/AssetTypeRegistry.h"
#include "Resource/AssetManager.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

class LuaScriptBindingTest : public ::testing::Test
{
  protected:
    Scene     _scene{"LuaBinding"};
    sol::state _lua;
    Entity*   _entity = nullptr;

    void SetUp() override
    {
        _lua.open_libraries(sol::lib::base, sol::lib::string);
        registerLuaScriptBindings(_lua);
        _entity        = _scene.createNode3D("Hero")->getEntity();
        _lua["entity"] = LuaScriptObject{script::entityRef(_entity)};
    }

    sol::protected_function_result run(const std::string& source) { return _lua.safe_script(source, sol::script_pass_on_error); }
};

TEST_F(LuaScriptBindingTest, ReflectedMembersReachTheComponents)
{
    const auto result = run(R"(
        local t = entity:getTransform()
        t.position = Vec3.new(1, 2, 3)
        t:setRotation(Vec3.new(0, 90, 0))
        local camera = entity:add("CameraComponent")
        camera.projection = 1
        camera.bPrimary = true
        return entity:getName(), t.position.y, entity:hasCamera(), camera.projection
    )");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_EQ(result.get<std::string>(0), "Hero");
    EXPECT_DOUBLE_EQ(result.get<double>(1), 2.0);
    EXPECT_TRUE(result.get<bool>(2));
    EXPECT_EQ(result.get<int>(3), 1);

    auto* transform = _entity->getComponent<TransformComponent>();
    EXPECT_EQ(transform->getPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(transform->getRotation(), glm::vec3(0.0f, 90.0f, 0.0f));
    auto* camera = _entity->getComponent<CameraComponent>();
    EXPECT_EQ(camera->_projection, ECameraProjection::Orthographic);
    EXPECT_TRUE(camera->bPrimary);
}

TEST_F(LuaScriptBindingTest, MissingComponentIsNilAndRefsCompareByIdentity)
{
    const auto result = run(R"(
        local a = entity:getTransform()
        local b = entity:get("TransformComponent")
        return entity:getCamera() == nil, a == b, a ~= entity
    )");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(result.get<bool>(0));
    EXPECT_TRUE(result.get<bool>(1));
    EXPECT_TRUE(result.get<bool>(2));
}

TEST_F(LuaScriptBindingTest, MembersAreCachedPerType)
{
    _lua["other"] = LuaScriptObject{script::entityRef(_scene.createNode3D("Other")->getEntity())};
    const auto result = run(R"(
        local getName = entity.getName
        local a, b = entity:getTransform(), other:getTransform()
        local _ = a.position
        return rawequal(getName, entity.getName), rawequal(getName, other.getName), getName(other),
               rawequal(a.getPosition, b.getPosition), b.position.x, getmetatable(entity)
    )");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(result.get<bool>(0));
    EXPECT_TRUE(result.get<bool>(1));
    EXPECT_EQ(result.get<std::string>(2), "Other");
    EXPECT_TRUE(result.get<bool>(3));
    EXPECT_DOUBLE_EQ(result.get<double>(4), 0.0);
    EXPECT_FALSE(result.get<bool>(5));
}

TEST_F(LuaScriptBindingTest, ModuleFunctionsBecomeGlobalTables)
{
    script::registerModuleFunction("probeLua", "sum", [](script::ScriptArgs args) -> script::ScriptValue {
        return script::scriptToNumber(args[0]) + script::scriptToNumber(args[1]);
    });
    script::registerModuleFunction("probeLua", "echo", [](script::ScriptArgs args) -> script::ScriptValue { return args[0]; });
    script::registerModuleFunction("probeLua", "fail", [](script::ScriptArgs) -> script::ScriptValue {
        throw script::ScriptError("boom");
    });
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    lua["probeLua"] = lua.create_table_with("kept", true);
    registerLuaScriptBindings(lua);
    lua["entity"] = LuaScriptObject{script::entityRef(_entity)};

    const auto result = lua.safe_script("return probeLua.sum(1, 2), probeLua.echo(entity) == entity, probeLua.kept",
                                        sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_DOUBLE_EQ(result.get<double>(0), 3.0);
    EXPECT_TRUE(result.get<bool>(1));
    EXPECT_TRUE(result.get<bool>(2));

    const auto failed = lua.safe_script("probeLua.fail()", sol::script_pass_on_error);
    ASSERT_FALSE(failed.valid());
    EXPECT_NE(std::string(sol::error(failed).what()).find("probeLua.fail: boom"), std::string::npos);
}

TEST_F(LuaScriptBindingTest, MistakesRaiseLuaErrors)
{
    for (const char* source : {
             "return entity.noSuchField",
             "entity:getTransform().localDirty = true",
             "entity:getTransform().position = 'up'",
             "return entity.getName()",
             "return entity.getName(entity:getTransform())",
             "entity.getName = 1",
             "return entity:get('NoSuchComponent')",
         }) {
        const auto result = run(source);
        EXPECT_FALSE(result.valid()) << source;
    }
}

TEST_F(LuaScriptBindingTest, DestroyedEntityRaisesInsteadOfDangling)
{
    ASSERT_TRUE(run("transform = entity:getTransform(); local _ = transform.position, entity:getName()").valid());
    _scene.destroyEntity(_entity);

    const auto name = run("return entity:getName()");
    ASSERT_FALSE(name.valid());
    EXPECT_NE(std::string(sol::error(name).what()).find("no longer exists"), std::string::npos);
    EXPECT_FALSE(run("return transform.position").valid());
}

// A script field write lands outside any typed setter: it routes through the
// scene edit funnel (resource-handle-events H2 follow-up) so derived-work
// processors hear it from the one signal source.
TEST_F(LuaScriptBindingTest, ComponentFieldWriteRoutesThroughTheSceneEditFunnel)
{
    auto* camera = _entity->addComponent<CameraComponent>();

    int         edits   = 0;
    DelegateHandle handle = SceneBus::get().onComponentEdited.addLambda(
        [&](entt::registry&, entt::entity, ya::type_index_t type) {
            if (type == type_index_v<CameraComponent>) {
                ++edits;
            }
        });

    const auto result = run(R"(
        local camera = entity:get("CameraComponent")
        camera.bPrimary = true
        return camera.bPrimary
    )");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(result.get<bool>(0));
    EXPECT_TRUE(camera->bPrimary);
    EXPECT_EQ(edits, 1);

    SceneBus::get().onComponentEdited.remove(handle);
}

// A non-const method is the same edit as a field write. Const methods stay
// quiet. Const comes from the reflected member-pointer kind.
TEST_F(LuaScriptBindingTest, ComponentMethodCallRoutesThroughTheSceneEditFunnel)
{
    auto* transform = _entity->getComponent<TransformComponent>();

    int            edits  = 0;
    DelegateHandle handle = SceneBus::get().onComponentEdited.addLambda(
        [&](entt::registry&, entt::entity, ya::type_index_t type) {
            if (type == type_index_v<TransformComponent>) {
                ++edits;
            }
        });

    const auto read = run(R"(
        local t = entity:getTransform()
        return t:getPosition().x
    )");
    ASSERT_TRUE(read.valid()) << sol::error(read).what();
    EXPECT_DOUBLE_EQ(read.get<double>(0), 0.0);
    EXPECT_EQ(edits, 0);

    const auto write = run(R"(
        local t = entity:getTransform()
        t:setPosition(Vec3.new(4, 5, 6))
        return t:getPosition().y
    )");
    ASSERT_TRUE(write.valid()) << sol::error(write).what();
    EXPECT_DOUBLE_EQ(write.get<double>(0), 5.0);
    EXPECT_EQ(transform->getPosition(), glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(edits, 1);

    SceneBus::get().onComponentEdited.remove(handle);
}

// TilemapComponent's movement query face (isSolid / worldToCell / cellToWorld
// / bounds) reaches scripts through reflection like any other method, so a
// gameplay script asks the map directly (rpg-prototype R1c).
TEST_F(LuaScriptBindingTest, TilemapQueryFaceReachesLua)
{
    auto* map = _entity->addComponent<TilemapComponent>();
    ASSERT_NE(map, nullptr);
    map->width      = 3;
    map->height     = 3;
    map->cellSize   = glm::vec2(1.0f, 1.0f);
    map->_editWidth = 3;
    map->_editHeight = 3;
    map->layers.push_back(TilemapLayer{.name = "Ground", .cells = std::vector<int32_t>(9, 1)});
    ASSERT_TRUE(map->setCell(2, 1, 0, 5)); // tile 4
    auto tileset        = std::make_shared<Tileset>();
    tileset->solidTiles = {4};
    AssetTypeRegistry::get().store<Tileset>()->registerAsset("LuaTilesetQueryTest", tileset);
    map->tileset.setPath("LuaTilesetQueryTest");

    const auto result = run(R"(
        local map = entity:getTilemap()
        local cell = map:worldToCell(Vec2.new(2.5, 1.5))
        return cell.x, cell.y, map:isSolid(2, 1), map:isSolid(0, 0), map:isSolid(-1, 0)
    )");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_FLOAT_EQ(result.get<double>(0), 2.0);
    EXPECT_FLOAT_EQ(result.get<double>(1), 1.0);
    EXPECT_TRUE(result.get<bool>(2));
    EXPECT_FALSE(result.get<bool>(3));
    EXPECT_TRUE(result.get<bool>(4));
}

} // namespace
} // namespace ya
