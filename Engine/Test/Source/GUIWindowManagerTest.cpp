#include "App/Kernel/AppKernel.h"
#include "Core/KeyCode.h"
#include "GUI/Host/GUIApp.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/DragDropOperation.h"
#include "GUI/Widgets/UIElement.h"

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
    manager.renderAll();
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

TEST(GUIWindowManagerTest, IsolatesPointerFocusCaptureTooltipClipboardDpiAndSnapshot)
{
    NamedWindowDelegate a;
    a.name = "A";
    NamedWindowDelegate b;
    b.name = "B";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    const GUIWindowId idA = manager.create(extraConfig("MW-102-A", 160, 120), a);
    const GUIWindowId idB = manager.create(extraConfig("MW-102-B", 200, 150), b);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    WidgetTree* treeA = manager.findTree(idA);
    WidgetTree* treeB = manager.findTree(idB);
    ASSERT_NE(treeA, nullptr);
    ASSERT_NE(treeB, nullptr);
    a.button->setTooltip("tip-a");
    b.button->setTooltip("tip-b");
    manager.tickAll(0.0f);

    treeA->setClipboardText("clip-a");
    treeB->setClipboardText("clip-b");
    EXPECT_EQ(treeA->getClipboardText(), "clip-a");
    EXPECT_EQ(treeB->getClipboardText(), "clip-b");

    treeA->setDpiScale(1.25f);
    treeB->setDpiScale(2.0f);
    EXPECT_FLOAT_EQ(treeA->getDpiScale(), 1.25f);
    EXPECT_FLOAT_EQ(treeB->getDpiScale(), 2.0f);

    MouseMoveEvent moveA(20.0f, 16.0f);
    moveA._windowID = idA;
    ASSERT_TRUE(manager.dispatchEvent(moveA));
    MouseButtonPressedEvent pressA(EMouse::Left);
    pressA._windowID = idA;
    ASSERT_TRUE(manager.dispatchEvent(pressA));
    EXPECT_EQ(treeA->getHovered(), a.button.get());
    EXPECT_EQ(treeA->getFocused(), a.button.get());
    EXPECT_EQ(treeA->getPointerCapture(), a.button.get());
    EXPECT_EQ(treeB->getHovered(), nullptr);
    EXPECT_EQ(treeB->getFocused(), nullptr);
    EXPECT_EQ(treeB->getPointerCapture(), nullptr);

    MouseMoveEvent moveB(20.0f, 16.0f);
    moveB._windowID = idB;
    ASSERT_TRUE(manager.dispatchEvent(moveB));
    MouseButtonPressedEvent pressB(EMouse::Left);
    pressB._windowID = idB;
    ASSERT_TRUE(manager.dispatchEvent(pressB));
    EXPECT_EQ(treeA->getPointerCapture(), a.button.get());
    EXPECT_EQ(treeA->getFocused(), a.button.get());
    EXPECT_EQ(treeA->getHovered(), a.button.get());
    EXPECT_EQ(treeB->getHovered(), b.button.get());
    EXPECT_EQ(treeB->getFocused(), b.button.get());
    EXPECT_EQ(treeB->getPointerCapture(), b.button.get());

    for (int i = 0; i < 40; ++i) {
        manager.tickAll(0.0f);
    }
    ASSERT_NE(treeA->getTooltipHost(), nullptr);
    ASSERT_NE(treeB->getTooltipHost(), nullptr);
    EXPECT_NE(treeA->getTooltipHost(), treeB->getTooltipHost());

    const UIFrameSnapshot* snapA = manager.findSnapshot(idA);
    const UIFrameSnapshot* snapB = manager.findSnapshot(idB);
    ASSERT_NE(snapA, nullptr);
    ASSERT_NE(snapB, nullptr);
    EXPECT_NE(snapA, snapB);
    EXPECT_FALSE(snapA->items.empty());
    EXPECT_FALSE(snapB->items.empty());
    EXPECT_EQ(snapA->logicalExtent.width, 160u);
    EXPECT_EQ(snapB->logicalExtent.width, 200u);

    ASSERT_TRUE(manager.dispatchEvent(WindowFocusLostEvent(idA)));
    EXPECT_EQ(treeA->getHovered(), nullptr);
    EXPECT_EQ(treeA->getTooltipHost(), nullptr);
    EXPECT_EQ(treeA->getFocused(), a.button.get());
    EXPECT_EQ(treeA->getPointerCapture(), a.button.get());
    EXPECT_EQ(treeB->getHovered(), b.button.get());
    EXPECT_EQ(manager.focusedWindowId(), idB);

    KeyPressedEvent leftoverKey;
    leftoverKey._keyCode = EKey::Space;
    EXPECT_TRUE(manager.dispatchEvent(leftoverKey));
    EXPECT_EQ(manager.focusedWindowId(), idB);
}

TEST(GUIWindowManagerTest, MinimizedExtraStillTicksAndSnapshots)
{
    NamedWindowDelegate a;
    a.name = "A";
    NamedWindowDelegate b;
    b.name = "B";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    const GUIWindowId idA = manager.create(extraConfig("MW-206-A", 160, 120), a);
    const GUIWindowId idB = manager.create(extraConfig("MW-206-B", 200, 150), b);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    manager.tickAll(0.0f);
    const int updatesA = a.updates;
    const int updatesB = b.updates;
    ASSERT_GT(updatesA, 0);
    ASSERT_GT(updatesB, 0);

    ASSERT_TRUE(manager.dispatchEvent(WindowMinimizeEvent(idB)));
    manager.tickAll(0.0f);
    EXPECT_GT(a.updates, updatesA);
    EXPECT_GT(b.updates, updatesB);

    const UIFrameSnapshot* snapB = manager.findSnapshot(idB);
    ASSERT_NE(snapB, nullptr);
    EXPECT_FALSE(snapB->items.empty());

    ASSERT_TRUE(manager.dispatchEvent(WindowRestoreEvent(idB)));
    manager.tickAll(0.0f);
    EXPECT_GT(b.updates, updatesB + 1);
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

struct CrossWindowDropTarget final : public UIElement
{
    explicit CrossWindowDropTarget(std::string name) : UIElement(std::move(name)) {}

    int         drops = 0;
    std::string lastPayload;

    bool canAcceptDrop(const UIDragDropOperation&, const glm::vec2& point) override
    {
        return hitTestLayoutRect(point);
    }

    void onDrop(const UIDragDropOperation& operation, const glm::vec2&) override
    {
        ++drops;
        if (const auto* text = operation.as<UIStringDragDropOperation>()) {
            lastPayload = text->text;
        }
    }
};

struct CrossWindowSourceDelegate final : IGUIAppDelegate
{
    std::shared_ptr<UIButton> source;

    void buildUI(WidgetTree& tree) override
    {
        source = std::make_shared<UIButton>("drag-source");
        FCanvasSlotArgs slot;
        slot.offset    = {4.0f, 4.0f};
        slot.fixedSize = {80.0f, 24.0f};
        tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, slot);
    }
};

struct CrossWindowTargetDelegate final : IGUIAppDelegate
{
    std::shared_ptr<CrossWindowDropTarget> target;

    void buildUI(WidgetTree& tree) override
    {
        target = std::make_shared<CrossWindowDropTarget>("drop-target");
        FCanvasSlotArgs slot;
        slot.offset    = {4.0f, 4.0f};
        slot.fixedSize = {140.0f, 100.0f};
        tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, slot);
    }
};

TEST(GUIAppCrossWindowDragTest, DropsFromSourceWindowOntoTargetWindow)
{
    NamedWindowDelegate primary;
    CrossWindowSourceDelegate sourceDel;
    CrossWindowTargetDelegate targetDel;

    GUIApp app(extraConfig("MW-301-primary", 64, 64), primary);
    const GUIWindowId idA = app.openWindow(extraConfig("MW-301-A", 160, 120), sourceDel);
    const GUIWindowId idB = app.openWindow(extraConfig("MW-301-B", 160, 120), targetDel);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    app.onTick(0.0f);
    WidgetTree* treeA = app.findTree(idA);
    WidgetTree* treeB = app.findTree(idB);
    ASSERT_NE(treeA, nullptr);
    ASSERT_NE(treeB, nullptr);
    treeA->layout();
    treeB->layout();

    treeA->beginDrag(sourceDel.source.get(), "cross-payload", "Ghost", {}, false);
    MouseMoveEvent adopt(20.0f, 16.0f);
    adopt._windowID = idA;
    app.onEvent(adopt);
    ASSERT_TRUE(app.isCrossWindowDragActive());
    EXPECT_EQ(app.crossWindowDragSourceId(), idA);
    EXPECT_TRUE(treeA->isDragging());
    EXPECT_FALSE(treeB->isDragging());

    MouseMoveEvent hover(40.0f, 40.0f);
    hover._windowID = idB;
    app.onEvent(hover);
    EXPECT_EQ(app.crossWindowDragHoverId(), idB);
    EXPECT_GT(app.crossWindowDragEnterCount(), 0u);
    EXPECT_EQ(treeB->getDropTarget(), targetDel.target.get());
    EXPECT_FALSE(treeB->isDragging());
    EXPECT_TRUE(treeA->isDragging());

    MouseButtonReleasedEvent release(EMouse::Left);
    release._windowID = idB;
    app.onEvent(release);
    EXPECT_FALSE(app.isCrossWindowDragActive());
    EXPECT_FALSE(treeA->isDragging());
    EXPECT_EQ(targetDel.target->drops, 1);
    EXPECT_EQ(targetDel.target->lastPayload, "cross-payload");
}

TEST(GUIAppCrossWindowDragTest, LeaveKeepsSourceSessionAlive)
{
    NamedWindowDelegate primary;
    CrossWindowSourceDelegate sourceDel;
    CrossWindowTargetDelegate targetDel;

    GUIApp app(extraConfig("MW-301-leave-primary", 64, 64), primary);
    const GUIWindowId idA = app.openWindow(extraConfig("MW-301-leave-A", 160, 120), sourceDel);
    const GUIWindowId idB = app.openWindow(extraConfig("MW-301-leave-B", 160, 120), targetDel);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    app.onTick(0.0f);
    WidgetTree* treeA = app.findTree(idA);
    ASSERT_NE(treeA, nullptr);
    treeA->layout();
    treeA->beginDrag(sourceDel.source.get(), "keep", "Keep", {}, false);
    MouseMoveEvent adopt(20.0f, 16.0f);
    adopt._windowID = idA;
    app.onEvent(adopt);
    ASSERT_TRUE(app.isCrossWindowDragActive());

    app.onEvent(WindowMouseLeaveEvent(idA));
    EXPECT_TRUE(treeA->isDragging());
    EXPECT_TRUE(app.isCrossWindowDragActive());
    EXPECT_GT(app.crossWindowDragLeaveCount(), 0u);
    EXPECT_EQ(app.crossWindowDragHoverId(), 0u);

    MouseMoveEvent hover(40.0f, 40.0f);
    hover._windowID = idB;
    app.onEvent(hover);
    EXPECT_EQ(app.crossWindowDragHoverId(), idB);
    EXPECT_GT(app.crossWindowDragEnterCount(), 0u);
    EXPECT_TRUE(treeA->isDragging());
}

TEST(GUIAppCrossWindowDragTest, DefersSourceCloseUntilDragEnds)
{
    NamedWindowDelegate primary;
    CrossWindowSourceDelegate sourceDel;
    CrossWindowTargetDelegate targetDel;

    GUIApp app(extraConfig("MW-301-close-primary", 64, 64), primary);
    const GUIWindowId idA = app.openWindow(extraConfig("MW-301-close-A", 160, 120), sourceDel);
    const GUIWindowId idB = app.openWindow(extraConfig("MW-301-close-B", 160, 120), targetDel);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    app.onTick(0.0f);
    WidgetTree* treeA = app.findTree(idA);
    ASSERT_NE(treeA, nullptr);
    treeA->layout();
    treeA->beginDrag(sourceDel.source.get(), "defer", "Defer", {}, false);
    MouseMoveEvent adopt(20.0f, 16.0f);
    adopt._windowID = idA;
    app.onEvent(adopt);
    ASSERT_TRUE(app.isCrossWindowDragActive());

    int after = 0;
    app.runAfterDrag([&] { ++after; });
    EXPECT_EQ(after, 0);

    app.closeWindow(idA);
    app.onTick(0.0f);
    EXPECT_NE(app.findTree(idA), nullptr);
    EXPECT_TRUE(treeA->isDragging());

    KeyPressedEvent escape;
    escape._keyCode = EKey::Escape;
    app.onEvent(escape);
    EXPECT_EQ(after, 1);
    EXPECT_FALSE(app.isCrossWindowDragActive());

    app.onTick(0.0f);
    EXPECT_EQ(app.findTree(idA), nullptr);
    EXPECT_NE(app.findTree(idB), nullptr);
}

} // namespace ya
