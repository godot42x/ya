#include "Core/Scripting/ScriptApiRegistry.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/JSScriptingSystem.h"
#include "Render3D/Scene.h"
#include "Render3D/SceneManager.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ya
{

namespace
{

using Json = ScriptApiRegistry::Json;

unsigned long currentProcessId()
{
#ifdef _WIN32
    return static_cast<unsigned long>(::_getpid());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

class ScriptApiFixture : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        registerCoreScriptApis(ScriptApiRegistry::get());

        _sceneManager = std::make_unique<SceneManager>();
        _scene        = std::make_shared<Scene>("ScriptTest");
        _sceneManager->activateScene(_scene);

        auto& api = ScriptApiRegistry::get();
        api.setActiveSceneProvider([this]() -> Scene* { return _sceneManager->getActiveScene(); });
        api.setSaveSceneFn([this](const std::string& path, Scene& scene) -> bool
                           { return _sceneManager->serializeToFile(path, &scene); });
        api.setLoadSceneFn([this](const std::string& path) -> bool { return _sceneManager->loadScene(path); });
    }

    std::unique_ptr<SceneManager> _sceneManager;
    std::shared_ptr<Scene>        _scene;
};

std::filesystem::path tempScenePath()
{
    return std::filesystem::temp_directory_path() /
           std::format("ya_script_api_test_{}.scene.json", currentProcessId());
}

} // namespace

TEST_F(ScriptApiFixture, RegistryCreatesEntityAndMutatesComponent)
{
    auto& api = ScriptApiRegistry::get();

    Json   result;
    std::string error;
    ASSERT_TRUE(api.invoke("entity.create", {{"name", "Box"}}, result, error)) << error;
    const uint32_t id = result["id"].get<uint32_t>();

    ASSERT_TRUE(api.invoke("component.add", {{"id", id}, {"type", "TransformComponent"}}, result, error)) << error;
    ASSERT_TRUE(api.invoke("component.set",
                           {{"id", id},
                            {"type", "TransformComponent"},
                            {"fields", {{"_position", Json::array({1.0, 2.0, 3.0})}}}},
                           result,
                           error))
        << error;
    EXPECT_EQ(result["_position"], Json::array({1.0, 2.0, 3.0}));

    ASSERT_TRUE(api.invoke("component.get", {{"id", id}, {"type", "TransformComponent"}}, result, error)) << error;
    EXPECT_EQ(result["_position"], Json::array({1.0, 2.0, 3.0}));

    // The write must have reached the actual ECS component.
    Entity* const entity = _scene->getEntityByEnttID(entt::entity{id});
    ASSERT_NE(entity, nullptr);
    auto* const transform = entity->getComponent<TransformComponent>();
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->getPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_F(ScriptApiFixture, RegistryReportsUnknownCommand)
{
    auto& api = ScriptApiRegistry::get();

    Json   result;
    std::string error;
    EXPECT_FALSE(api.invoke("no.such_command", Json::object(), result, error));
    EXPECT_NE(error.find("unknown script api"), std::string::npos);
}

TEST_F(ScriptApiFixture, CommandListExposesCoreApi)
{
    auto& api = ScriptApiRegistry::get();

    const Json commands = api.buildCommandList();
    ASSERT_TRUE(commands.is_array());
    EXPECT_GT(commands.size(), 0);

    for (const std::string& expected : {"scene.get_active", "entity.create", "component.list_types", "scene.save"}) {
        const bool found = std::any_of(commands.begin(), commands.end(), [&](const Json& c) {
            return c.value("name", "") == expected;
        });
        EXPECT_TRUE(found) << "missing command: " << expected;
    }
}

TEST_F(ScriptApiFixture, JsClassBindingCreatesAndMutatesEntity)
{
    JSScriptingSystem system;
    system.init();

    const auto result = system.evalJS(R"(
        const e = ya.entity.create("Box");
        const t = e.addComponentByName("TransformComponent");
        t.setPosition([1, 2, 3]);
        t._rotation = [0, 90, 0];
        t.getPosition()
    )");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value, Json::array({1.0, 2.0, 3.0}));

    // The writes must have reached the actual ECS component through reflection.
    Entity* entity = nullptr;
    for (auto& [handle, candidate] : _scene->_entityMap) {
        (void)handle;
        if (candidate.getName() == "Box") {
            entity = &candidate;
            break;
        }
    }
    ASSERT_NE(entity, nullptr);
    auto* const transform = entity->getComponent<TransformComponent>();
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->getPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(transform->getRotation(), glm::vec3(0.0f, 90.0f, 0.0f));
}

TEST_F(ScriptApiFixture, JsFieldWriteThroughSecondReference)
{
    JSScriptingSystem system;
    system.init();

    const auto result = system.evalJS(R"(
        const e = ya.entity.create("Box");
        const t = e.addComponentByName("TransformComponent");
        t._position = [4, 5, 6];
        const c = e.componentByName("TransformComponent");
        c._position
    )");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value, Json::array({4.0, 5.0, 6.0}));
}

TEST_F(ScriptApiFixture, JsEntitySurface)
{
    JSScriptingSystem system;
    system.init();

    const auto result = system.evalJS(R"(
        const e = ya.entity.create("Box");
        const c = e.components().TransformComponent;
        c.setPosition([7, 8, 9]);
        [e.getId(), e.getName(), c !== undefined, c.getPosition(), ya.scene.active().entityCount()]
    )");
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_TRUE(result.value.is_array());
    EXPECT_TRUE(result.value[0].is_number_integer());
    EXPECT_EQ(result.value[1], "Box");
    EXPECT_EQ(result.value[2], true);
    EXPECT_EQ(result.value[3], Json::array({7.0, 8.0, 9.0}));
    EXPECT_GE(result.value[4], 1);
}

TEST_F(ScriptApiFixture, JsReflectedEntityComponentOps)
{
    JSScriptingSystem system;
    system.init();

    const auto result = system.evalJS(R"(
        const e = ya.entity.create("Ops");
        const before = e.hasComponentByName("TransformComponent");
        const added = e.addComponentByName("TransformComponent") !== undefined;
        const still = e.hasComponentByName("TransformComponent");
        const removed = e.removeComponentByName("TransformComponent");
        const after = e.hasComponentByName("TransformComponent");
        [before, added, still, removed, after]
    )");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value, Json::array({true, true, true, true, false}));

    // The remove must have reached the actual ECS component.
    Entity* entity = nullptr;
    for (auto& [handle, candidate] : _scene->_entityMap) {
        (void)handle;
        if (candidate.getName() == "Ops") {
            entity = &candidate;
            break;
        }
    }
    ASSERT_NE(entity, nullptr);
    EXPECT_FALSE(entity->hasComponent<TransformComponent>());
}

TEST_F(ScriptApiFixture, JsReflectedEntityUnknownTypeErrors)
{
    JSScriptingSystem system;
    system.init();

    const auto result = system.evalJS(R"(
        const e = ya.entity.create("Box");
        e.addComponentByName("NoSuchComponent")
    )");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("unknown component type"), std::string::npos);
}

TEST_F(ScriptApiFixture, JsErrorsPropagate)
{
    JSScriptingSystem system;
    system.init();

    const auto result = system.evalJS("ya.entity.get(999999)");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("not found"), std::string::npos);

    const auto syntax = system.evalJS("this is not valid js (");
    EXPECT_FALSE(syntax.ok);
}

TEST_F(ScriptApiFixture, SceneSaveLoadRoundTrip)
{
    auto& api = ScriptApiRegistry::get();

    Json   result;
    std::string error;
    ASSERT_TRUE(api.invoke("entity.create", {{"name", "Persisted"}}, result, error)) << error;

    const auto path = tempScenePath();
    ASSERT_TRUE(api.invoke("scene.save", {{"path", path.string()}}, result, error)) << error;
    ASSERT_TRUE(api.invoke("scene.load", {{"path", path.string()}}, result, error)) << error;

    ASSERT_TRUE(api.invoke("entity.list", Json::object(), result, error)) << error;
    const bool found = std::any_of(result.begin(), result.end(), [](const Json& e) {
        return e.value("name", "") == "Persisted";
    });
    EXPECT_TRUE(found) << "entity was not restored after scene reload: " << result.dump();

    std::filesystem::remove(path);
}

} // namespace ya
