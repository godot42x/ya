#include "GameEditor/UI/EditorViewportTab.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

struct FRecordingViewportHostSink final : IEditorViewportHostSink
{
    IEditorViewportHost* host = nullptr;
    void setViewportHost(IEditorViewportHost* next) override { host = next; }
};

} // namespace

TEST(EditorViewportTabTest, AttachRegistersHostAndDetachClears)
{
    FRecordingViewportHostSink sink;
    WidgetTree tree({.width = 400, .height = 300});
    auto root = std::make_shared<UICanvasPanel>("Root");
    FCanvasSlotArgs rootSlot;
    rootSlot.anchorMin = {0.0f, 0.0f};
    rootSlot.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), root, rootSlot).valid());

    auto tab = std::make_shared<EditorViewportTab>(&sink);
    ASSERT_TRUE(tree.attach(*root, tab).valid());
    ASSERT_EQ(sink.host, static_cast<IEditorViewportHost*>(tab.get()));

    tab->setDisplayImage(nullptr, true);
    EXPECT_FALSE(tab->isHovered());
    EXPECT_FALSE(tab->isFocused());

    tree.detach(*tab);
    EXPECT_EQ(sink.host, nullptr);
}

} // namespace ya
