#include "Foundation/Core/Scripting/ScriptApiAsset.h"
#include "Foundation/Core/Scripting/ScriptApiRegistry.h"
#include "Foundation/Core/System/VirtualFileSystem.h"
#include "Framework/Game/Gameplay/ECS/Component/TransformComponent.h"
#include "Framework/Game/Gameplay/ECS/Entity.h"
#include "Framework/Game/Gameplay/ECS/System/JSScriptingSystem.h"
#include "Framework/Game/Render/Render3D/Scene.h"
#include "Framework/Game/Render/Render3D/SceneManager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
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

class ScriptApiLibraryFixture : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (VirtualFileSystem::get() == nullptr) {
            VirtualFileSystem::init();
        }

        registerCoreScriptApis(ScriptApiRegistry::get());
        registerAssetScriptApis(ScriptApiRegistry::get());

        _sceneManager = std::make_unique<SceneManager>();
        _scene        = std::make_shared<Scene>("LibraryTest");
        _sceneManager->activateScene(_scene);

        auto& api = ScriptApiRegistry::get();
        api.setActiveSceneProvider([this]() -> Scene* { return _sceneManager->getActiveScene(); });
        api.setSaveSceneFn([this](const std::string& path, Scene& scene) -> bool
                           { return _sceneManager->serializeToFile(path, &scene); });
        api.setLoadSceneFn([this](const std::string& path) -> bool { return _sceneManager->loadScene(path); });

        _system.init();
    }

    void TearDown() override { _system.shutdown(); }

    JSScriptingSystem          _system;
    std::unique_ptr<SceneManager> _sceneManager;
    std::shared_ptr<Scene>     _scene;
};

std::filesystem::path tempAssetPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() /
           std::format("ya_library_test_{}_{}", currentProcessId(), name);
}

} // namespace

TEST_F(ScriptApiLibraryFixture, RegistryCommandsExportAsLibraryNamespaces)
{
    const auto result = _system.evalJS(R"(
        [
            typeof ya.component.list_types,
            typeof ya.component.add,
            typeof ya.asset.get_info,
            typeof ya.asset.stats,
            typeof ya.scene.get_active,
            typeof ya.entity.destroy
        ]
    )");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value, Json::array({"function",
                                         "function",
                                         "function",
                                         "function",
                                         "function",
                                         "function"}));
}

TEST_F(ScriptApiLibraryFixture, NoArgCommandReturnsRegistryResult)
{
    const auto result = _system.evalJS(R"(
        const types = ya.component.list_types();
        const scene = ya.scene.get_active();
        [types.includes("TransformComponent"), scene.name, scene.entityCount]
    )");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value, Json::array({true, "LibraryTest", 0}));
}

TEST_F(ScriptApiLibraryFixture, SingleArgCommandMapsPositionally)
{
    const auto result = _system.evalJS(R"(
        const e = ya.entity.create("ToDestroy");
        const id = e.getId();
        const removed = ya.entity.destroy(id);
        const names = ya.entity.list().map(e => e.getName());
        [removed.destroyed, names.includes("ToDestroy")]
    )");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value, Json::array({true, false}));
}

TEST_F(ScriptApiLibraryFixture, MultiArgCommandTakesParamsObject)
{
    const auto result = _system.evalJS(R"(
        const e = ya.entity.create("LibBox");
        const id = e.getId();
        const set = ya.component.set({id: id, type: "TransformComponent", fields: {_position: [1, 2, 3]}});
        const get = ya.component.get({id: id, type: "TransformComponent"});
        [set._position, get._position]
    )");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value, Json::array({Json::array({1.0, 2.0, 3.0}), Json::array({1.0, 2.0, 3.0})}));

    // The write must have reached the actual ECS component.
    Entity* entity = nullptr;
    for (auto& [handle, candidate] : _scene->_entityMap) {
        (void)handle;
        if (candidate.getName() == "LibBox") {
            entity = &candidate;
            break;
        }
    }
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->getComponent<TransformComponent>()->getPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_F(ScriptApiLibraryFixture, MultiArgCommandRejectsPositionalArgs)
{
    const auto result = _system.evalJS("ya.component.get(999999, \"TransformComponent\")");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("pass a single object"), std::string::npos);
}

TEST_F(ScriptApiLibraryFixture, FailingCommandThrowsJsError)
{
    const auto result = _system.evalJS(R"(
        ya.component.get({id: 999999, type: "TransformComponent"})
    )");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("not found"), std::string::npos);
}

TEST_F(ScriptApiLibraryFixture, ModuleFunctionsTakePrecedenceOverRegistry)
{
    const auto result = _system.evalJS(R"(
        const e = ya.entity.create("Precedence");
        [typeof e.getId, typeof e.addComponentByName, e.components().TransformComponent !== undefined]
    )");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value, Json::array({"function", "function", true}));
}

TEST_F(ScriptApiLibraryFixture, AssetLibraryInspectsPaths)
{
    const auto assetPath = tempAssetPath("texture.png");
    {
        std::ofstream stream(assetPath);
        ASSERT_TRUE(stream.is_open());
        stream << "fake png bytes";
    }

    const std::string quotedPath = assetPath.generic_string();
    const auto result = _system.evalJS(
        "const info = ya.asset.get_info(\"" + quotedPath + "\");\n"
        "const stats = ya.asset.stats();\n"
        "[info.type, info.resourceVersion, info.textureLoaded, info.modelLoaded, stats.textureCount, stats.modelCount]");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.value,
              Json::array({"texture", 0, false, false, 0, 0}));

    std::filesystem::remove(assetPath);
}

TEST_F(ScriptApiLibraryFixture, AssetLibraryRejectsMissingPathParam)
{
    const auto result = _system.evalJS("ya.asset.get_info()");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("requires string param 'path'"), std::string::npos);
}

} // namespace ya
