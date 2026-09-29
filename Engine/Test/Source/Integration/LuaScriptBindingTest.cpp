// Lua view of the shared script export (rpg-prototype B1, D12).

#include "ECS/Entity.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/LuaScriptBinding.h"
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

} // namespace
} // namespace ya
