#include "GameEditor/UI/EditorListRows.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Text.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorListRowsTest, ContentRowUpdaterRefreshesSelectionWithoutRebuildingInstance)
{
    auto row = contentRow("Entry", "Alpha/", "Alpha", {}, {}).share();
    ASSERT_FALSE(row->_bSelected);

    bool selected = false;
    updateContentRow(*row, "Alpha/", "Alpha", true, [&](const std::string& id) {
        EXPECT_EQ(id, "Alpha");
        selected = true;
    }, {}, true);
    EXPECT_TRUE(row->_bSelected);

    selected = false;
    if (row->_onSelect) {
        row->_onSelect("Alpha");
    }
    EXPECT_TRUE(selected);

    UIText* label = contentRowLabel(*row);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->getText(), "Alpha/");

    UIImage* icon = contentRowIcon(*row);
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(icon->_assetPath, editor_icons::kFolder);

    updateContentRow(*row, "file.txt", "file.txt", false, {}, {}, false);
    EXPECT_EQ(contentRowLabel(*row)->getText(), "file.txt");
    EXPECT_EQ(contentRowIcon(*row)->_assetPath, editor_icons::kFile);
}

TEST(EditorListRowsTest, ContentTileUpdaterSetsPreviewPathAndSelection)
{
    auto tile = contentTile("Tile", "ao.jpg", "ao.jpg", {}, {}).share();
    ASSERT_FALSE(tile->_bSelected);

    updateContentTile(*tile,
                      "ao.jpg",
                      "ao.jpg",
                      true,
                      {},
                      {},
                      editor_icons::kFile,
                      64.0f);
    EXPECT_TRUE(tile->_bSelected);
    ASSERT_NE(contentRowLabel(*tile), nullptr);
    EXPECT_EQ(contentRowLabel(*tile)->getText(), "ao.jpg");
    ASSERT_NE(contentRowIcon(*tile), nullptr);
    EXPECT_EQ(contentRowIcon(*tile)->_assetPath, editor_icons::kFile);
    EXPECT_EQ(contentRowIcon(*tile)->getScaleMode(), EImageScaleMode::Contain);
    EXPECT_TRUE(contentRowLabel(*tile)->_bWrap);
    EXPECT_FLOAT_EQ(contentRowLabel(*tile)->_maxWrapWidth, 64.0f);

    UIImage* icon = contentRowIcon(*tile);
    ASSERT_NE(icon, nullptr);
    icon->clearPaintDirty();
    updateContentTile(*tile, "ao.jpg", "ao.jpg", true, {}, {}, editor_icons::kFile, 64.0f);
    EXPECT_FALSE(icon->isPaintDirty());
    EXPECT_EQ(icon->_assetPath, editor_icons::kFile);

    updateContentTile(*tile, "ao.jpg", "ao.jpg", true, {}, {}, "Engine:Content/face.png", 64.0f);
    EXPECT_TRUE(icon->isPaintDirty());
    EXPECT_EQ(icon->_assetPath, "Engine:Content/face.png");
}

TEST(EditorListRowsTest, IconLabeledButtonHostsImageAndLabel)
{
    auto button = iconLabeledButton("Play", "Play", editor_icons::kPlay).share();
    ASSERT_FALSE(button->getChildren().empty());
    UIElement* content = button->getChildren().front().get();
    ASSERT_NE(content, nullptr);
    UIImage* icon = nullptr;
    UIText*  label = nullptr;
    for (const auto& child : content->getChildren()) {
        if (auto* image = dynamic_cast<UIImage*>(child.get())) {
            icon = image;
        }
        else if (auto* text = dynamic_cast<UIText*>(child.get())) {
            label = text;
        }
    }
    ASSERT_NE(icon, nullptr);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(icon->_assetPath, editor_icons::kPlay);
    EXPECT_EQ(label->getText(), "Play");
}

} // namespace ya
