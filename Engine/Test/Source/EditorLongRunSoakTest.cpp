// Phase 9C long-run soak. Repeats the editor tab/theme/resource paths that
// leak paint cache or dirty identity when they only fail after many cycles:
//   - attach/detach the same subtree (dock tab hide/show)
//   - destroy/recreate a subtree (document close / hot reload)
//   - theme switch settles to rebuiltWidgets==0
//   - deferred texture resolver generation miss/hit
//
// The target links ONLY the GUI closure.

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/Texture.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace ya
{

namespace
{

std::shared_ptr<Texture> makeFakeTexture()
{
    return std::shared_ptr<Texture>(reinterpret_cast<Texture*>(static_cast<uintptr_t>(0x1)),
                                    [](Texture*) {});
}

std::shared_ptr<UITheme> makePanelTheme(const glm::vec4& fill)
{
    auto theme = std::make_shared<UITheme>();
    FPanelStyle style;
    style.fillColor = FBrush::solid(fill);
    theme->define<FPanelStyle>("panel", style);
    return theme;
}

} // namespace

TEST(EditorLongRunSoakTest, RepeatedAttachDetachSameSubtreeDoesNotLeakDrawItems)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       host = std::make_shared<UIPanel>("TabHost");
    FCanvasSlotArgs hostSlot;
    hostSlot.fixedSize = {200.0f, 260.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), host, hostSlot).valid());

    std::vector<std::shared_ptr<UIPanel>> cells;
    cells.reserve(8);
    for (int i = 0; i < 8; ++i) {
        auto cell = std::make_shared<UIPanel>("Cell" + std::to_string(i));
        FCanvasSlotArgs cellSlot;
        cellSlot.offset    = {8.0f, 8.0f + static_cast<float>(i) * 28.0f};
        cellSlot.fixedSize = {120.0f, 24.0f};
        ASSERT_TRUE(tree.attach(*host, cell, cellSlot).valid());
        cells.push_back(std::move(cell));
    }

    size_t attachedItems = 0;

    constexpr int kCycles = 64;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        if (cycle > 0) {
            ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), host, hostSlot).valid());
        }
        const UIFrameSnapshot attached = tree.buildSnapshot(UIFrameBuildContext{});
        ASSERT_FALSE(attached.items.empty());
        if (cycle == 0) {
            attachedItems = attached.items.size();
        }
        EXPECT_EQ(attached.items.size(), attachedItems);

        tree.buildSnapshot(UIFrameBuildContext{});
        EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
        EXPECT_EQ(tree.getPerfStats().drawItems, attachedItems);

        tree.detach(*host);
        EXPECT_FALSE(host->isAttached());
        EXPECT_TRUE(tree.buildSnapshot(UIFrameBuildContext{}).items.empty());
    }

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), host, hostSlot).valid());
    EXPECT_EQ(tree.buildSnapshot(UIFrameBuildContext{}).items.size(), attachedItems);
}

TEST(EditorLongRunSoakTest, RepeatedDestroyRecreateSubtreeGetsFreshIdentity)
{
    WidgetTree tree({.width = 320, .height = 200});
    uint64_t   lastId     = 0;
    size_t     itemCount  = 0;

    constexpr int kCycles = 64;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        auto panel = std::make_shared<UIPanel>("ReloadPanel");
        auto child = std::make_shared<UIButton>("ReloadButton");
        EXPECT_GT(panel->getRuntimeId(), lastId);
        lastId = child->getRuntimeId();

        FCanvasSlotArgs panelSlot;
        panelSlot.fixedSize = {80.0f, 24.0f};
        FCanvasSlotArgs childSlot;
        childSlot.fixedSize = {60.0f, 20.0f};
        ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot).valid());
        ASSERT_TRUE(tree.attach(*panel, child, childSlot).valid());

        const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
        ASSERT_FALSE(snap.items.empty());
        if (cycle == 0) {
            itemCount = snap.items.size();
        }
        EXPECT_EQ(snap.items.size(), itemCount);

        tree.detach(*panel);
        EXPECT_TRUE(tree.buildSnapshot(UIFrameBuildContext{}).items.empty());
    }
}

TEST(EditorLongRunSoakTest, RepeatedThemeSwitchSettlesWithoutRebuild)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto       panel = std::make_shared<UIPanel>("Themed");
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

    const glm::vec4 fillA{0.15f, 0.16f, 0.20f, 1.0f};
    const glm::vec4 fillB{0.94f, 0.95f, 0.97f, 1.0f};
    auto            themeA = makePanelTheme(fillA);
    auto            themeB = makePanelTheme(fillB);

    constexpr int kCycles = 32;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        const bool        useA = (cycle % 2) == 0;
        tree.setTheme(useA ? themeA.get() : themeB.get());
        const UIFrameSnapshot dirty = tree.buildSnapshot(UIFrameBuildContext{});
        ASSERT_FALSE(dirty.items.empty());
        EXPECT_GT(tree.getPerfStats().rebuiltWidgets, 0u);
        EXPECT_EQ(dirty.items.front().color, useA ? fillA : fillB);

        tree.buildSnapshot(UIFrameBuildContext{});
        EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
        EXPECT_EQ(tree.getPerfStats().drawItems, dirty.items.size());
    }
}

TEST(EditorLongRunSoakTest, RepeatedDeferredTextureGenerationSettles)
{
    WidgetTree tree({.width = 200, .height = 120});
    auto       image = std::make_shared<UIImage>("Deferred");
    image->_assetPath = "tex:soak";
    FCanvasSlotArgs slot;
    slot.fixedSize = {64.0f, 64.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), image, slot);

    auto ready = makeFakeTexture();

    constexpr int kCycles = 32;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        UIFrameBuildContext missCtx;
        missCtx.generation      = static_cast<uint32_t>(cycle * 2 + 1);
        missCtx.textureResolver = [](const std::string&) { return std::shared_ptr<Texture>(); };
        const UIFrameSnapshot missSnap = tree.buildSnapshot(missCtx);
        bool bMissHasTexture = false;
        for (const auto& item : missSnap.items) {
            if (item.texture) {
                bMissHasTexture = true;
            }
        }
        EXPECT_FALSE(bMissHasTexture);

        UIFrameBuildContext hitCtx;
        hitCtx.generation      = static_cast<uint32_t>(cycle * 2 + 2);
        hitCtx.textureResolver = [&](const std::string& path) {
            return path == "tex:soak" ? ready : std::shared_ptr<Texture>();
        };
        const UIFrameSnapshot hitSnap = tree.buildSnapshot(hitCtx);
        EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::ResourceReady);
        bool bHitHasTexture = false;
        for (const auto& item : hitSnap.items) {
            if (item.texture == ready) {
                bHitHasTexture = true;
            }
        }
        EXPECT_TRUE(bHitHasTexture);

        tree.buildSnapshot(hitCtx);
        EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
    }
}

} // namespace ya
