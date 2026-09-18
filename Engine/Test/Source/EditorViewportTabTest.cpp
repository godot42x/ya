#include "GameEditor/UI/Tabs/EditorViewportTab.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/Texture.h"

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

/// Handle-only texture stand-in: these cases assert placement and hit
/// ownership, which the tab answers from its stored handle and layout rect.
std::shared_ptr<Texture> fakeTexture()
{
    return std::shared_ptr<Texture>(reinterpret_cast<Texture*>(static_cast<uintptr_t>(0x1)),
                                    [](Texture*) {});
}

/// Viewport tab filling a 400x300 tree, laid out.
struct FViewportTabFixture
{
    FRecordingViewportHostSink         sink;
    WidgetTree                         tree{{.width = 400, .height = 300}};
    std::shared_ptr<EditorViewportTab> tab;

    FViewportTabFixture()
    {
        auto root = std::make_shared<UICanvasPanel>("Root");
        FCanvasSlotArgs rootSlot;
        rootSlot.anchorMin = {0.0f, 0.0f};
        rootSlot.anchorMax = {1.0f, 1.0f};
        EXPECT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), root, rootSlot).valid());

        tab = std::make_shared<EditorViewportTab>(&sink);
        FCanvasSlotArgs tabSlot;
        tabSlot.anchorMin = {0.0f, 0.0f};
        tabSlot.anchorMax = {1.0f, 1.0f};
        EXPECT_TRUE(tree.attach(*root, tab, tabSlot).valid());
        tree.layout();
    }
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

TEST(EditorViewportTabTest, PreviewPanelTakesTheAreaItIsPushedToFromWorldInput)
{
    FViewportTabFixture fixture;
    const Rect2D        world  = fixture.tab->imageRect();
    const glm::vec2     origin = world.pos;
    ASSERT_GT(world.extent.x, 0.0f);
    ASSERT_GT(world.extent.y, 0.0f);

    // With no preview pushed, every point of the image belongs to the world.
    EXPECT_TRUE(fixture.tab->isWorldPoint(origin + glm::vec2(20.0f, 20.0f)));

    const Rect2D local{.pos = {200.0f, 160.0f}, .extent = {120.0f, 90.0f}};
    fixture.tab->setPreviewImage(fakeTexture(), local);
    fixture.tree.layout();

    // Chrome owns its own area: inside is not world, and just outside is.
    EXPECT_FALSE(fixture.tab->isWorldPoint(origin + local.pos + local.extent * 0.5f));
    EXPECT_FALSE(fixture.tab->isWorldPoint(origin + local.pos + glm::vec2(2.0f, 2.0f)));
    EXPECT_TRUE(fixture.tab->isWorldPoint(origin + local.pos - glm::vec2(4.0f, 4.0f)));
    EXPECT_TRUE(fixture.tab->isWorldPoint(origin + glm::vec2(20.0f, 20.0f)));
    // Stacking chrome on the image must not resize it.
    EXPECT_FLOAT_EQ(fixture.tab->imageRect().extent.x, world.extent.x);
    EXPECT_FLOAT_EQ(fixture.tab->imageRect().extent.y, world.extent.y);

    // A second rect moves the panel: the old area goes back to the world.
    const Rect2D moved{.pos = {40.0f, 30.0f}, .extent = {100.0f, 80.0f}};
    fixture.tab->setPreviewImage(fakeTexture(), moved);
    fixture.tree.layout();
    EXPECT_FALSE(fixture.tab->isWorldPoint(origin + moved.pos + moved.extent * 0.5f));
    EXPECT_TRUE(fixture.tab->isWorldPoint(origin + local.pos + local.extent * 0.5f));
}

TEST(EditorViewportTabTest, PreviewPanelCollapsesWithoutAnImage)
{
    FViewportTabFixture fixture;
    const Rect2D        world = fixture.tab->imageRect();
    const glm::vec2     inside = world.pos + glm::vec2(240.0f, 200.0f);

    fixture.tab->setPreviewImage(fakeTexture(),
                                 Rect2D{.pos = {200.0f, 160.0f}, .extent = {120.0f, 90.0f}});
    fixture.tree.layout();
    EXPECT_FALSE(fixture.tab->isWorldPoint(inside));

    // No image this tick (2D mode, no camera selected, a frame the preview View
    // did not record): the panel is gone and the world gets the whole image.
    fixture.tab->setPreviewImage(nullptr, {});
    fixture.tree.layout();
    EXPECT_TRUE(fixture.tab->isWorldPoint(inside));
    EXPECT_TRUE(fixture.tab->isWorldPoint(world.pos + glm::vec2(20.0f, 20.0f)));
}

} // namespace ya
