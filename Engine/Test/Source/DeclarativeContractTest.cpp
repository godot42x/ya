// Phase -1A contract guards for the future React-style declarative UI path.
// These tests intentionally exercise existing WidgetTree invariants rather
// than introducing a premature DSL implementation.

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Declarative/Declarative.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Theme.h"
#include "Render/Resources/FontManager.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(DeclarativeContractTest, NoThemeStillRendersAuthoredAppearance)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto panel = std::make_shared<UIPanel>("AuthoredPanel");
    panel->setSize({120.0f, 60.0f});
    panel->setColor({0.12f, 0.24f, 0.36f, 1.0f});
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, panel).valid());

    const UIFrameSnapshot snapshot = tree.buildSnapshot({});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items.front().color, glm::vec4(0.12f, 0.24f, 0.36f, 1.0f));
}

TEST(DeclarativeContractTest, PreRegisteredFontResolvesThroughDpiQualifiedCache)
{
    constexpr uint32_t kFontSize = 77;
    auto font = std::make_shared<Font>();
    font->fontSize = static_cast<float>(kFontSize);
    font->lineHeight = static_cast<float>(kFontSize);
    font->ascent = static_cast<float>(kFontSize);
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, kFontSize, font);

    EXPECT_EQ(FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, kFontSize), font);
}

TEST(DeclarativeContractTest, SameValuePropertyMutationDoesNotDirtyTree)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto panel = std::make_shared<UIPanel>("StablePanel");
    panel->setSize({120.0f, 60.0f});
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, panel).valid());

    (void)tree.buildSnapshot({});
    (void)tree.buildSnapshot({});
    const GuiPerfStats before = tree.getPerfStats();

    panel->setSize(panel->getSize());
    panel->setColor(panel->getColor());
    (void)tree.buildSnapshot({});

    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, before.layoutDirtyTransitions);
    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, before.paintDirtyTransitions);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
}

TEST(DeclarativeContractTest, AppearanceModesCoverNoThemeAuthoredAndThemeOnly)
{
    // No theme: framework fallback remains renderable.
    WidgetTree noTheme({.width = 320, .height = 200});
    auto fallback = std::make_shared<UIPanel>("Fallback");
    fallback->setSize({120.0f, 60.0f});
    ASSERT_TRUE(noTheme.attachToLayer(WidgetTree::ELayer::Content, fallback).valid());
    const auto fallbackSnapshot = noTheme.buildSnapshot({});
    ASSERT_EQ(fallbackSnapshot.items.size(), 1u);

    // Authored appearance: explicit color wins without a theme.
    WidgetTree authored({.width = 320, .height = 200});
    auto authoredPanel = std::make_shared<UIPanel>("Authored");
    authoredPanel->setSize({120.0f, 60.0f});
    authoredPanel->setColor({0.1f, 0.2f, 0.3f, 1.0f});
    ASSERT_TRUE(authored.attachToLayer(WidgetTree::ELayer::Content, authoredPanel).valid());
    const auto authoredSnapshot = authored.buildSnapshot({});
    ASSERT_EQ(authoredSnapshot.items.size(), 1u);
    EXPECT_EQ(authoredSnapshot.items.front().color, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));

    // Theme-only: no explicit widget color, so the mounted theme supplies it.
    WidgetTree themed({.width = 320, .height = 200});
    auto theme = std::make_shared<UITheme>();
    FPanelStyle panelStyle;
    panelStyle.fillColor = FBrush::Solid({0.7f, 0.1f, 0.2f, 1.0f});
    theme->define<FPanelStyle>("panel", panelStyle);
    themed.setTheme(theme.get());
    auto themedPanel = std::make_shared<UIPanel>("Themed");
    themedPanel->setSize({120.0f, 60.0f});
    ASSERT_TRUE(themed.attachToLayer(WidgetTree::ELayer::Content, themedPanel).valid());
    const auto themedSnapshot = themed.buildSnapshot({});
    ASSERT_EQ(themedSnapshot.items.size(), 1u);
    EXPECT_EQ(themedSnapshot.items.front().color, glm::vec4(0.7f, 0.1f, 0.2f, 1.0f));
}

TEST(DeclarativeContractTest, SnapshotDoesNotDependOnLiveWidgetAfterDetach)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto panel = std::make_shared<UIPanel>("SnapshotPanel");
    panel->setSize({120.0f, 60.0f});
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, panel).valid());

    UIFrameSnapshot snapshot = tree.buildSnapshot({});
    ASSERT_EQ(snapshot.items.size(), 1u);
    const auto expectedSize = snapshot.items.front().size;

    tree.detach(*panel);
    panel.reset();

    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items.front().size, expectedSize);
}

TEST(DeclarativeContractTest, DeclarativeSnapshotIsStableAcrossRepeatedBuilds)
{
    WidgetTree tree({.width = 320, .height = 200});
    ui::UIReconciler reconciler(tree);
    const auto description = ui::column("root")
        .setSize({200.0f, 100.0f})
        .child(ui::panel("panel").setSize({120.0f, 60.0f}).setColor({0.3f, 0.4f, 0.5f, 1.0f}))
        .child(ui::text("label").setText("stable"));
    ASSERT_NE(reconciler.reconcile(description.build()), nullptr);

    const UIFrameSnapshot first = tree.buildSnapshot({});
    const UIFrameSnapshot second = tree.buildSnapshot({});
    ASSERT_EQ(first.items.size(), second.items.size());
    ASSERT_FALSE(first.items.empty());
    for (size_t index = 0; index < first.items.size(); ++index) {
        EXPECT_EQ(first.items[index].kind, second.items[index].kind);
        EXPECT_EQ(first.items[index].pos, second.items[index].pos);
        EXPECT_EQ(first.items[index].size, second.items[index].size);
        EXPECT_EQ(first.items[index].color, second.items[index].color);
        EXPECT_EQ(first.items[index].text, second.items[index].text);
    }
}

TEST(DeclarativeContractTest, DetachClearsFocusAndPointerCapture)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto button = std::make_shared<UIButton>("TransientButton");
    button->setSize({120.0f, 60.0f});
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, button).valid());
    tree.layout();

    tree.setFocus(button.get());
    tree.setPointerCapture(button.get());
    ASSERT_EQ(tree.getFocused(), button.get());
    ASSERT_EQ(tree.getPointerCapture(), button.get());

    tree.detach(*button);

    EXPECT_EQ(tree.getFocused(), nullptr);
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_FALSE(button->isAttached());
}

TEST(DeclarativeContractTest, DeclarativeReconcilerReusesStableKeysAndReordersChildren)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    const auto first = reconciler.reconcile(
        ui::column("settings", "Settings")
            .child(ui::panel("left", "Left").setSize({100.0f, 40.0f}).setColor({0.10f, 0.20f, 0.30f, 1.0f}))
            .child(ui::panel("right", "Right").setSize({120.0f, 50.0f}).setColor({0.20f, 0.30f, 0.40f, 1.0f})));

    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->getChildren().size(), 2u);
    const UIElement* left = first->getChildren()[0].get();
    const UIElement* right = first->getChildren()[1].get();

    const auto second = reconciler.reconcile(
        ui::column("settings", "Settings v2")
            .child(ui::panel("right", "Right").setSize({120.0f, 50.0f}).setColor({0.20f, 0.30f, 0.40f, 1.0f}))
            .child(ui::panel("left", "Left").setSize({100.0f, 40.0f}).setColor({0.10f, 0.20f, 0.30f, 1.0f})));

    ASSERT_EQ(second.get(), first.get());
    EXPECT_EQ(second->_stableKey, "settings");
    EXPECT_EQ(second->_name, "Settings v2");
    ASSERT_EQ(second->getChildren().size(), 2u);
    EXPECT_EQ(second->getChildren()[0].get(), right);
    EXPECT_EQ(second->getChildren()[1].get(), left);
}

TEST(DeclarativeContractTest, DeclarativeDescriptionReportsDuplicateSiblingKeys)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);
    const auto description = ui::column("root")
        .child(ui::text("duplicate").setText("A"))
        .child(ui::button("duplicate").setText("B"));

    std::string error;
    EXPECT_FALSE(reconciler.validateDescription(description.build(), &error));
    EXPECT_NE(error.find("duplicate key 'duplicate'"), std::string::npos);
    EXPECT_NE(error.find("root"), std::string::npos);
}

TEST(DeclarativeContractTest, DeclarativeReconcilerPreservesTextFieldFocusAcrossUpdates)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    const auto first = reconciler.reconcile(
        ui::column("root", "Root")
            .child(ui::textField("editor", "Editor").setText("hello").setSize({180.0f, 28.0f})));

    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->getChildren().size(), 1u);
    auto* field = dynamic_cast<UITextField*>(first->getChildren()[0].get());
    ASSERT_NE(field, nullptr);

    tree.setFocus(field);
    ASSERT_EQ(tree.getFocused(), field);

    const auto second = reconciler.reconcile(
        ui::column("root", "Root")
            .child(ui::textField("editor", "Editor").setText("world").setSize({180.0f, 28.0f})));

    ASSERT_NE(second, nullptr);
    auto* fieldAfter = dynamic_cast<UITextField*>(second->getChildren()[0].get());
    ASSERT_EQ(fieldAfter, field);
    EXPECT_EQ(tree.getFocused(), field);
}

TEST(DeclarativeContractTest, DeclarativeReconcilerRemovesStaleChildrenAndClearsTransientState)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    const auto first = reconciler.reconcile(
        ui::column("root", "Root")
            .child(ui::button("keep", "Keep").setText("Keep").setSize({80.0f, 30.0f}))
            .child(ui::button("gone", "Gone").setText("Gone").setSize({80.0f, 30.0f})));

    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->getChildren().size(), 2u);
    auto* gone = dynamic_cast<UIButton*>(first->getChildren()[1].get());
    ASSERT_NE(gone, nullptr);

    tree.setFocus(gone);
    tree.setPointerCapture(gone);
    ASSERT_EQ(tree.getFocused(), gone);
    ASSERT_EQ(tree.getPointerCapture(), gone);

    const auto second = reconciler.reconcile(
        ui::column("root", "Root")
            .child(ui::button("keep", "Keep").setText("Keep").setSize({80.0f, 30.0f})));

    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->getChildren().size(), 1u);
    EXPECT_EQ(tree.getFocused(), nullptr);
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_FALSE(gone->isAttached());
}

TEST(DeclarativeContractTest, RenderControllerWritesBackExternalStateOnNextFlush)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIRenderController controller(tree);
    std::string title = "Before";
    int renderCalls = 0;

    controller.setRenderFunction([&] {
        ++renderCalls;
        return ui::column("root", "Root")
            .child(ui::text("title", "Title").setText(title));
    });

    EXPECT_TRUE(controller.flush());
    EXPECT_EQ(renderCalls, 1);
    ASSERT_EQ(tree.getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 1u);
    auto* root = tree.getLayer(WidgetTree::ELayer::Content)->getChildren().front().get();
    ASSERT_EQ(root->getChildren().size(), 1u);
    auto* text = dynamic_cast<UIText*>(root->getChildren().front().get());
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->getText(), "Before");

    title = "After";
    controller.invalidate();
    EXPECT_TRUE(controller.flush());
    EXPECT_EQ(renderCalls, 2);
    EXPECT_EQ(text->getText(), "After");
}

TEST(DeclarativeContractTest, RenderControllerBatchesMultipleStateWrites)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIRenderController controller(tree);
    int value = 0;
    int renderCalls = 0;

    controller.setRenderFunction([&] {
        ++renderCalls;
        return ui::text("value", "Value").setText(std::to_string(value));
    });

    controller.beginBatch();
    EXPECT_TRUE(controller.isDirty());
    EXPECT_FALSE(controller.flush());
    value = 1;
    controller.invalidate();
    value = 2;
    controller.invalidate();
    EXPECT_FALSE(controller.flush());
    EXPECT_EQ(renderCalls, 0);
    controller.endBatch();

    EXPECT_TRUE(controller.flush());
    EXPECT_EQ(renderCalls, 1);
    auto* live = tree.getLayer(WidgetTree::ELayer::Content)->getChildren().front().get();
    EXPECT_EQ(dynamic_cast<UIText*>(live)->getText(), "2");
}

} // namespace ya
