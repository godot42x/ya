// RR0 scene and asset resave baseline. Reads tracked Example files and never
// writes them back. The recorded hash is FNV-1a of the first
// deserialize → serialize text. A second round must produce the same text.
//
// Number rounding inside SceneSerializer::saveToFile is private. This baseline
// is serialize() + SceneSerializer::normalizePaths + dumpJsonCompactLeaves.

#include "Core/Common/JsonFormat.h"
#include "Core/Common/SpriteAnimationSet.h"
#include "Core/Common/Tileset.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/System/VirtualFileSystem.h"
#include "GUI/Widgets/UIDocument.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "TestSource.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace ya::rr0
{
namespace
{

void prepareFilesystem()
{
    if (!VirtualFileSystem::get()) {
        VirtualFileSystem::init();
    }
    reflection::DeferredInitializerQueue::instance().executeAll();
}

[[nodiscard]] std::string readRepoFile(const char* relative)
{
    const std::filesystem::path path = ya::test::repoRoot() / relative;
    std::ifstream               in(path);
    EXPECT_TRUE(in.good()) << path.string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

[[nodiscard]] uint64_t fnv1a(std::string_view text)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] std::string hexHash(std::string_view text)
{
    return std::format("{:016x}", fnv1a(text));
}

[[nodiscard]] std::string resaveScene(const char* relative)
{
    const nlohmann::json loaded = nlohmann::json::parse(readRepoFile(relative));
    Scene                scene("rr0");
    SceneSerializer(&scene).deserialize(loaded);
    nlohmann::json saved = SceneSerializer(&scene).serialize();
    SceneSerializer::normalizePaths(saved);
    return dumpJsonCompactLeaves(saved);
}

void expectSameText(const char* relative, std::string_view first, std::string_view second)
{
    if (first == second) {
        return;
    }
    const size_t limit = std::min(first.size(), second.size());
    size_t       index = 0;
    while (index < limit && first[index] == second[index]) {
        ++index;
    }
    ADD_FAILURE() << relative << " resave is not idempotent (" << first.size() << " vs " << second.size()
                  << " bytes, first difference at " << index << ")\n"
                  << std::string(first.substr(index, 180)) << "\n---\n"
                  << std::string(second.substr(index, 180));
}

void expectIdempotentHash(const char* relative, std::string_view first, const char* expectedHash)
{
    const nlohmann::json again = nlohmann::json::parse(first);
    Scene                scene("rr0-again");
    SceneSerializer(&scene).deserialize(again);
    nlohmann::json saved = SceneSerializer(&scene).serialize();
    SceneSerializer::normalizePaths(saved);
    const std::string second = dumpJsonCompactLeaves(saved);
    expectSameText(relative, first, second);
    EXPECT_EQ(hexHash(first), expectedHash) << relative << " hash " << hexHash(first);
}

struct SceneBaseline
{
    const char* path;
    const char* hash;
};

constexpr SceneBaseline kScenes[] = {
    {"Example/2dRpgPrototype/Content/Scenes/House.scene.json", "21aa871b08136afd"},
    {"Example/2dRpgPrototype/Content/Scenes/PropertyOverride.scene.json", "78cd79bee9c492ac"},
    {"Example/2dRpgPrototype/Content/Scenes/Town.scene.json", "eb5b809a5adf0c5a"},
    {"Example/2dRpgPrototype/Content/Scenes/TownLarge.scene.json", "135679ba934a6c3a"},
    {"Example/GreedySnake/Content/Scenes/GreedySnake.scene.json", "a47ef4c683f38988"},
    {"Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json", "94cafe06d86b9d46"},
    {"Example/PerfLab/Content/Scenes/Lights_50.scene.json", "5562c4d3845e6583"},
    {"Example/PerfLab/Content/Scenes/Lights_200.scene.json", "a7c6895c5ff0c3e9"},
    {"Example/PerfLab/Content/Scenes/Lights_500.scene.json", "d203d306505ee627"},
    {"Example/PerfLab/Content/Scenes/Meshes_1000.scene.json", "62537d23032655ae"},
    {"Example/PerfLab/Content/Scenes/Sponza.scene.json", "2814a9ffab5e5fee"},
    {"Example/PerfLab/Content/Scenes/Sprites_500.scene.json", "bd5667e0f4fd32ee"},
};

constexpr SceneBaseline kHeavyScenes[] = {
    {"Example/PerfLab/Content/Scenes/Meshes_5000.scene.json", "e570dde7f501b1c4"},
    {"Example/PerfLab/Content/Scenes/Meshes_10000.scene.json", "d9be7bad3bd83dfc"},
    {"Example/PerfLab/Content/Scenes/Sprites_2000.scene.json", "a86c07e61cec8de4"},
    {"Example/PerfLab/Content/Scenes/Sprites_5000.scene.json", "884de2b7cf132ba7"},
};

constexpr const char* kAnimationSets[] = {
    "Example/2dRpgPrototype/Content/Animations/Chest.yaanim.json",
    "Example/2dRpgPrototype/Content/Animations/Hero.yaanim.json",
    "Example/2dRpgPrototype/Content/Animations/Npc.yaanim.json",
};

constexpr const char* kAnimationHashes[] = {"c5eb0233912113ec", "17ecac8fc2efcaa6", "c6882b9d89542979"};

constexpr const char* kUiDocuments[] = {
    "Example/2dRpgPrototype/Content/UI/Dialogue.yaui.json",
    "Example/GreedySnake/Content/UI/GameOver.yaui.json",
    "Example/GreedySnake/Content/UI/HUD.yaui.json",
    "Example/GreedySnake/Content/UI/Settings.yaui.json",
    "Example/HelloMaterial/Content/UI/Panel1.yaui.json",
    "Example/HelloMaterial/Content/UI/Panel2.yaui.json",
};

constexpr const char* kUiHashes[] = {
    "38aff1e896d08f66",
    "a2e26278553915ec",
    "6e5d0dc6dbcfdae7",
    "61e2906040907fbb",
    "7878cd8986966a33",
    "a50599cedebc3826",
};

constexpr const char* kTilesets[] = {
    "Example/2dRpgPrototype/Content/Tilesets/town.yatileset.json",
    "Example/PerfLab/Content/Tilesets/town.yatileset.json",
};

constexpr const char* kTilesetAtlasHashes[] = {"86f92467c7828373", "e3bf7385660954c2"};

[[nodiscard]] std::string resaveAnimation(const char* relative)
{
    std::string error;
    const auto  set = parseSpriteAnimationSetJson(readRepoFile(relative), error);
    EXPECT_NE(set, nullptr) << relative << " " << error;
    if (!set) {
        return {};
    }
    return serializeSpriteAnimationSetJson(*set);
}

[[nodiscard]] std::string resaveUi(const char* relative)
{
    const auto document = UIDocument::fromJson(nlohmann::json::parse(readRepoFile(relative)));
    EXPECT_NE(document, nullptr) << relative;
    if (!document) {
        return {};
    }
    const UIElementRef live = document->instantiate();
    EXPECT_TRUE(static_cast<bool>(live)) << relative;
    if (!live) {
        return {};
    }
    const auto saved = UIDocument::fromWidget(*live);
    EXPECT_NE(saved, nullptr) << relative;
    if (!saved) {
        return {};
    }
    return dumpJsonCompactLeaves(saved->toJson());
}

[[nodiscard]] std::string resaveTilesetAtlas(const char* relative)
{
    std::string error;
    const auto  tileset = parseTilesetJson(readRepoFile(relative), error);
    EXPECT_NE(tileset, nullptr) << relative << " " << error;
    if (!tileset) {
        return {};
    }
    const nlohmann::json atlas = ReflectionSerializer::serializeByRuntimeReflection(tileset->atlas, "TextureSlot");
    return dumpJsonCompactLeaves(atlas);
}

} // namespace

TEST(ReflectionSceneResave, TrackedScenesMatchBaseline)
{
    prepareFilesystem();
    for (const SceneBaseline& scene : kScenes) {
        SCOPED_TRACE(scene.path);
        const std::string first = resaveScene(scene.path);
        expectIdempotentHash(scene.path, first, scene.hash);
    }
}

// Each stress scene is its own test. First-pass resave on this machine was
// 374–849 ms, and the second round stayed under 2 s, so they run in the
// default suite.
TEST(ReflectionSceneResave, PerfLabMeshes5000)
{
    prepareFilesystem();
    const std::string first = resaveScene(kHeavyScenes[0].path);
    expectIdempotentHash(kHeavyScenes[0].path, first, kHeavyScenes[0].hash);
}

TEST(ReflectionSceneResave, PerfLabMeshes10000)
{
    prepareFilesystem();
    const std::string first = resaveScene(kHeavyScenes[1].path);
    expectIdempotentHash(kHeavyScenes[1].path, first, kHeavyScenes[1].hash);
}

TEST(ReflectionSceneResave, PerfLabSprites2000)
{
    prepareFilesystem();
    const std::string first = resaveScene(kHeavyScenes[2].path);
    expectIdempotentHash(kHeavyScenes[2].path, first, kHeavyScenes[2].hash);
}

TEST(ReflectionSceneResave, PerfLabSprites5000)
{
    prepareFilesystem();
    const std::string first = resaveScene(kHeavyScenes[3].path);
    expectIdempotentHash(kHeavyScenes[3].path, first, kHeavyScenes[3].hash);
}

TEST(ReflectionSceneResave, SpriteAnimationSetsMatchBaseline)
{
    prepareFilesystem();
    for (size_t index = 0; index < std::size(kAnimationSets); ++index) {
        SCOPED_TRACE(kAnimationSets[index]);
        const std::string first = resaveAnimation(kAnimationSets[index]);
        ASSERT_FALSE(first.empty());
        std::string error;
        const auto  again = parseSpriteAnimationSetJson(first, error);
        ASSERT_NE(again, nullptr) << error;
        expectSameText(kAnimationSets[index], first, serializeSpriteAnimationSetJson(*again));
        EXPECT_EQ(hexHash(first), kAnimationHashes[index]) << kAnimationSets[index] << " hash " << hexHash(first);
    }
}

TEST(ReflectionSceneResave, UiDocumentsMatchBaseline)
{
    prepareFilesystem();
    for (size_t index = 0; index < std::size(kUiDocuments); ++index) {
        SCOPED_TRACE(kUiDocuments[index]);
        const std::string first = resaveUi(kUiDocuments[index]);
        ASSERT_FALSE(first.empty());
        const auto document = UIDocument::fromJson(nlohmann::json::parse(first));
        ASSERT_NE(document, nullptr);
        const UIElementRef live = document->instantiate();
        ASSERT_TRUE(static_cast<bool>(live));
        const auto saved = UIDocument::fromWidget(*live);
        ASSERT_NE(saved, nullptr);
        expectSameText(kUiDocuments[index], first, dumpJsonCompactLeaves(saved->toJson()));
        EXPECT_EQ(hexHash(first), kUiHashes[index]) << kUiDocuments[index] << " hash " << hexHash(first);
    }
}

TEST(ReflectionSceneResave, TilesetAtlasReflectionMatchesBaseline)
{
    prepareFilesystem();
    for (size_t index = 0; index < std::size(kTilesets); ++index) {
        SCOPED_TRACE(kTilesets[index]);
        const std::string first = resaveTilesetAtlas(kTilesets[index]);
        ASSERT_FALSE(first.empty());
        TextureSlot slot;
        ReflectionSerializer::deserializeByRuntimeReflection(slot, nlohmann::json::parse(first), "TextureSlot");
        const std::string second = dumpJsonCompactLeaves(ReflectionSerializer::serializeByRuntimeReflection(slot, "TextureSlot"));
        expectSameText(kTilesets[index], first, second);
        EXPECT_EQ(hexHash(first), kTilesetAtlasHashes[index]) << kTilesets[index] << " hash " << hexHash(first);
    }
}

} // namespace ya::rr0
