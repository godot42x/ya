#include "App/Kernel/AppKernel.h"
#include "Core/KeyCode.h"
#include "GUI/Host/GUIAppHost.h"
#include "GUI/Host/GUIDockNativePlacement.h"
#include "GUI/Host/GUIDragRouter.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GUI/Host/GUIWindowSession.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/DragDrop.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/DockSpace/DockTabStack.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/DragDropOperation.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/Event.h"
#include "Core/Os/OsEvent.h"
#include "RHI/NativeWindow.h"

#include <gtest/gtest.h>
#include <memory>

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

TEST(GUIWindowManagerTest, IsolatesPointerFocusCaptureTooltipDpiAndSnapshot)
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
    // Focus survives key-focus loss; the pointer session does not, because the
    // press was synthetic. The manager reconciles the cached button mask with
    // the platform's authoritative state on focus loss, and the platform here
    // reports no button held, so the stale session is cancelled instead of
    // poisoning the next click. A real cross-window drag is kept alive by the
    // drag path (see FocusLostClearsHoverDuringDragWithoutInjectingFarPointer).
    EXPECT_EQ(treeA->getPointerCapture(), nullptr);
    EXPECT_EQ(treeB->getHovered(), b.button.get());
    EXPECT_EQ(manager.focusedWindowId(), idB);

    KeyPressedEvent leftoverKey;
    leftoverKey._keyCode = EKey::Space;
    EXPECT_TRUE(manager.dispatchEvent(leftoverKey));
    EXPECT_EQ(manager.focusedWindowId(), idB);

    manager.setFocusedWindow(0);
    EXPECT_EQ(manager.focusedWindowId(), 0u);
    EXPECT_FALSE(manager.dispatchEvent(leftoverKey));
}

/// The clipboard is deliberately NOT tree-local: a session's tree is bound to
/// the OS clipboard because a real window is a real clipboard client, so the
/// last write is what every window reads back. The tree-local fallback in
/// WidgetTree exists for hosts with no clipboard hook (windowless trees), and
/// that half is asserted here too.
TEST(GUIWindowManagerTest, ClipboardIsProcessGlobalNotPerTree)
{
    NamedWindowDelegate a;
    a.name = "A";
    NamedWindowDelegate b;
    b.name = "B";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    const GUIWindowId idA = manager.create(extraConfig("MW-102-clip-A", 160, 120), a);
    const GUIWindowId idB = manager.create(extraConfig("MW-102-clip-B", 200, 150), b);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    WidgetTree* treeA = manager.findTree(idA);
    WidgetTree* treeB = manager.findTree(idB);
    ASSERT_NE(treeA, nullptr);
    ASSERT_NE(treeB, nullptr);

    treeA->setClipboardText("clip-a");
    EXPECT_EQ(treeA->getClipboardText(), "clip-a");
    EXPECT_EQ(treeB->getClipboardText(), "clip-a");

    treeB->setClipboardText("clip-b");
    EXPECT_EQ(treeB->getClipboardText(), "clip-b");
    EXPECT_EQ(treeA->getClipboardText(), "clip-b");

    WidgetTree windowless({.width = 64, .height = 64});
    windowless.setClipboardText("local");
    EXPECT_EQ(windowless.getClipboardText(), "local");
}

TEST(GUIWindowManagerTest, FocusLostClearsHoverDuringDragWithoutInjectingFarPointer)
{
    NamedWindowDelegate a;
    a.name = "A";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    const GUIWindowId idA = manager.create(extraConfig("MW-102-drag-A", 160, 120), a);
    if (idA == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    WidgetTree* treeA = manager.findTree(idA);
    ASSERT_NE(treeA, nullptr);
    a.button->setTooltip("tip-a");
    manager.tickAll(0.0f);

    MouseMoveEvent moveA(20.0f, 16.0f);
    moveA._windowID = idA;
    ASSERT_TRUE(manager.dispatchEvent(moveA));
    EXPECT_EQ(treeA->getHovered(), a.button.get());
    for (int i = 0; i < 40; ++i) {
        manager.tickAll(0.0f);
    }
    ASSERT_NE(treeA->getTooltipHost(), nullptr);

    treeA->beginDrag(a.button.get(), UIDragDropOperation::make("keep-drag", "Ghost"), {}, false);
    ASSERT_TRUE(treeA->isDragging());

    ASSERT_TRUE(manager.dispatchEvent(WindowFocusLostEvent(idA)));
    EXPECT_EQ(treeA->getHovered(), nullptr);
    EXPECT_EQ(treeA->getTooltipHost(), nullptr);
    EXPECT_TRUE(treeA->isDragging());
    EXPECT_EQ(treeA->getDragSource(), a.button.get());
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
    EXPECT_EQ(kernel.run(AppAutomationRunOptions{.exitAfterTick = 2}), 0);
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
    int         drops = 0;
    std::string lastPayload;

    explicit CrossWindowDropTarget(std::string name) : UIElement(std::move(name))
    {
        auto drop       = std::make_shared<UIDropTargetBehavior>();
        drop->canAccept = [this](UIElement&, const UIDragDropOperation&, const glm::vec2& point) {
            return hitTestLayoutRect(point);
        };
        drop->handleDrop = [this](UIElement&, const UIDragDropOperation& operation, const glm::vec2&) {
            ++drops;
            lastPayload = operation.payload;
        };
        addBehavior(drop);
    }
};

struct CaptureProbe final : public UIElement
{
    int       moves = 0;
    glm::vec2 last{};

    explicit CaptureProbe(std::string name) : UIElement(std::move(name)) {}

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        const EEvent::T type = event.getEventType();
        if (type == EEvent::MouseButtonPressed) {
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        }
        if (ctx.bViaCapture && type == EEvent::MouseMoved) {
            ++moves;
            last = ctx.logicalPoint;
            return true;
        }
        if (ctx.bViaCapture && type == EEvent::MouseButtonReleased) {
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            return true;
        }
        return false;
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

    treeA->beginDrag(sourceDel.source.get(), UIDragDropOperation::make("cross-payload", "Ghost"), {}, false);
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

TEST(GUIAppCrossWindowDragTest, SourceTaggedMoveHitsForeignWindowUsingGlobalMouse)
{
    NamedWindowDelegate primary;
    CrossWindowSourceDelegate sourceDel;
    CrossWindowTargetDelegate targetDel;

    GUIApp app(extraConfig("MW-301-global-primary", 64, 64), primary);
    FGUIWindowHostConfig cfgA = extraConfig("MW-301-global-A", 160, 120);
    cfgA.bHasPosition         = true;
    cfgA.posX                 = 80;
    cfgA.posY                 = 80;
    FGUIWindowHostConfig cfgB = extraConfig("MW-301-global-B", 160, 120);
    cfgB.bHasPosition         = true;
    cfgB.posX                 = 420;
    cfgB.posY                 = 80;
    const GUIWindowId idA = app.openWindow(cfgA, sourceDel);
    const GUIWindowId idB = app.openWindow(cfgB, targetDel);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    IGUIWindowSession* sessionA = app.findSession(idA);
    IGUIWindowSession* sessionB = app.findSession(idB);
    ASSERT_NE(sessionA, nullptr);
    ASSERT_NE(sessionB, nullptr);
    INativeWindow* nativeA = sessionA->nativeWindow();
    INativeWindow* nativeB = sessionB->nativeWindow();
    ASSERT_NE(nativeA, nullptr);
    ASSERT_NE(nativeB, nullptr);

    int ax = 0;
    int ay = 0;
    int aw = 0;
    int ah = 0;
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    ASSERT_TRUE(nativeA->getWindowPosition(ax, ay));
    nativeA->getWindowSize(aw, ah);
    ASSERT_TRUE(nativeB->getWindowPosition(bx, by));
    nativeB->getWindowSize(bw, bh);
    if (bx < ax + aw + 16) {
        ASSERT_TRUE(nativeB->setWindowPosition(ax + aw + 40, ay));
        ASSERT_TRUE(nativeB->getWindowPosition(bx, by));
    }

    app.onTick(0.0f);
    WidgetTree* treeA = app.findTree(idA);
    WidgetTree* treeB = app.findTree(idB);
    ASSERT_NE(treeA, nullptr);
    ASSERT_NE(treeB, nullptr);
    treeA->layout();
    treeB->layout();

    treeA->beginDrag(sourceDel.source.get(), UIDragDropOperation::make("global-payload", "Ghost"), {}, false);
    MouseMoveEvent adopt(20.0f, 16.0f);
    adopt._windowID = idA;
    app.onEvent(adopt);
    ASSERT_TRUE(app.isCrossWindowDragActive());
    EXPECT_EQ(app.crossWindowDragSourceId(), idA);

    OsEventPump::warpGlobalMouse(static_cast<float>(bx + bw / 2), static_cast<float>(by + bh / 2));
    OsEventPump::pump();

    MouseMoveEvent clamped(20.0f, 16.0f);
    clamped._windowID = idA;
    app.onEvent(clamped);
    EXPECT_EQ(app.crossWindowDragHoverId(), idB);
    EXPECT_EQ(treeB->getDropTarget(), targetDel.target.get());
    EXPECT_TRUE(treeA->isDragging());

    MouseButtonReleasedEvent release(EMouse::Left);
    release._windowID = idA;
    app.onEvent(release);
    EXPECT_FALSE(app.isCrossWindowDragActive());
    EXPECT_EQ(targetDel.target->drops, 1);
    EXPECT_EQ(targetDel.target->lastPayload, "global-payload");
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
    treeA->beginDrag(sourceDel.source.get(), UIDragDropOperation::make("keep", "Keep"), {}, false);
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
    treeA->beginDrag(sourceDel.source.get(), UIDragDropOperation::make("defer", "Defer"), {}, false);
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

TEST(GUIWindowManagerTest, SessionOwnsIsolatedTreeSnapshotAndNullSurfaceWithoutDevice)
{
    NamedWindowDelegate a;
    a.name = "A";
    NamedWindowDelegate b;
    b.name = "B";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    IGUIWindowCoordinator& coordinator = manager;

    const GUIWindowId idA = coordinator.createSession(extraConfig("MW-702-A", 160, 120), a);
    const GUIWindowId idB = coordinator.createSession(extraConfig("MW-702-B", 200, 150), b);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    IGUIWindowSession* sessionA = coordinator.findSession(idA);
    IGUIWindowSession* sessionB = coordinator.findSession(idB);
    ASSERT_NE(sessionA, nullptr);
    ASSERT_NE(sessionB, nullptr);
    EXPECT_NE(sessionA, sessionB);
    EXPECT_NE(sessionA->tree(), sessionB->tree());
    EXPECT_NE(sessionA->nativeWindow(), sessionB->nativeWindow());
    EXPECT_NE(sessionA->snapshot(), sessionB->snapshot());
    EXPECT_EQ(sessionA->surfaceContext(), nullptr);
    EXPECT_EQ(sessionB->surfaceContext(), nullptr);

    manager.tickAll(0.0f);
    ASSERT_NE(sessionA->snapshot(), nullptr);
    ASSERT_NE(sessionB->snapshot(), nullptr);
    EXPECT_FALSE(sessionA->snapshot()->items.empty());
    EXPECT_FALSE(sessionB->snapshot()->items.empty());
}

TEST(GUIWindowManagerTest, RealizeNativeDockPlacementBindsSessionWithoutMigratingWidget)
{
    NamedWindowDelegate content;
    content.name = "DockWin";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    FDockContext dock;
    dock.bAllowFloating = true;
    dock.bAllowTearOff  = true;
    dock.hostWindowId   = 1;
    auto panelWidget = std::make_shared<UICanvasPanel>("Torn");
    const DockPanelId panelId = dock.addPanel("torn", "Torn", panelWidget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    const FDockFloatingWindowId placementId =
        dock.tearOffPanel(panelId, {8.0f, 8.0f}, {180.0f, 120.0f}, EDockFloatingProjection::NativeWindow);
    ASSERT_NE(placementId, kInvalidFloatingWindowId);
    EXPECT_EQ(dock.findFloatingById(placementId)->targetWindowId, 0u);

    const GUIWindowId id = realizeNativeDockPlacement(manager, dock, placementId, content);
    if (id == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    EXPECT_EQ(dock.findFloatingById(placementId)->targetWindowId, id);
    IGUIWindowSession* session = manager.findSession(id);
    ASSERT_NE(session, nullptr);
    ASSERT_NE(session->tree(), nullptr);
    EXPECT_FALSE(session->tree()->contains(*panelWidget));
    EXPECT_EQ(panelWidget->getTree(), nullptr);
    EXPECT_EQ(manager.extraWindowCount(), 1u);
    EXPECT_EQ(realizeNativeDockPlacement(manager, dock, placementId, content), id);
    EXPECT_EQ(manager.extraWindowCount(), 1u);

    const DockPanelId overlayId = dock.addPanel("over", "Over", std::make_shared<UICanvasPanel>("O"));
    const FDockFloatingWindowId overlayFloating =
        dock.tearOffPanel(overlayId, {0.0f, 0.0f}, {80.0f, 80.0f});
    EXPECT_EQ(realizeNativeDockPlacement(manager, dock, overlayFloating, content), 0u);
    EXPECT_EQ(manager.extraWindowCount(), 1u);
}

TEST(GUIWindowManagerTest, RealizeNativeDockPlacementUsesScreenOriginNotTreeLocal)
{
    NamedWindowDelegate content;
    content.name = "ScreenDock";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    FDockContext dock;
    dock.bAllowFloating = true;
    dock.bAllowTearOff  = true;
    auto panelWidget = std::make_shared<UICanvasPanel>("Torn");
    const DockPanelId panelId = dock.addPanel("torn", "Torn", panelWidget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    const FDockFloatingWindowId placementId =
        dock.tearOffPanel(panelId, {424.0f, 318.0f}, {180.0f, 120.0f}, EDockFloatingProjection::NativeWindow);
    ASSERT_NE(placementId, kInvalidFloatingWindowId);
    ASSERT_TRUE(dock.setFloatingGeometrySpace(placementId, EDockGeometrySpace::Screen));

    const GUIWindowId id = realizeNativeDockPlacement(manager, dock, placementId, content);
    if (id == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    IGUIWindowSession* session = manager.findSession(id);
    ASSERT_NE(session, nullptr);
    auto* owned = dynamic_cast<GUIWindowSession*>(session);
    ASSERT_NE(owned, nullptr);
    EXPECT_TRUE(owned->config.bHasPosition);
    EXPECT_EQ(owned->config.posX, 424);
    EXPECT_EQ(owned->config.posY, 318);
    ASSERT_NE(session->nativeWindow(), nullptr);
    int x = 0;
    int y = 0;
    if (session->nativeWindow()->getWindowPosition(x, y)) {
        EXPECT_NE(x, 8) << "TreeLocal overlay coords must not become OS origin";
        EXPECT_NE(y, 8);
    }
}

TEST(GUIAppExtraWindowTest, ExposesCoordinatorSessions)
{
    NamedWindowDelegate primary;
    primary.name = "Primary";
    NamedWindowDelegate extra;
    extra.name = "Extra";

    GUIApp app(extraConfig("MW-702-primary", 64, 64), primary);
    IGUIWindowCoordinator& coordinator = app.windowCoordinator();
    const GUIWindowId extraId = app.openWindow(extraConfig("MW-702-extra", 160, 120), extra);
    if (extraId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    IGUIWindowSession* session = app.findSession(extraId);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session, coordinator.findSession(extraId));
    EXPECT_EQ(session->id(), extraId);
    EXPECT_EQ(session->tree(), app.findTree(extraId));
    EXPECT_EQ(app.findSession(0), nullptr);
}

/// A GUI app's windows are one set, whether they were opened at startup or
/// later. The id is the only handle a caller needs: it never has to know that
/// the window it is asking about is "the primary" one, and the app never
/// answers a lookup with "that is the other kind of window".
///
/// The startup window only joins the registry once `init()` has created it, so
/// this case pins the other half: before that, the registry holds exactly the
/// windows that exist, and every lookup still answers by id.
TEST(GUIAppWindowRegistryTest, EveryWindowIsASessionInOneRegistry)
{
    NamedWindowDelegate primary;
    NamedWindowDelegate first;
    NamedWindowDelegate second;

    GUIApp app(extraConfig("MW-703-primary", 64, 64), primary);
    EXPECT_FALSE(app.getPrimaryWindow().isInitialized());
    EXPECT_EQ(app.windowCount(), 0u);
    EXPECT_TRUE(app.sessions().empty());

    const GUIWindowId firstId  = app.openWindow(extraConfig("MW-703-first", 160, 120), first);
    const GUIWindowId secondId = app.openWindow(extraConfig("MW-703-second", 200, 150), second);
    if (firstId == 0 || secondId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    // The registry is the window set, and it answers for each window by id.
    EXPECT_EQ(app.windowCount(), app.extraWindowCount());
    std::vector<GUIWindowId> visited;
    app.forEachSession([&visited](IGUIWindowSession& session) { visited.push_back(session.id()); });
    ASSERT_EQ(visited.size(), 2u);
    EXPECT_EQ(visited[0], firstId);
    EXPECT_EQ(visited[1], secondId);
    for (GUIWindowId id : {firstId, secondId}) {
        IGUIWindowSession* session = app.findSession(id);
        ASSERT_NE(session, nullptr);
        EXPECT_EQ(session->id(), id);
        EXPECT_EQ(session->tree(), app.findTree(id));
        EXPECT_EQ(session->surfaceContext(), nullptr);
        EXPECT_FALSE(session->closeRequested());
    }

    app.closeWindow(firstId);
    app.onTick(0.0f);
    EXPECT_EQ(app.windowCount(), 1u);
    EXPECT_EQ(app.findSession(firstId), nullptr);
    ASSERT_NE(app.findSession(secondId), nullptr);
}

struct DockWindowDelegate final : IGUIAppDelegate
{
    std::shared_ptr<FDockContext> dock;
    std::shared_ptr<UIDockSpace>  space;
    std::shared_ptr<UICanvasPanel>      extraPanel;

    void buildUI(WidgetTree& tree) override
    {
        dock = std::make_shared<FDockContext>();
        dock->bAllowDocking = true;
        space = std::make_shared<UIDockSpace>("Dock");
        FCanvasSlotArgs fill;
        fill.anchorMin = {0.0f, 0.0f};
        fill.anchorMax = {1.0f, 1.0f};
        tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), space, fill);
        space->setContext(dock);
        dock->addPanel("keep", "Keep", std::make_shared<UICanvasPanel>("Keep"));
        if (extraPanel) {
            dock->addPanel("moved", "Moved", extraPanel);
        }
    }
};

TEST(GUIAppCrossWindowDragTest, DockPanelDropTransfersWithoutDualMount)
{
    NamedWindowDelegate primary;
    DockWindowDelegate  sourceDel;
    DockWindowDelegate  targetDel;
    sourceDel.extraPanel = std::make_shared<UICanvasPanel>("Moved");

    GUIApp app(extraConfig("MW-703-primary", 64, 64), primary);
    const GUIWindowId idA = app.openWindow(extraConfig("MW-703-A", 400, 300), sourceDel);
    const GUIWindowId idB = app.openWindow(extraConfig("MW-703-B", 400, 300), targetDel);
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

    const FDockContext::FPanel* moved = sourceDel.dock->findPanelByStableKey("moved");
    ASSERT_NE(moved, nullptr);
    ASSERT_NE(moved->widget, nullptr);
    EXPECT_TRUE(treeA->contains(*moved->widget));
    EXPECT_FALSE(treeB->contains(*moved->widget));

    treeA->beginDrag(sourceDel.space.get(),
                     FDockPanelDragDropOp::make(moved->id, "Moved", sourceDel.dock.get()));
    MouseMoveEvent adopt(20.0f, 16.0f);
    adopt._windowID = idA;
    app.onEvent(adopt);
    ASSERT_TRUE(app.isCrossWindowDragActive());

    MouseMoveEvent hover(200.0f, 150.0f);
    hover._windowID = idB;
    app.onEvent(hover);
    EXPECT_EQ(app.crossWindowDragHoverId(), idB);
    ASSERT_NE(treeB->getDropTarget(), nullptr);
    EXPECT_NE(treeB->getDropTarget(), targetDel.space.get());
    EXPECT_TRUE(dynamic_cast<UIDockTabStack*>(treeB->getDropTarget()) != nullptr ||
                dynamic_cast<UIDockTabWell*>(treeB->getDropTarget()) != nullptr);

    MouseButtonReleasedEvent release(EMouse::Left);
    release._windowID = idB;
    app.onEvent(release);
    EXPECT_FALSE(app.isCrossWindowDragActive());
    EXPECT_FALSE(treeA->isDragging());
    EXPECT_EQ(sourceDel.dock->findPanelByStableKey("moved"), nullptr);
    EXPECT_FALSE(treeA->contains(*sourceDel.extraPanel));
    EXPECT_TRUE(treeB->contains(*sourceDel.extraPanel));
    EXPECT_EQ(sourceDel.extraPanel->getTree(), treeB);
}

TEST(GUIWindowManagerTest, TickTreesResizeMinimizeCloseSoakKeepsSibling)
{
    NamedWindowDelegate a;
    a.name = "A";
    NamedWindowDelegate b;
    b.name = "B";

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    const GUIWindowId idA = manager.create(extraConfig("C9-soak-A", 160, 120), a);
    const GUIWindowId idB = manager.create(extraConfig("C9-soak-B", 200, 150), b);
    if (idA == 0 || idB == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    WidgetTree* treeA = manager.findTree(idA);
    WidgetTree* treeB = manager.findTree(idB);
    ASSERT_NE(treeA, nullptr);
    ASSERT_NE(treeB, nullptr);

    constexpr int kFrames = 64;
    for (int frame = 0; frame < kFrames; ++frame) {
        if (frame == 8) {
            ASSERT_TRUE(manager.dispatchEvent(WindowResizeEvent(idA, 280, 160)));
        }
        if (frame == 20) {
            ASSERT_TRUE(manager.dispatchEvent(WindowMinimizeEvent(idB)));
        }
        if (frame == 28) {
            ASSERT_TRUE(manager.dispatchEvent(WindowRestoreEvent(idB)));
        }
        if (frame == 48) {
            ASSERT_TRUE(manager.dispatchEvent(WindowCloseEvent(idA)));
            EXPECT_TRUE(manager.findSession(idA)->closeRequested());
            ASSERT_TRUE(manager.destroySession(idA));
            EXPECT_EQ(manager.findTree(idA), nullptr);
        }

        manager.tickTrees(0.016f);
        manager.renderAll();
        EXPECT_EQ(manager.findTree(idB), treeB);
        EXPECT_NE(treeB->getLogicalExtent().width, 0u);
    }

    EXPECT_EQ(manager.findTree(idA), nullptr);
    ASSERT_NE(manager.findTree(idB), nullptr);
    EXPECT_EQ(manager.extraWindowCount(), 1u);
    EXPECT_EQ(treeB->getLogicalExtent().width, 200u);
    EXPECT_GT(b.updates, a.updates);
}

TEST(GUIWindowManagerTest, DragDropTileCaptureStartsSessionAndDrops)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       source = std::make_shared<UIDragDropTile>("Src", UIDragDropTile::EKind::Source);
    source->_label    = "Payload";
    auto sourceBehavior = std::make_shared<UIDragSourceBehavior>();
    sourceBehavior->bCapturePointerOnPress = true;
    sourceBehavior->operationFactory       = [](UIElement&) {
        return UIDragDropOperation::make("tile-payload", "Ghost", "workbench.payload");
    };
    source->addBehavior(sourceBehavior);

    auto target = std::make_shared<UIDragDropTile>("Dst", UIDragDropTile::EKind::Target);
    target->_label = "Drop";
    std::string dropped;
    auto targetBehavior = std::make_shared<UIDropTargetBehavior>();
    targetBehavior->canAccept = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& point) {
        return owner.hitTestLayoutRect(point) && operation.isType("workbench.payload");
    };
    targetBehavior->handleDrop = [&](UIElement&, const UIDragDropOperation& operation, const glm::vec2&) {
        dropped = operation.payload;
    };
    target->addBehavior(targetBehavior);

    FCanvasSlotArgs sourceSlot;
    sourceSlot.fixedSize = {120.0f, 30.0f};
    FCanvasSlotArgs targetSlot;
    targetSlot.offset    = {200.0f, 0.0f};
    targetSlot.fixedSize = {120.0f, 30.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, targetSlot);
    tree.layout();

    WidgetEventContext pressCtx;
    pressCtx.logicalPoint = {40.0f, 15.0f};
    WidgetEventContext moveCtx;
    moveCtx.logicalPoint = {80.0f, 15.0f};
    WidgetEventContext dropCtx;
    dropCtx.logicalPoint = {240.0f, 15.0f};

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pressCtx),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getPointerCapture(), source.get());
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(80.0f, 15.0f), moveCtx),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(tree.isDragging());
    EXPECT_EQ(tree.getPointerCapture(), nullptr);

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(240.0f, 15.0f), dropCtx),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), dropCtx),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(dropped, "tile-payload");
    EXPECT_FALSE(tree.isDragging());
}

TEST(GUIDragRouterTest, AdoptSourceFromPrimaryTreeWithoutSdl)
{
    WidgetTree tree({.width = 120, .height = 80});
    auto source = std::make_shared<UICanvasPanel>("drag-source");
    FCanvasSlotArgs slot;
    slot.offset = {4.0f, 4.0f};
    slot.fixedSize = {40.0f, 20.0f};
    (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, slot);

    GUIDragRouter router;
    router.bindPrimary(7, &tree);
    tree.beginDrag(source.get(), UIDragDropOperation::make("payload", "Ghost"), {}, false);
    router.adoptSource();
    EXPECT_TRUE(router.isActive());
    EXPECT_EQ(router.sourceId(), 7u);
    EXPECT_TRUE(tree.isDragging());
    router.cancel();
    EXPECT_FALSE(router.isActive());
    EXPECT_FALSE(tree.isDragging());
}

TEST(GUIDragRouterTest, RoutesDropOntoBoundForeignTreeWithoutSdl)
{
    WidgetTree sourceTree({.width = 160, .height = 120});
    WidgetTree targetTree({.width = 160, .height = 120});
    auto source = std::make_shared<UICanvasPanel>("drag-source");
    auto target = std::make_shared<CrossWindowDropTarget>("drop-target");
    FCanvasSlotArgs slot;
    slot.offset = {4.0f, 4.0f};
    slot.fixedSize = {140.0f, 100.0f};
    (void)sourceTree.attach(*sourceTree.getLayer(WidgetTree::ELayer::Content), source, slot);
    (void)targetTree.attach(*targetTree.getLayer(WidgetTree::ELayer::Content), target, slot);
    sourceTree.layout();
    targetTree.layout();

    GUIDragRouter router;
    router.bindPrimary(1, &sourceTree);
    router.bindWindow(2, &targetTree);
    sourceTree.beginDrag(source.get(), UIDragDropOperation::make("cross-payload", "Ghost"), {}, false);
    router.adoptSource();
    ASSERT_TRUE(router.isActive());

    MouseMoveEvent hover(40.0f, 40.0f);
    hover._windowID = 2;
    EXPECT_TRUE(router.route(hover));
    EXPECT_EQ(router.hoverId(), 2u);
    EXPECT_EQ(targetTree.getDropTarget(), target.get());
    EXPECT_FALSE(targetTree.isDragging());
    EXPECT_TRUE(sourceTree.isDragging());

    MouseButtonReleasedEvent release(EMouse::Left);
    release._windowID = 2;
    EXPECT_TRUE(router.route(release));
    EXPECT_FALSE(router.isActive());
    EXPECT_FALSE(sourceTree.isDragging());
    EXPECT_EQ(target->drops, 1);
    EXPECT_EQ(target->lastPayload, "cross-payload");
}

TEST(GUIDragRouterTest, RoutesCapturedMoveFromForeignTreeWithoutSdl)
{
    WidgetTree sourceTree({.width = 160, .height = 120});
    WidgetTree foreignTree({.width = 160, .height = 120});
    auto probe = std::make_shared<CaptureProbe>("capture-probe");
    FCanvasSlotArgs slot;
    slot.offset = {4.0f, 4.0f};
    slot.fixedSize = {140.0f, 100.0f};
    (void)sourceTree.attach(*sourceTree.getLayer(WidgetTree::ELayer::Content), probe, slot);
    sourceTree.layout();

    WidgetEventContext pressCtx;
    pressCtx.logicalPoint = {20.0f, 20.0f};
    (void)sourceTree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pressCtx);
    ASSERT_EQ(sourceTree.getPointerCapture(), probe.get());

    GUIDragRouter router;
    router.bindPrimary(1, &sourceTree);
    router.bindWindow(2, &foreignTree);
    router.adoptCapture();
    ASSERT_TRUE(router.isCaptureActive());
    EXPECT_EQ(router.captureId(), 1u);

    MouseMoveEvent move(80.0f, 40.0f);
    move._windowID = 2;
    EXPECT_TRUE(router.route(move));
    EXPECT_EQ(probe->moves, 1);
    EXPECT_EQ(probe->last, glm::vec2(80.0f, 40.0f));
    EXPECT_EQ(sourceTree.getPointerCapture(), probe.get());
    EXPECT_EQ(foreignTree.getHovered(), nullptr);

    MouseButtonReleasedEvent release(EMouse::Left);
    release._windowID = 2;
    EXPECT_TRUE(router.route(release));
    EXPECT_EQ(sourceTree.getPointerCapture(), nullptr);
    EXPECT_FALSE(router.isCaptureActive());
}

TEST(GUIDragRouterTest, ModalBlocksForeignWindowPointerWithoutSdl)
{
    WidgetTree modalTree({.width = 160, .height = 120});
    WidgetTree foreignTree({.width = 160, .height = 120});
    auto overlay = std::make_shared<UIPopupOverlay>("app-modal");
    overlay->setRole(UIPopupOverlay::EOverlayRole::Modal);
    overlay->open(modalTree);
    ASSERT_TRUE(modalTree.hasModalPopup());
    EXPECT_FALSE(foreignTree.hasModalPopup());

    GUIDragRouter router;
    router.bindPrimary(1, &modalTree);
    router.bindWindow(2, &foreignTree);
    EXPECT_EQ(router.modalWindowId(), 1u);

    MouseMoveEvent move(40.0f, 40.0f);
    move._windowID = 2;
    EXPECT_TRUE(router.route(move));
    EXPECT_EQ(foreignTree.getHovered(), nullptr);
}

TEST(GUIDragRouterTest, TextInputWindowFollowsFocusedFieldWithoutSdl)
{
    WidgetTree primary({.width = 160, .height = 80});
    WidgetTree extra({.width = 160, .height = 80});
    auto field = std::make_shared<UITextField>("ime-field");
    FCanvasSlotArgs slot;
    slot.offset = {4.0f, 4.0f};
    slot.fixedSize = {120.0f, 24.0f};
    (void)extra.attach(*extra.getLayer(WidgetTree::ELayer::Content), field, slot);
    extra.setFocus(field.get());
    ASSERT_TRUE(extra.wantsTextInput());
    EXPECT_FALSE(primary.wantsTextInput());

    GUIDragRouter router;
    router.bindPrimary(1, &primary);
    router.bindWindow(2, &extra);
    EXPECT_EQ(router.textInputWindowId(), 2u);
}

TEST(GUIDragRouterTest, CursorReadsCaptureWidgetWithoutSdl)
{
    WidgetTree tree({.width = 160, .height = 80});
    auto probe = std::make_shared<CaptureProbe>("cursor-probe");
    FCanvasSlotArgs slot;
    slot.offset = {4.0f, 4.0f};
    slot.fixedSize = {120.0f, 24.0f};
    (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), probe, slot);
    tree.layout();

    WidgetEventContext pressCtx;
    pressCtx.logicalPoint = {10.0f, 10.0f};
    (void)tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pressCtx);

    GUIDragRouter router;
    router.bindPrimary(3, &tree);
    router.adoptCapture();
    EXPECT_EQ(router.cursor(), probe->getCursor());
}

TEST(GUIDragRouterTest, WantsDesktopOverlayWhenPointerLeavesTreeExtent)
{
    WidgetTree tree({.width = 80, .height = 60});
    auto source = std::make_shared<UICanvasPanel>("drag-source");
    FCanvasSlotArgs slot;
    slot.offset = {4.0f, 4.0f};
    slot.fixedSize = {40.0f, 20.0f};
    (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, slot);

    GUIDragRouter router;
    router.bindPrimary(1, &tree);
    tree.beginDrag(source.get(), UIDragDropOperation::make("payload", "Ghost"), {}, false);
    router.adoptSource();
    ASSERT_TRUE(router.isActive());

    MouseMoveEvent move(200.0f, 10.0f);
    move._windowID = 1;
    EXPECT_TRUE(router.route(move));
    EXPECT_TRUE(router.wantsDesktopOverlay());
    EXPECT_EQ(router.hoverId(), 0u);
    EXPECT_EQ(router.overlayWindowId(), 0u);

    router.cancel();
    EXPECT_FALSE(router.isActive());
    EXPECT_FALSE(router.wantsDesktopOverlay());
}

TEST(GUIWindowManagerTest, DragOverlaySessionIsExemptFromFocusAndInput)
{
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    NamedWindowDelegate delegate;
    FGUIWindowHostConfig config;
    config.title        = "DragOverlay";
    config.width        = 168;
    config.height       = 32;
    config.bDragOverlay = true;
    config.chromeMode   = EWindowChromeMode::ClientDrawn;
    const GUIWindowId id = manager.createSession(config, delegate);
    ASSERT_NE(id, 0u);
    EXPECT_TRUE(manager.isHostOverlay(id));
    EXPECT_EQ(manager.focusedWindowId(), 0u);

    MouseMoveEvent move(4.0f, 4.0f);
    move._windowID = id;
    EXPECT_TRUE(manager.dispatchEvent(move));
    EXPECT_EQ(manager.destroySession(id), true);
    manager.shutdown();
}

TEST(GUIWindowManagerTest, TakeTreeMovesWidgetTreeWithoutDestroyingWidgets)
{
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    NamedWindowDelegate delegate;
    FGUIWindowHostConfig config;
    config.title  = "TakeTree";
    config.width  = 160;
    config.height = 90;
    const GUIWindowId id = manager.createSession(config, delegate);
    if (id == 0) {
        GTEST_SKIP() << "native extra window unavailable";
    }
    WidgetTree* original = manager.findTree(id);
    ASSERT_NE(original, nullptr);
    ASSERT_NE(delegate.button, nullptr);
    EXPECT_EQ(delegate.button->getTree(), original);

    std::unique_ptr<WidgetTree> stolen = manager.takeTree(id);
    ASSERT_NE(stolen.get(), nullptr);
    EXPECT_EQ(stolen.get(), original);
    EXPECT_EQ(manager.findTree(id), nullptr);
    EXPECT_EQ(delegate.button->getTree(), original);

    NamedWindowDelegate overlayDelegate;
    FGUIWindowHostConfig overlayConfig;
    overlayConfig.title        = "Overlay";
    overlayConfig.width        = 320;
    overlayConfig.height       = 200;
    overlayConfig.bDragOverlay = true;
    overlayConfig.chromeMode   = EWindowChromeMode::ClientDrawn;
    const GUIWindowId overlayId = manager.createSession(overlayConfig, overlayDelegate);
    ASSERT_NE(overlayId, 0u);
    ASSERT_TRUE(manager.adoptTree(overlayId, std::move(stolen)));
    EXPECT_EQ(manager.findTree(overlayId), original);
    EXPECT_EQ(delegate.button->getTree(), original);

    EXPECT_TRUE(manager.destroySession(id));
    EXPECT_TRUE(manager.destroySession(overlayId));
    manager.shutdown();
}

TEST(GUIDragRouterTest, ExtraDockDragKeepsSourceWindow)
{
    WidgetTree primary({.width = 80, .height = 60});
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    NamedWindowDelegate extraDelegate;
    extraDelegate.name = "Extra";
    FGUIWindowHostConfig extraConfig;
    extraConfig.title  = "Extra";
    extraConfig.width  = 240;
    extraConfig.height = 180;
    const GUIWindowId extraId = manager.createSession(extraConfig, extraDelegate);
    if (extraId == 0) {
        GTEST_SKIP() << "native extra window unavailable";
    }
    WidgetTree* extraTree = manager.findTree(extraId);
    ASSERT_NE(extraTree, nullptr);
    ASSERT_NE(extraDelegate.button, nullptr);

    extraTree->beginDrag(extraDelegate.button.get(),
                         FDockPanelDragDropOp::make(1, "Debug"),
                         {},
                         true);

    GUIDragRouter router;
    router.bindPrimary(1, &primary);
    router.bindExtras(&manager);
    router.sync();

    EXPECT_TRUE(router.isActive());
    EXPECT_NE(manager.findSession(extraId), nullptr);
    EXPECT_EQ(router.sourceId(), extraId);
    EXPECT_EQ(router.overlayWindowId(), 0u);
    EXPECT_FALSE(router.wantsDesktopOverlay());
    EXPECT_EQ(manager.findTree(extraId), extraTree);

    router.cancel();
    EXPECT_FALSE(router.isActive());
    EXPECT_EQ(router.overlayWindowId(), 0u);
    EXPECT_NE(manager.findSession(extraId), nullptr);
    manager.shutdown();
}

TEST(GUIWindowManagerTest, NativeWindowHideShowDoesNotDestroySession)
{
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    NamedWindowDelegate delegate;
    delegate.name = "HideShow";
    FGUIWindowHostConfig config;
    config.title  = "HideShow";
    config.width  = 160;
    config.height = 120;
    const GUIWindowId id = manager.createSession(config, delegate);
    if (id == 0) {
        GTEST_SKIP() << "native extra window unavailable";
    }
    INativeWindow* native = manager.findNative(id);
    ASSERT_NE(native, nullptr);
    ASSERT_TRUE(native->hide());
    EXPECT_TRUE(native->isHidden());
    EXPECT_NE(manager.findSession(id), nullptr);
    ASSERT_TRUE(native->show());
    EXPECT_FALSE(native->isHidden());
    EXPECT_TRUE(manager.destroySession(id));
    manager.shutdown();
}

TEST(GUIDragRouterTest, ForeignHoverShowsExternalGhostWithoutPickup)
{
    WidgetTree sourceTree({.width = 160, .height = 120});
    WidgetTree hoverTree({.width = 160, .height = 120});
    auto source = std::make_shared<UICanvasPanel>("drag-source");
    auto target = std::make_shared<CrossWindowDropTarget>("drop-target");
    target->_hitFilter = EWidgetHitFilter::Stop;
    FCanvasSlotArgs slot;
    slot.offset    = {4.0f, 4.0f};
    slot.fixedSize = {120.0f, 80.0f};
    (void)sourceTree.attach(*sourceTree.getLayer(WidgetTree::ELayer::Content), source, slot);
    (void)hoverTree.attach(*hoverTree.getLayer(WidgetTree::ELayer::Content), target, slot);
    sourceTree.layout();
    hoverTree.layout();

    sourceTree.beginDrag(source.get(), UIDragDropOperation::make("payload", "Ghost"));
    GUIDragRouter router;
    router.bindPrimary(1, &hoverTree);
    router.bindWindow(2, &sourceTree);
    router.sync();
    ASSERT_TRUE(router.isActive());
    EXPECT_EQ(router.sourceId(), 2u);

    MouseMoveEvent hover(40.0f, 40.0f);
    hover._windowID = 1;
    EXPECT_TRUE(router.route(hover));
    EXPECT_EQ(router.hoverId(), 1u);
    EXPECT_EQ(hoverTree.getDropTarget(), target.get());
    EXPECT_EQ(router.overlayWindowId(), 0u);

    UIElement* sourceGhost = nullptr;
    if (UIElement* layer = sourceTree.getLayer(WidgetTree::ELayer::DragIme)) {
        for (const UIElementRef& child : layer->getChildren()) {
            if (child && child->_name == "DragGhost") {
                sourceGhost = child.get();
            }
        }
    }
    ASSERT_NE(sourceGhost, nullptr);
    EXPECT_EQ(sourceGhost->getVisibility(), EWidgetVisibility::Hidden);

    UIElement* hoverGhost = nullptr;
    if (UIElement* layer = hoverTree.getLayer(WidgetTree::ELayer::DragIme)) {
        for (const UIElementRef& child : layer->getChildren()) {
            if (child && child->_name == "DragGhost") {
                hoverGhost = child.get();
            }
        }
    }
    ASSERT_NE(hoverGhost, nullptr);
    EXPECT_EQ(hoverGhost->getVisibility(), EWidgetVisibility::SelfHitTestInvisible);

    router.cancel();
}

} // namespace ya
