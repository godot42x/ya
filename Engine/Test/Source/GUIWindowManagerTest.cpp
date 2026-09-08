#include "App/Kernel/AppKernel.h"
#include "Core/KeyCode.h"
#include "GUI/Host/GUIApp.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GUI/Widgets/Controls/Button.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

struct NamedWindowDelegate final : IGUIAppDelegate
{
    const char*              name = "Window";
    int                      updates = 0;
    std::shared_ptr<UIButton> button;

    void buildUI(WidgetTree& tree) override
    {
        button = std::make_shared<UIButton>(name);
        FCanvasSlotArgs slot;
        slot.offset    = {4.0f, 4.0f};
        slot.fixedSize = {80.0f, 24.0f};
        tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, slot);
    }

    void updateUI() override { ++updates; }
};

FGUIWindowHostConfig extraConfig(const char* title, uint32_t width, uint32_t height)
{
    FGUIWindowHostConfig config;
    config.title  = title;
    config.width  = width;
    config.height = height;
    return config;
}

} // namespace

TEST(GUIWindowManagerTest, CreatesIsolatedTreesAndDoesNotQuitSiblingOnClose)
{
    NamedWindowDelegate a;
    a.name = "A";
    NamedWindowDelegate b;
    b.name = "B";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    const GUIWindowId idA = manager.create(extraConfig("MW-101-A", 160, 120), a);
    const GUIWindowId idB = manager.create(extraConfig("MW-101-B", 200, 150), b);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    EXPECT_NE(idA, idB);
    EXPECT_EQ(manager.extraWindowCount(), 2u);
    WidgetTree* treeA = manager.findTree(idA);
    WidgetTree* treeB = manager.findTree(idB);
    ASSERT_NE(treeA, nullptr);
    ASSERT_NE(treeB, nullptr);
    EXPECT_NE(treeA, treeB);

    manager.tickAll(0.0f);
    EXPECT_GT(a.updates, 0);
    EXPECT_GT(b.updates, 0);
    EXPECT_EQ(treeA->getLogicalExtent().width, 160u);
    EXPECT_EQ(treeB->getLogicalExtent().width, 200u);

    ASSERT_TRUE(manager.dispatchEvent(WindowResizeEvent(idA, 240, 180)));
    EXPECT_EQ(treeA->getLogicalExtent().width, 240u);
    EXPECT_EQ(treeA->getLogicalExtent().height, 180u);
    EXPECT_EQ(treeB->getLogicalExtent().width, 200u);

    MouseMoveEvent moveA(20.0f, 16.0f);
    moveA._windowID = idA;
    ASSERT_TRUE(manager.dispatchEvent(moveA));
    EXPECT_EQ(treeA->getHovered(), a.button.get());
    EXPECT_EQ(treeB->getHovered(), nullptr);

    MouseMoveEvent moveB(20.0f, 16.0f);
    moveB._windowID = idB;
    ASSERT_TRUE(manager.dispatchEvent(moveB));
    EXPECT_EQ(treeA->getHovered(), a.button.get());
    EXPECT_EQ(treeB->getHovered(), b.button.get());

    ASSERT_TRUE(manager.dispatchEvent(WindowFocusEvent(idA)));
    EXPECT_EQ(manager.focusedWindowId(), idA);

    manager.requestClose(idA);
    manager.tickAll(0.0f);
    EXPECT_EQ(manager.findTree(idA), nullptr);
    ASSERT_NE(manager.findTree(idB), nullptr);
    EXPECT_EQ(manager.extraWindowCount(), 1u);
}

TEST(GUIAppExtraWindowTest, RoutesExtraEventsWithoutCopyingPrimaryLoop)
{
    NamedWindowDelegate primary;
    primary.name = "Primary";
    NamedWindowDelegate extra;
    extra.name = "Extra";

    GUIApp app(extraConfig("MW-101-primary", 64, 64), primary);
    const GUIWindowId extraId = app.openWindow(extraConfig("MW-101-extra", 160, 120), extra);
    if (extraId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    WidgetTree* extraTree = app.findTree(extraId);
    ASSERT_NE(extraTree, nullptr);
    EXPECT_FALSE(app.getPrimaryWindow().isInitialized());

    app.onEvent(WindowResizeEvent(extraId, 320, 200));
    EXPECT_EQ(extraTree->getLogicalExtent().width, 320u);
    EXPECT_EQ(extraTree->getLogicalExtent().height, 200u);

    app.onTick(0.0f);
    struct ExtraPointerSource final : IAppEventSource
    {
        GUIWindowId id = 0;
        void pollEvents(const std::function<void(const Event&)>& emit) override
        {
            MouseMoveEvent move(20.0f, 16.0f);
            move._windowID = id;
            emit(move);
        }
    } source;
    source.id = extraId;

    AppKernel kernel({.eventSource = &source}, app);
    EXPECT_EQ(kernel.run(AppAutomationRunOptions{.exitAfterFrame = 2}), 0);
    EXPECT_EQ(extraTree->getHovered(), extra.button.get());
    EXPECT_GT(extra.updates, 0);
    EXPECT_FALSE(app.getPrimaryWindow().isInitialized());
    EXPECT_FALSE(app.shouldClose());

    KeyPressedEvent escape;
    escape._keyCode  = EKey::Escape;
    escape._windowID = extraId;
    app.onEvent(escape);
    app.onTick(0.0f);
    EXPECT_EQ(app.findTree(extraId), nullptr);
    EXPECT_FALSE(app.shouldClose());
}

TEST(GUIAppExtraWindowTest, ExtraCloseDoesNotQuitPrimary)
{
    NamedWindowDelegate primary;
    NamedWindowDelegate extra;

    GUIApp app(extraConfig("MW-101-primary", 64, 64), primary);
    const GUIWindowId extraId = app.openWindow(extraConfig("MW-101-extra", 96, 72), extra);
    if (extraId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    app.onEvent(WindowCloseEvent(extraId));
    app.onTick(0.0f);
    EXPECT_EQ(app.findTree(extraId), nullptr);
    EXPECT_FALSE(app.shouldClose());

    app.onEvent(WindowCloseEvent(0));
    EXPECT_TRUE(app.shouldClose());
}

} // namespace ya
