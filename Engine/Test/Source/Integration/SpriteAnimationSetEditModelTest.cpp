#include "GameEditor/Animation/SpriteAnimationSetEditModel.h"
#include "GameEditor/UI/Tabs/EditorAnimationSetTab.h"

#include "Core/Common/AssetTypeRegistry.h"
#include "Core/Common/SpriteAnimationSet.h"
#include "Core/Event.h"
#include "Core/System/VirtualFileSystem.h"
#include "GUI/Widgets/UIElement.h"
#include "Resource/AssetManager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace ya
{
namespace
{

class SpriteAnimationSetEditModelTest : public ::testing::Test
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
        if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
            vfs->unmount("YaAnimEdit");
        }
        AssetManager::get()->clearCache();
        std::filesystem::remove_all(std::filesystem::temp_directory_path() / "ya-sprite-anim-edit-test");
    }
};

const char* kDocument = R"({
    "columns": 3,
    "rows": 4,
    "clips": [ {"name": "idle_down", "frames": [1], "fps": 1.0, "bLoop": true} ]
})";

TEST_F(SpriteAnimationSetEditModelTest, MutationsValidateAndRevert)
{
    SpriteAnimationSetEditModel model;
    ASSERT_TRUE(model.loadFromText(kDocument, "mem"));
    EXPECT_FALSE(model.dirty());
    EXPECT_TRUE(model.error().empty());

    EXPECT_FALSE(model.renameClip(0, ""));
    EXPECT_NE(model.error().find("non-empty"), std::string::npos);
    EXPECT_FALSE(model.dirty());
    EXPECT_EQ(model.document().clips[0].name, "idle_down");

    EXPECT_FALSE(model.setFps(0, 0.0f));
    EXPECT_NE(model.error().find("fps"), std::string::npos);
    EXPECT_FALSE(model.dirty());

    EXPECT_FALSE(model.setColumns(0));
    EXPECT_FALSE(model.dirty());

    ASSERT_TRUE(model.addClip());
    EXPECT_TRUE(model.dirty());
    EXPECT_EQ(model.selectedClip(), 1);
    EXPECT_EQ(model.document().clips[1].name, "clip");
    EXPECT_FALSE(model.renameClip(1, "idle_down"));
    EXPECT_NE(model.error().find("duplicate"), std::string::npos);

    ASSERT_TRUE(model.renameClip(1, "walk"));
    ASSERT_TRUE(model.appendFrame(1, 2));
    ASSERT_TRUE(model.moveFrame(1, 1, -1));
    EXPECT_EQ(model.document().clips[1].frames, (std::vector<int32_t>{2, 0}));
    ASSERT_TRUE(model.removeFrame(1, 0));
    EXPECT_EQ(model.document().clips[1].frames, (std::vector<int32_t>{0}));
    ASSERT_TRUE(model.setLoop(1, false));
    ASSERT_TRUE(model.setFps(1, 12.0f));
    EXPECT_TRUE(model.validate());

    int32_t column = -1;
    int32_t row    = -1;
    EXPECT_TRUE(SpriteAnimationSetEditModel::frameToCell(7, 3, 4, column, row));
    EXPECT_EQ(column, 1);
    EXPECT_EQ(row, 2);
    EXPECT_FALSE(SpriteAnimationSetEditModel::frameToCell(12, 3, 4, column, row));

    model.revert();
    EXPECT_FALSE(model.dirty());
    ASSERT_EQ(model.document().clips.size(), 1u);
    EXPECT_EQ(model.document().clips[0].name, "idle_down");
}

// Empty atlas stays legal: playback then uses each sprite's own texture.
TEST_F(SpriteAnimationSetEditModelTest, AtlasEmptyIsOmittedAndRoundTrips)
{
    SpriteAnimationSetEditModel model;
    ASSERT_TRUE(model.loadFromText(kDocument, "mem"));
    EXPECT_TRUE(model.document().atlas.empty());
    EXPECT_EQ(model.serialize().find("\"atlas\""), std::string::npos);

    ASSERT_TRUE(model.setAtlas("Content/Textures/hero_walk.png"));
    EXPECT_TRUE(model.dirty());
    const std::string text = model.serialize();
    EXPECT_NE(text.find("\"atlas\": \"Content/Textures/hero_walk.png\""), std::string::npos);

    SpriteAnimationSetEditModel again;
    std::string                 error;
    ASSERT_TRUE(again.loadFromText(text, "mem"));
    EXPECT_EQ(again.document().atlas, "Content/Textures/hero_walk.png");
    ASSERT_TRUE(again.setAtlas(""));
    EXPECT_EQ(again.serialize().find("\"atlas\""), std::string::npos);
}

TEST_F(SpriteAnimationSetEditModelTest, SaveRejectsInvalidDocument)
{
    auto* vfs = VirtualFileSystem::get();
    ASSERT_NE(vfs, nullptr);
    const auto dir = std::filesystem::temp_directory_path() / "ya-sprite-anim-edit-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    vfs->unmount("YaAnimEdit");
    vfs->mount("YaAnimEdit", dir);

    SpriteAnimationSet authored;
    authored.columns = 3;
    authored.rows    = 4;
    authored.clips.push_back(SpriteAnimationClip{
        .name   = "idle_down",
        .frames = {1},
        .fps    = 1.0f,
        .bLoop  = true,
    });
    constexpr const char* kKey = "YaAnimEdit:hero.yaanim.json";
    vfs->saveToFile(kKey, serializeSpriteAnimationSetJson(authored));

    SpriteAnimationSetRef           ref(kKey);
    SpriteAnimationSetEditModel     model;
    ASSERT_TRUE(model.loadFromPath(kKey));
    EXPECT_EQ(model.assetPath(), ref.getPath());
    const uint64_t generation = ref._handle->generation;
    const std::string before  = serializeSpriteAnimationSetJson(*ref.get());

    ASSERT_TRUE(model.removeFrame(0, 0));
    EXPECT_TRUE(model.dirty());
    EXPECT_FALSE(model.validate());
    EXPECT_FALSE(model.save());
    EXPECT_NE(model.error().find("frames"), std::string::npos);
    EXPECT_EQ(ref._handle->generation, generation);
    std::string onDisk;
    ASSERT_TRUE(vfs->readFileToString(kKey, onDisk));
    EXPECT_EQ(onDisk, before);
}

TEST_F(SpriteAnimationSetEditModelTest, SaveReplacesLoadedSlot)
{
    auto* vfs = VirtualFileSystem::get();
    ASSERT_NE(vfs, nullptr);
    const auto dir = std::filesystem::temp_directory_path() / "ya-sprite-anim-edit-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "Animations");
    vfs->unmount("YaAnimEdit");
    vfs->mount("YaAnimEdit", dir);

    SpriteAnimationSet authored;
    authored.columns = 3;
    authored.rows    = 4;
    authored.clips.push_back(SpriteAnimationClip{
        .name   = "idle_down",
        .frames = {1},
        .fps    = 1.0f,
        .bLoop  = true,
    });
    const auto        file = dir / "Animations" / "hero.yaanim.json";
    const std::string key  = "YaAnimEdit:Animations/hero.yaanim.json";
    vfs->saveToFile(key, serializeSpriteAnimationSetJson(authored));

    const std::string canonical = SpriteAnimationSetEditModel::canonicalAssetPath(file.string());
    EXPECT_EQ(canonical, key);

    SpriteAnimationSetRef ref(key);
    ASSERT_NE(ref.get(), nullptr);
    const uint64_t generation = ref._handle->generation;
    EXPECT_EQ(ref.get()->clips[0].frames, (std::vector<int32_t>{1}));
    EXPECT_TRUE(ref.get()->atlas.empty());

    SpriteAnimationSetEditModel model;
    ASSERT_TRUE(model.loadFromPath(file.string()));
    EXPECT_EQ(model.assetPath(), ref.getPath());
    ASSERT_TRUE(model.setAtlas("Content/Textures/hero_walk.png"));
    ASSERT_TRUE(model.appendFrame(0, 2));
    ASSERT_TRUE(model.save());
    EXPECT_FALSE(model.dirty());

    EXPECT_GT(ref._handle->generation, generation);
    ASSERT_NE(ref.get(), nullptr);
    EXPECT_EQ(ref.get()->atlas, "Content/Textures/hero_walk.png");
    EXPECT_EQ(ref.get()->clips[0].frames, (std::vector<int32_t>{1, 2}));
    EXPECT_EQ(ref.get(), AssetTypeRegistry::get().store<SpriteAnimationSet>()->get(key).get());
}

TEST_F(SpriteAnimationSetEditModelTest, GridClickAppendsFrameAndMarksDirty)
{
    SpriteAnimationSetEditModel model;
    ASSERT_TRUE(model.loadFromText(kDocument, "mem"));
    model.selectClip(0);

    UISpriteAnimAtlasGrid grid;
    grid.setGrid(3, 4);
    grid.layoutAssigned(Rect2D{.pos = {0.0f, 0.0f}, .extent = {300.0f, 400.0f}});
    grid.setOnCell([&model](int32_t frame) { EXPECT_TRUE(model.appendFrame(model.selectedClip(), frame)); });

    MouseButtonPressedEvent press(EMouse::Left);
    WidgetEventContext      ctx;
    ctx.logicalPoint = {150.0f, 250.0f};
    EXPECT_TRUE(grid.handleInputEvent(press, ctx));
    EXPECT_TRUE(model.dirty());
    ASSERT_EQ(model.document().clips[0].frames.size(), 2u);
    EXPECT_EQ(model.document().clips[0].frames[1], 7);
}

} // namespace
} // namespace ya
