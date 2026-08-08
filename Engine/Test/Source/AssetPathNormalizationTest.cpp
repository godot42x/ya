#include "Foundation/Core/System/VirtualFileSystem.h"
#include "Framework/Game/Resource/AssetManager.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace ya
{
namespace
{

class AssetPathNormalizationTest : public ::testing::Test
{
  protected:
    std::filesystem::path _originalCwd;
    std::filesystem::path _tempRoot;
    std::filesystem::path _gameRoot;

    void SetUp() override
    {
        _originalCwd = std::filesystem::current_path();
        _tempRoot    = std::filesystem::temp_directory_path() /
                    std::filesystem::path("ya-asset-path-normalization-test-" +
                                          std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                                          std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        _gameRoot    = _tempRoot / "Example" / "PathGame";

        std::filesystem::remove_all(_tempRoot);
        std::filesystem::create_directories(_tempRoot / "Engine" / "Content" / "Textures");
        std::filesystem::create_directories(_tempRoot / "Engine" / "ThirdParty" / "Assets");
        std::filesystem::create_directories(_gameRoot / "Content" / "Textures");
        std::filesystem::current_path(_tempRoot);

        VirtualFileSystem::init();
        ASSERT_NE(VirtualFileSystem::get(), nullptr);
        VirtualFileSystem::get()->setGameRoot(_gameRoot);
    }

    void TearDown() override
    {
        std::filesystem::current_path(_originalCwd);
        std::filesystem::remove_all(_tempRoot);
    }
};

TEST_F(AssetPathNormalizationTest, NormalizeAssetPathCanonicalizesLegacyAndAbsoluteForms)
{
    const auto engineContentAbs = (_tempRoot / "Engine" / "Content" / "Textures" / "sky.hdr").lexically_normal();
    const auto gameContentAbs   = (_gameRoot / "Content" / "Textures" / "albedo.png").lexically_normal();
    const auto thirdPartyAbs    = (_tempRoot / "Engine" / "ThirdParty" / "Assets" / "mesh.obj").lexically_normal();

    EXPECT_EQ(AssetManager::normalizeAssetPath("Engine/Content/Textures/sky.hdr"), "Engine:Content/Textures/sky.hdr");
    EXPECT_EQ(AssetManager::normalizeAssetPath("Engine:Content/Content/Textures/sky.hdr"), "Engine:Content/Textures/sky.hdr");
    EXPECT_EQ(AssetManager::normalizeAssetPath("Game:Content/Textures/albedo.png"), "Content/Textures/albedo.png");
    EXPECT_EQ(AssetManager::normalizeAssetPath("Content/Content/Textures/albedo.png"), "Content/Textures/albedo.png");
    EXPECT_EQ(AssetManager::normalizeAssetPath(engineContentAbs.generic_string()), "Engine:Content/Textures/sky.hdr");
    EXPECT_EQ(AssetManager::normalizeAssetPath(gameContentAbs.generic_string()), "Content/Textures/albedo.png");
    EXPECT_EQ(AssetManager::normalizeAssetPath(thirdPartyAbs.generic_string()), "Engine/ThirdParty/Assets/mesh.obj");
}

TEST_F(AssetPathNormalizationTest, VfsSeparatesLogicalPathsFromIoTranslation)
{
    auto* vfs = VirtualFileSystem::get();
    ASSERT_NE(vfs, nullptr);

    // VFS roots are captured from current_path(), which resolves symlinks
    // (e.g. /var -> /private/var on macOS). Compare against the resolved
    // physical path so the test is symlink-agnostic.
    const auto engineContentAbs = std::filesystem::weakly_canonical(_tempRoot / "Engine" / "Content" / "Textures" / "sky.hdr");
    const auto gameContentAbs   = std::filesystem::weakly_canonical(_gameRoot / "Content" / "Textures" / "albedo.png");
    const auto thirdPartyAbs    = std::filesystem::weakly_canonical(_tempRoot / "Engine" / "ThirdParty" / "Assets" / "mesh.obj");

    EXPECT_EQ(vfs->toVfsPath(engineContentAbs.generic_string()), "Engine:Content/Textures/sky.hdr");
    EXPECT_EQ(vfs->toVfsPath(gameContentAbs.generic_string()), "Content/Textures/albedo.png");
    EXPECT_EQ(vfs->toVfsPath(thirdPartyAbs.generic_string()), "Engine/ThirdParty/Assets/mesh.obj");

    EXPECT_EQ(vfs->translatePath("Engine:Content/Textures/sky.hdr").lexically_normal(), engineContentAbs);
    EXPECT_EQ(vfs->translatePath("Content/Textures/albedo.png").lexically_normal(), gameContentAbs);
    EXPECT_EQ(vfs->translatePath("Engine/ThirdParty/Assets/mesh.obj").lexically_normal(), thirdPartyAbs);
}

} // namespace
} // namespace ya
