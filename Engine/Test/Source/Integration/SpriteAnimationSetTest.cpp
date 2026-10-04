// SpriteAnimationSet is a shared .yaanim.json document: reflection parse and
// serialize, the same frame window as SpriteAnimationComponent, and one slot
// per path through AssetManager.

#include "Core/Common/AssetTypeRegistry.h"
#include "Core/Common/SpriteAnimationSet.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/AssetManager.h"
#include "Scene2D/SpriteAnimationComponent.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace ya
{
namespace
{

class SpriteAnimationSetTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!VirtualFileSystem::get()) {
            VirtualFileSystem::init();
        }
        AssetManager::get()->clearCache();
    }

    void TearDown() override
    {
        AssetManager::get()->clearCache();
    }
};

void expectRejected(const std::string& text, const std::string& needle)
{
    std::string error;
    const auto  parsed = parseSpriteAnimationSetJson(text, error);
    EXPECT_EQ(parsed, nullptr);
    EXPECT_NE(error.find(needle), std::string::npos) << error;
}

const char* kHeroDocument = R"({
    "columns": 3,
    "rows": 4,
    "clips": [
        {"name": "idle_down", "frames": [1], "fps": 1.0, "bLoop": true},
        {"name": "walk_down", "frames": [0, 1, 2, 1], "fps": 8.0, "bLoop": false}
    ]
})";

TEST_F(SpriteAnimationSetTest, ExampleDocumentsMatchSerializerBytes)
{
    const char* paths[] = {
        "Example/2dRpgPrototype/Content/Animations/Hero.yaanim.json",
        "Example/2dRpgPrototype/Content/Animations/Npc.yaanim.json",
        "Example/2dRpgPrototype/Content/Animations/Chest.yaanim.json",
    };
    for (const char* path : paths) {
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open()) << path;
        std::stringstream buffer;
        buffer << input.rdbuf();
        const std::string text = buffer.str();

        std::string error;
        const auto  parsed = parseSpriteAnimationSetJson(text, error);
        ASSERT_NE(parsed, nullptr) << path << ": " << error;
        EXPECT_EQ(text, serializeSpriteAnimationSetJson(*parsed)) << path;
    }
}

TEST_F(SpriteAnimationSetTest, ParseRoundTrip)
{
    std::string error;
    const auto  parsed = parseSpriteAnimationSetJson(kHeroDocument, error);
    ASSERT_NE(parsed, nullptr) << error;
    EXPECT_EQ(parsed->columns, 3);
    EXPECT_EQ(parsed->rows, 4);
    ASSERT_EQ(parsed->clips.size(), 2u);
    EXPECT_EQ(parsed->clips[0].name, "idle_down");
    EXPECT_EQ(parsed->clips[0].frames, std::vector<int32_t>({1}));
    EXPECT_FLOAT_EQ(parsed->clips[0].fps, 1.0f);
    EXPECT_TRUE(parsed->clips[0].bLoop);
    EXPECT_EQ(parsed->clips[1].name, "walk_down");
    EXPECT_EQ(parsed->clips[1].frames, (std::vector<int32_t>{0, 1, 2, 1}));
    EXPECT_FLOAT_EQ(parsed->clips[1].fps, 8.0f);
    EXPECT_FALSE(parsed->clips[1].bLoop);
    ASSERT_NE(parsed->findClip("walk_down"), nullptr);
    EXPECT_EQ(parsed->findClip("walk_down")->frames.size(), 4u);
    EXPECT_EQ(parsed->findClip("missing"), nullptr);
    EXPECT_TRUE(parsed->atlas.empty());

    const auto again = parseSpriteAnimationSetJson(serializeSpriteAnimationSetJson(*parsed), error);
    ASSERT_NE(again, nullptr) << error;
    EXPECT_EQ(again->columns, parsed->columns);
    EXPECT_EQ(again->rows, parsed->rows);
    ASSERT_EQ(again->clips.size(), parsed->clips.size());
    EXPECT_EQ(again->clips[1].frames, parsed->clips[1].frames);
    EXPECT_EQ(again->clips[1].bLoop, parsed->clips[1].bLoop);
}

TEST_F(SpriteAnimationSetTest, RejectsIllegalDocuments)
{
    expectRejected(R"({"columns": 0, "rows": 4, "clips": []})", ">= 1");
    expectRejected(R"({"rows": 4, "clips": []})", "columns");
    expectRejected(R"({"columns": 3, "rows": 4})", "clips");
    {
        std::string error;
        EXPECT_EQ(parseSpriteAnimationSetJson("{", error), nullptr);
        EXPECT_FALSE(error.empty()) << error;
    }
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "frames": [1], "fps": 1.0, "bLoop": true},
            {"name": "idle_down", "frames": [2], "fps": 1.0, "bLoop": true}
        ]})",
        "duplicate clip name");
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "", "frames": [1], "fps": 1.0, "bLoop": true}
        ]})",
        "non-empty");
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "frames": [], "fps": 1.0, "bLoop": true}
        ]})",
        "frames must be non-empty");
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "fps": 1.0, "bLoop": true}
        ]})",
        "frames");
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "frames": [12], "fps": 1.0, "bLoop": true}
        ]})",
        "frame out of range");
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "frames": [0], "fps": 0, "bLoop": true}
        ]})",
        "fps must be > 0");
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "frames": [0], "fps": -1.0, "bLoop": true}
        ]})",
        "fps must be > 0");
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "frames": [0], "fps": "fast", "bLoop": true}
        ]})",
        "fps");
    expectRejected(
        R"({"columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "frames": [0], "fps": 1.0, "bLoop": "yes"}
        ]})",
        "bLoop");
    expectRejected(
        R"({"atlas": 1, "columns": 3, "rows": 4, "clips": [
            {"name": "idle_down", "frames": [0], "fps": 1.0, "bLoop": true}
        ]})",
        "atlas");
}

TEST_F(SpriteAnimationSetTest, AtlasRoundTripOmitsEmpty)
{
    std::string error;
    const auto  missing = parseSpriteAnimationSetJson(kHeroDocument, error);
    ASSERT_NE(missing, nullptr) << error;
    EXPECT_TRUE(missing->atlas.empty());
    EXPECT_EQ(serializeSpriteAnimationSetJson(*missing).find("\"atlas\""), std::string::npos);

    missing->atlas = "Content/Textures/hero_walk.png";
    const std::string withAtlas = serializeSpriteAnimationSetJson(*missing);
    EXPECT_NE(withAtlas.find("\"atlas\": \"Content/Textures/hero_walk.png\""), std::string::npos);
    const auto again = parseSpriteAnimationSetJson(withAtlas, error);
    ASSERT_NE(again, nullptr) << error;
    EXPECT_EQ(again->atlas, "Content/Textures/hero_walk.png");

    again->atlas.clear();
    EXPECT_EQ(serializeSpriteAnimationSetJson(*again).find("\"atlas\""), std::string::npos);
    const auto emptyAtlas =
        parseSpriteAnimationSetJson(R"({"atlas": "", "columns": 1, "rows": 1, "clips": []})", error);
    ASSERT_NE(emptyAtlas, nullptr) << error;
    EXPECT_TRUE(emptyAtlas->atlas.empty());
}

TEST_F(SpriteAnimationSetTest, FpsAndLoopDefaultWhenOmitted)
{
    std::string error;
    const auto  parsed = parseSpriteAnimationSetJson(R"({
        "columns": 3,
        "rows": 4,
        "clips": [ {"name": "idle_down", "frames": [1]} ]
    })",
                                                    error);
    ASSERT_NE(parsed, nullptr) << error;
    ASSERT_EQ(parsed->clips.size(), 1u);
    EXPECT_FLOAT_EQ(parsed->clips[0].fps, 8.0f);
    EXPECT_TRUE(parsed->clips[0].bLoop);
}

TEST(SpriteAnimationSetFrameRect, MatchesSpriteAnimationComponent)
{
    auto* store = AssetTypeRegistry::get().store<SpriteAnimationSet>();
    ASSERT_NE(store, nullptr);
    const int32_t grids[][2] = {{3, 4}, {1, 1}, {4, 1}, {0, 4}, {2, 0}};
    for (const auto& grid : grids) {
        auto set     = std::make_shared<SpriteAnimationSet>();
        set->columns = grid[0];
        set->rows    = grid[1];
        const std::string name = "frame-rect-" + std::to_string(grid[0]) + "x" + std::to_string(grid[1]);
        store->registerAsset(name, set);

        SpriteAnimationComponent component;
        component.animation = SpriteAnimationSetRef(name);
        const int32_t last = grid[0] > 0 && grid[1] > 0 ? grid[0] * grid[1] : 0;
        for (int32_t frame = -1; frame <= last; ++frame) {
            const glm::vec4 fromSet       = set->frameRect(frame);
            const glm::vec4 fromComponent = component.frameRect(frame);
            EXPECT_FLOAT_EQ(fromSet.x, fromComponent.x) << frame;
            EXPECT_FLOAT_EQ(fromSet.y, fromComponent.y) << frame;
            EXPECT_FLOAT_EQ(fromSet.z, fromComponent.z) << frame;
            EXPECT_FLOAT_EQ(fromSet.w, fromComponent.w) << frame;
        }
        store->unload(name);
    }
}

TEST_F(SpriteAnimationSetTest, MissingFileIsSharedFailedSlot)
{
    constexpr const char* kMissing = "Content/Animations/__sprite_anim_missing.yaanim.json";

    SpriteAnimationSetRef first(kMissing);
    SpriteAnimationSetRef copy = first;
    SpriteAnimationSetRef second;
    second.setPath(kMissing);

    ASSERT_NE(first._handle, nullptr);
    EXPECT_EQ(first._handle, copy._handle);
    EXPECT_EQ(first._handle, second._handle);
    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Failed);
    EXPECT_FALSE(first.isLoaded());
    EXPECT_EQ(first.get(), nullptr);
    EXPECT_EQ(first._handle->generation, 1u);
    IDocumentAssetStore<SpriteAnimationSet>* sets = AssetTypeRegistry::get().store<SpriteAnimationSet>();
    ASSERT_NE(sets, nullptr);
    EXPECT_FALSE(sets->isLoaded(kMissing));
    EXPECT_EQ(sets->get(kMissing), nullptr);
}

TEST_F(SpriteAnimationSetTest, ContentMountSpellingsShareOneSlot)
{
    SpriteAnimationSetRef colon("Content:Animations/Hero.yaanim.json");
    SpriteAnimationSetRef slash("Content/Animations/Hero.yaanim.json");

    EXPECT_EQ(colon.getPath(), slash.getPath());
    EXPECT_EQ(colon.getPath(), "Content/Animations/Hero.yaanim.json");
    ASSERT_NE(colon._handle, nullptr);
    EXPECT_EQ(colon._handle, slash._handle);
    EXPECT_EQ(colon.getResolveState(), EAssetResolveState::Failed);
}

TEST_F(SpriteAnimationSetTest, ParsedDocumentIsSharedAcrossRefs)
{
    auto* vfs = VirtualFileSystem::get();
    ASSERT_NE(vfs, nullptr);

    const auto dir = std::filesystem::temp_directory_path() / "ya-sprite-animation-set-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    vfs->unmount("YaAnimTest");
    vfs->mount("YaAnimTest", dir);

    SpriteAnimationSet authored;
    authored.columns = 3;
    authored.rows    = 4;
    authored.clips.push_back(SpriteAnimationClip{
        .name   = "idle_down",
        .frames = {1},
        .fps    = 1.0f,
        .bLoop  = true,
    });
    vfs->saveToFile("YaAnimTest:hero.yaanim.json", serializeSpriteAnimationSetJson(authored));

    SpriteAnimationSetRef first("YaAnimTest:hero.yaanim.json");
    SpriteAnimationSetRef second("YaAnimTest:hero.yaanim.json");

    ASSERT_NE(first._handle, nullptr);
    EXPECT_EQ(first._handle, second._handle);
    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Ready);
    EXPECT_TRUE(first.isLoaded());
    ASSERT_NE(first.get(), nullptr);
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(first.get()->columns, 3);
    EXPECT_EQ(first.get()->rows, 4);
    ASSERT_EQ(first.get()->clips.size(), 1u);
    EXPECT_EQ(first.get()->clips[0].name, "idle_down");
    EXPECT_TRUE(AssetTypeRegistry::get().store<SpriteAnimationSet>()->isLoaded("YaAnimTest:hero.yaanim.json"));

    vfs->unmount("YaAnimTest");
    std::filesystem::remove_all(dir);
}

TEST_F(SpriteAnimationSetTest, EmptyPathBindsNothing)
{
    SpriteAnimationSetRef empty;
    EXPECT_EQ(empty.getResolveState(), EAssetResolveState::Empty);
    EXPECT_EQ(empty._handle, nullptr);
    EXPECT_FALSE(empty.isLoaded());
}

} // namespace
} // namespace ya
