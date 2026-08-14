#include "App/Module/ModuleManager.h"
#include "Core/TypeIndex.h"

#include <gtest/gtest.h>
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_filesystem.h>

#include <filesystem>
#include <fstream>

namespace ya
{
namespace
{

struct FFixtureState
{
    int sequence;
    int created;
    int loaded;
    int queried;
    int started;
    int stopped;
    int unloaded;
    int destroyed;
};

class ModuleManagerTest : public ::testing::Test
{
  protected:
    std::filesystem::path root;

    void SetUp() override
    {
        root = std::filesystem::temp_directory_path() /
               ("ya-module-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        std::filesystem::create_directories(root);
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path writeManifest(const std::string& name,
                                        const std::string& kind,
                                        const std::string& dependencies = "[]",
                                        const std::string& binary = "") const
    {
        const auto path = root / (name + ".yamodule");
        std::ofstream stream(path);
        stream << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"name\": \"" << name << "\",\n"
               << "  \"kind\": \"" << kind << "\",\n"
               << "  \"binary\": \"" << (binary.empty() ? name : binary) << "\",\n"
               << "  \"dependencies\": " << dependencies << "\n"
               << "}\n";
        return path;
    }
};

TEST(TypeIndexTest, IsDeterministicAnd64Bit)
{
    static_assert(sizeof(type_index_t) == sizeof(uint64_t));
    constexpr auto first  = type_index_v<int>;
    constexpr auto second = TypeIndex<int>::value();
    EXPECT_EQ(first, second);
    EXPECT_NE(type_index_v<int>, type_index_v<float>);
}

TEST(ModuleApiTest, BuildFingerprintIncludesCurrentBinaryContract)
{
    EXPECT_NE(YA_MODULE_BUILD_FINGERPRINT, 0u);
    EXPECT_STRNE(YA_MODULE_TOOLCHAIN, "unknown-toolchain");
    EXPECT_STRNE(YA_MODULE_ARCHITECTURE, "unknown-arch");
    EXPECT_STRNE(YA_MODULE_BUILD_MODE, "unknown");
}

TEST_F(ModuleManagerTest, ResolvesRequiredBeforeConsumerAndSkipsMissingOptional)
{
    ModuleManager manager;
    ASSERT_TRUE(manager.addManifest(writeManifest("CoreGameplay", "runtime"))) << manager.getLastError();
    ASSERT_TRUE(manager.addManifest(writeManifest(
        "Game",
        "project",
        R"([{"name":"CoreGameplay","required":true},{"name":"MissingDebug","required":false}])")))
        << manager.getLastError();

    ASSERT_TRUE(manager.resolve({"Game"})) << manager.getLastError();
    EXPECT_EQ(manager.getResolvedOrder(), (std::vector<std::string>{"CoreGameplay", "Game"}));
}

TEST_F(ModuleManagerTest, RejectsCycles)
{
    ModuleManager manager;
    ASSERT_TRUE(manager.addManifest(writeManifest("A", "runtime", R"([{"name":"B"}])")));
    ASSERT_TRUE(manager.addManifest(writeManifest("B", "runtime", R"([{"name":"A"}])")));

    EXPECT_FALSE(manager.resolve({"A"}));
    EXPECT_NE(manager.getLastError().find("A -> B -> A"), std::string::npos);
}

TEST_F(ModuleManagerTest, RejectsRuntimeDependencyOnEditor)
{
    ModuleManager manager;
    ASSERT_TRUE(manager.addManifest(writeManifest("EditorTools", "editor")));
    ASSERT_TRUE(manager.addManifest(writeManifest("RuntimePlugin", "runtime", R"([{"name":"EditorTools"}])")));

    EXPECT_FALSE(manager.resolve({"RuntimePlugin"}));
    EXPECT_NE(manager.getLastError().find("cannot depend on editor"), std::string::npos);
}

TEST_F(ModuleManagerTest, LoadsQueriesAndTearsDownDynamicModuleInOrder)
{
#if defined(_WIN32)
    constexpr const char* fixtureBinary = "ya-module-fixture.dll";
#elif defined(__APPLE__)
    constexpr const char* fixtureBinary = "libya-module-fixture.dylib";
#else
    constexpr const char* fixtureBinary = "libya-module-fixture.so";
#endif

    const auto fixturePath = std::filesystem::path(SDL_GetBasePath()) / fixtureBinary;
    SDL_SharedObject* fixtureHandle = SDL_LoadObject(fixturePath.string().c_str());
    ASSERT_NE(fixtureHandle, nullptr) << SDL_GetError();
    auto getState = reinterpret_cast<const FFixtureState* (*)()>(SDL_LoadFunction(fixtureHandle, "yaGetFixtureState"));
    ASSERT_NE(getState, nullptr) << SDL_GetError();

    const auto manifestPath = writeManifest("Fixture", "runtime", "[]", fixtureBinary);
    {
        ModuleManager manager;
        ASSERT_TRUE(manager.addManifest(manifestPath)) << manager.getLastError();
        ASSERT_TRUE(manager.resolve({"Fixture"})) << manager.getLastError();
        ASSERT_TRUE(manager.loadAll()) << manager.getLastError();
        EXPECT_NE(manager.queryInterface("Fixture", makeInterfaceId("ya.test.fixture/1")), nullptr);
        ASSERT_TRUE(manager.startAll({})) << manager.getLastError();
        manager.stopAll();
        manager.unloadAll();
    }

    const auto* state = getState();
    EXPECT_LT(state->created, state->loaded);
    EXPECT_LT(state->loaded, state->queried);
    EXPECT_LT(state->queried, state->started);
    EXPECT_LT(state->started, state->stopped);
    EXPECT_LT(state->stopped, state->unloaded);
    EXPECT_LT(state->unloaded, state->destroyed);
    SDL_UnloadObject(fixtureHandle);
}

} // namespace
} // namespace ya
