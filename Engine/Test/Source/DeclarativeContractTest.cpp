// Phase -1A contract guards for the future React-style declarative UI path.
// These tests intentionally exercise existing WidgetTree invariants rather
// than introducing a premature DSL implementation.

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Declarative/Declarative.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Theme.h"
#include "Render/Resources/FontManager.h"

#include <gtest/gtest.h>

namespace ya
{

static_assert(ui::UIDescriptionFactory<decltype([] { return ui::text("compile-time"); })>);
static_assert(!ui::UIDescriptionFactory<decltype([] { return 42; })>);

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

TEST(DeclarativeContractTest, FunctionalComposeBuildsTypedSubtree)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    const auto description = ui::column("root", "Root")
        .setSize({240.0f, 120.0f})
        .compose([] {
            return ui::row("toolbar", "Toolbar")
                .setSpacing(8.0f)
                .compose([] {
                    return ui::button("save", "Save")
                        .setText("Save")
                        .setSize({80.0f, 28.0f});
                });
        })
            .compose([] {
            return ui::text("status", "Status").setText("Ready");
        });

    auto root = reconciler.reconcile(description.build());
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->getChildren().size(), 2u);
    auto* toolbar = dynamic_cast<UIContainer*>(root->getChildren()[0].get());
    ASSERT_NE(toolbar, nullptr);
    ASSERT_EQ(toolbar->getChildren().size(), 1u);
    EXPECT_NE(dynamic_cast<UIButton*>(toolbar->getChildren()[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<UIText*>(root->getChildren()[1].get()), nullptr);
}

TEST(DeclarativeContractTest, FunctionalComposeChildrenAndWhenComposeStableConditionalSiblings)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    const bool showDetails = true;
    auto first = reconciler.reconcile(
        ui::column("root")
            .composeChildren(
                [] { return ui::text("title").setText("Title"); },
                [&] {
                    return ui::panel("details")
                        .when(showDetails, [] { return ui::text("body").setText("Details"); });
                })
            .when(false, [] { return ui::text("hidden").setText("Hidden"); }));

    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->getChildren().size(), 2u);
    EXPECT_EQ(first->getChildren()[0]->_stableKey, "title");
    EXPECT_EQ(first->getChildren()[1]->_stableKey, "details");
    ASSERT_EQ(first->getChildren()[1]->getChildren().size(), 1u);
    EXPECT_EQ(first->getChildren()[1]->getChildren()[0]->_stableKey, "body");

    auto second = reconciler.reconcile(
        ui::column("root")
            .composeChildren(
                [] { return ui::text("title").setText("Title v2"); },
                [] { return ui::text("footer").setText("Footer"); }));

    ASSERT_EQ(second.get(), first.get());
    ASSERT_EQ(second->getChildren().size(), 2u);
    EXPECT_EQ(second->getChildren()[0]->_stableKey, "title");
    EXPECT_EQ(second->getChildren()[1]->_stableKey, "footer");
}

TEST(DeclarativeContractTest, TypedEnabledPropertyReconcilesWithoutReplacingWidget)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    auto first = reconciler.reconcile(
        ui::column("root")
            .child(ui::button("action").setText("Action").setEnabled(false)));
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->getChildren().size(), 1u);
    auto* button = dynamic_cast<UIButton*>(first->getChildren()[0].get());
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isEnabled());

    auto second = reconciler.reconcile(
        ui::column("root")
            .child(ui::button("action").setText("Action").setEnabled(true)));
    ASSERT_EQ(second.get(), first.get());
    ASSERT_EQ(second->getChildren().size(), 1u);
    EXPECT_EQ(second->getChildren()[0].get(), button);
    EXPECT_TRUE(button->isEnabled());
}

TEST(DeclarativeContractTest, TypedFocusPolicyReconcilesWithoutReplacingWidget)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    auto first = reconciler.reconcile(
        ui::column("root")
            .child(ui::button("action")
                .setText("Action")
                .setFocusPolicy(EWidgetFocusPolicy::Focusable)));
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->getChildren().size(), 1u);
    auto* button = dynamic_cast<UIButton*>(first->getChildren()[0].get());
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->_focusPolicy, EWidgetFocusPolicy::Focusable);

    auto second = reconciler.reconcile(
        ui::column("root")
            .child(ui::button("action")
                .setText("Action")
                .setFocusPolicy(EWidgetFocusPolicy::None)));
    ASSERT_EQ(second.get(), first.get());
    ASSERT_EQ(second->getChildren().size(), 1u);
    EXPECT_EQ(second->getChildren()[0].get(), button);
    EXPECT_EQ(button->_focusPolicy, EWidgetFocusPolicy::None);
}

TEST(DeclarativeContractTest, TypedColorPropertyReconcilesWithoutReplacingWidget)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    auto first = reconciler.reconcile(
        ui::panel("root").setColor({0.2f, 0.3f, 0.4f, 1.0f}));
    ASSERT_NE(first, nullptr);
    auto* panel = dynamic_cast<UIPanel*>(first.get());
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->getColor(), glm::vec4(0.2f, 0.3f, 0.4f, 1.0f));

    auto second = reconciler.reconcile(
        ui::panel("root").setColor({0.5f, 0.6f, 0.7f, 1.0f}));
    ASSERT_EQ(second.get(), first.get());
    EXPECT_EQ(panel->getColor(), glm::vec4(0.5f, 0.6f, 0.7f, 1.0f));
}

TEST(DeclarativeContractTest, PanelBuilderStoresTypedDescriptionPayload)
{
    const auto builder = ui::panel("panel").setColor({0.2f, 0.3f, 0.4f, 1.0f});
    const ui::UIDescription description = builder.build();

    ASSERT_TRUE(description._panel.has_value());
    ASSERT_TRUE(description._panel->color.has_value());
    EXPECT_EQ(*description._panel->color, glm::vec4(0.2f, 0.3f, 0.4f, 1.0f));
}

TEST(DeclarativeContractTest, TextBuilderStoresTypedDescriptionPayload)
{
    const auto builder = ui::text("label")
        .setText("Hello")
        .setFontSize(24)
        .setColor({0.3f, 0.4f, 0.5f, 1.0f});
    const ui::UIDescription description = builder.build();

    ASSERT_TRUE(description._textDescription.has_value());
    ASSERT_TRUE(description._textDescription->text.has_value());
    ASSERT_TRUE(description._textDescription->fontSize.has_value());
    ASSERT_TRUE(description._textDescription->color.has_value());
    EXPECT_EQ(*description._textDescription->text, "Hello");
    EXPECT_EQ(*description._textDescription->fontSize, 24u);
    EXPECT_EQ(*description._textDescription->color, glm::vec4(0.3f, 0.4f, 0.5f, 1.0f));
}

TEST(DeclarativeContractTest, ButtonAndTextFieldBuilderStoreTypedPayloads)
{
    const ui::UIDescription button = ui::button("action")
        .setText("Save")
        .onClick([] {})
        .build();
    ASSERT_TRUE(button._button.has_value());
    ASSERT_TRUE(button._button->text.has_value());
    ASSERT_TRUE(button._button->onClick.has_value());
    EXPECT_EQ(*button._button->text, "Save");

    const ui::UIDescription field = ui::textField("editor")
        .setText("hello")
        .setFontSize(18)
        .build();
    ASSERT_TRUE(field._textField.has_value());
    ASSERT_TRUE(field._textField->text.has_value());
    ASSERT_TRUE(field._textField->fontSize.has_value());
    EXPECT_EQ(*field._textField->text, "hello");
    EXPECT_EQ(*field._textField->fontSize, 18u);
}

TEST(DeclarativeContractTest, CommonDescriptionStoresSharedFields)
{
    const auto description = ui::column("root", "Root")
        .setPosition({10.0f, 20.0f})
        .setSize({100.0f, 40.0f})
        .setEnabled(false)
        .setFocusPolicy(EWidgetFocusPolicy::Focusable)
        .child(ui::text("label").setText("Hello"))
        .build();

    EXPECT_EQ(description.common.key.value(), "root");
    EXPECT_EQ(description.common.displayName.value(), "Root");
    ASSERT_TRUE(description.common.position.has_value());
    ASSERT_TRUE(description.common.size.has_value());
    ASSERT_TRUE(description.common.enabled.has_value());
    ASSERT_TRUE(description.common.focusPolicy.has_value());
    ASSERT_EQ(description.common.children.size(), 1u);
    EXPECT_EQ(description.common.position.value(), glm::vec2(10.0f, 20.0f));
    EXPECT_EQ(description.common.size.value(), glm::vec2(100.0f, 40.0f));
    EXPECT_FALSE(description.common.enabled.value());
    EXPECT_EQ(description.common.focusPolicy.value(), EWidgetFocusPolicy::Focusable);
}

TEST(DeclarativeContractTest, StaticChildrenAndFunctionalComposeSyncCommonChildren)
{
    const auto description = ui::column("root")
        .compose([] { return ui::text("title").setText("Title"); })
        .children(
            ui::panel("left").setColor({0.1f, 0.2f, 0.3f, 1.0f}),
            ui::panel("right").setColor({0.4f, 0.5f, 0.6f, 1.0f}))
        .build();

    ASSERT_EQ(description.children.size(), 3u);
    ASSERT_EQ(description.common.children.size(), 3u);
    EXPECT_EQ(description.common.children[0].key, "title");
    EXPECT_EQ(description.common.children[1].key, "left");
    EXPECT_EQ(description.common.children[2].key, "right");
}

TEST(DeclarativeContractTest, CommonChildrenCanDriveReconciliation)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    ui::UIDescription description;
    description.kind = ui::EWidgetKind::Column;
    description.key = "root";
    description.displayName = "Root";
    description.common.key = description.key;
    description.common.displayName = description.displayName;
    description.common.children.push_back(ui::text("title").setText("Title").build());

    auto live = reconciler.reconcile(description);
    ASSERT_NE(live, nullptr);
    ASSERT_EQ(live->getChildren().size(), 1u);
    EXPECT_EQ(live->getChildren()[0]->_stableKey, "title");
}

TEST(DeclarativeContractTest, TypedFontSizePropertyReconcilesWithoutReplacingWidget)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    auto first = reconciler.reconcile(
        ui::text("root").setFontSize(42));
    ASSERT_NE(first, nullptr);
    auto* text = dynamic_cast<UIText*>(first.get());
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->_fontSize, 42u);

    auto second = reconciler.reconcile(
        ui::text("root").setFontSize(24));
    ASSERT_EQ(second.get(), first.get());
    EXPECT_EQ(text->_fontSize, 24u);
}

TEST(DeclarativeContractTest, TypedButtonTextReconcilesGeneratedLabelWithoutReplacingSubtree)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    auto first = reconciler.reconcile(
        ui::button("action").setText("Save").setSize({120.0f, 30.0f}));
    ASSERT_NE(first, nullptr);
    auto* button = dynamic_cast<UIButton*>(first.get());
    ASSERT_NE(button, nullptr);
    ASSERT_EQ(button->getChildren().size(), 1u);
    auto* label = dynamic_cast<UIText*>(button->getChildren()[0].get());
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->_stableKey, "action__label");
    EXPECT_EQ(label->getText(), "Save");

    auto second = reconciler.reconcile(
        ui::button("action").setText("Save As").setSize({120.0f, 30.0f}));
    ASSERT_EQ(second.get(), first.get());
    ASSERT_EQ(second->getChildren().size(), 1u);
    EXPECT_EQ(second->getChildren()[0].get(), label);
    EXPECT_EQ(label->getText(), "Save As");
}

TEST(DeclarativeContractTest, TypedTextFieldPropertiesPreserveIdentityAndFocus)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);

    auto first = reconciler.reconcile(
        ui::textField("editor")
            .setText("hello")
            .setFontSize(18)
            .setSize({180.0f, 28.0f}));
    ASSERT_NE(first, nullptr);
    auto* field = dynamic_cast<UITextField*>(first.get());
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->_text, "hello");
    EXPECT_EQ(field->_fontSize, 18u);

    tree.setFocus(field);
    ASSERT_EQ(tree.getFocused(), field);

    auto second = reconciler.reconcile(
        ui::textField("editor")
            .setText("world")
            .setFontSize(22)
            .setSize({180.0f, 28.0f}));
    ASSERT_EQ(second.get(), first.get());
    EXPECT_EQ(second.get(), field);
    EXPECT_EQ(field->_text, "world");
    EXPECT_EQ(field->_fontSize, 22u);
    EXPECT_EQ(tree.getFocused(), field);
}

TEST(DeclarativeContractTest, TypedButtonOnClickPreservesRuntimeCallbackUnlessAuthored)
{
    WidgetTree tree({.width = 640, .height = 360});
    ui::UIReconciler reconciler(tree);
    int runtimeClickCount = 0;
    int authoredClickCount = 0;

    auto first = reconciler.reconcile(
        ui::button("action").setText("Action").onClick([&] { authoredClickCount += 1; }));
    ASSERT_NE(first, nullptr);
    auto* button = dynamic_cast<UIButton*>(first.get());
    ASSERT_NE(button, nullptr);

    button->_onClick = [&] { runtimeClickCount += 1; };
    ASSERT_EQ(runtimeClickCount, 0);
    ASSERT_EQ(authoredClickCount, 0);

    auto second = reconciler.reconcile(
        ui::button("action").setText("Action"));
    ASSERT_EQ(second.get(), first.get());
    ASSERT_EQ(button->_onClick != nullptr, true);
    button->_onClick();
    EXPECT_EQ(runtimeClickCount, 1);
    EXPECT_EQ(authoredClickCount, 0);

    auto third = reconciler.reconcile(
        ui::button("action").setText("Action").onClick([&] { authoredClickCount += 1; }));
    ASSERT_EQ(third.get(), first.get());
    ASSERT_EQ(button->_onClick != nullptr, true);
    button->_onClick();
    EXPECT_EQ(runtimeClickCount, 1);
    EXPECT_EQ(authoredClickCount, 1);
}

TEST(DeclarativeContractTest, TypedContainerPropertiesPreserveIdentityAndInvalidationScope)
{
    WidgetTree treeA({.width = 640, .height = 360});
    ui::UIReconciler reconcilerA(treeA);

    auto first = reconcilerA.reconcile(
        ui::row("root")
            .setSpacing(4.0f)
            .setPadding({2.0f, 3.0f})
            .setClipChildren(false)
            .setStretchLastChild(false)
            .child(ui::panel("left").setSize({20.0f, 10.0f}))
            .child(ui::panel("right").setSize({30.0f, 10.0f})));
    ASSERT_NE(first, nullptr);
    auto* container = dynamic_cast<UIContainer*>(first.get());
    ASSERT_NE(container, nullptr);
    EXPECT_EQ(container->getBoxLayout().getDirection(), EWidgetBoxLayout::Horizontal);
    EXPECT_FLOAT_EQ(container->getBoxLayout().getSpacing(), 4.0f);
    EXPECT_EQ(container->getBoxLayout().getPadding(), glm::vec2(2.0f, 3.0f));
    EXPECT_FALSE(container->getBoxLayout().clipsChildren());
    EXPECT_FALSE(container->getBoxLayout().stretchesLastChild());
    ASSERT_EQ(first->getChildren().size(), 2u);
    auto* left = dynamic_cast<UIPanel*>(first->getChildren()[0].get());
    auto* right = dynamic_cast<UIPanel*>(first->getChildren()[1].get());
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    const UIFrameSnapshot firstSnapshot = treeA.buildSnapshot({});
    ASSERT_GE(firstSnapshot.items.size(), 2u);
    EXPECT_FLOAT_EQ(left->_layoutRect.pos.x, 2.0f);
    EXPECT_FLOAT_EQ(right->_layoutRect.pos.x, 26.0f);
    EXPECT_EQ(left->_layoutRect.extent.x, 20.0f);
    EXPECT_EQ(right->_layoutRect.extent.x, 30.0f);

    auto second = reconcilerA.reconcile(
        ui::row("root")
            .setSpacing(12.0f)
            .setPadding({8.0f, 9.0f})
            .setClipChildren(true)
            .setStretchLastChild(true)
            .child(ui::panel("left").setSize({20.0f, 10.0f}))
            .child(ui::panel("right").setSize({30.0f, 10.0f})));
    ASSERT_EQ(second.get(), first.get());
    EXPECT_EQ(second->getChildren().size(), 2u);
    container = dynamic_cast<UIContainer*>(second.get());
    ASSERT_NE(container, nullptr);
    EXPECT_FLOAT_EQ(container->getBoxLayout().getSpacing(), 12.0f);
    EXPECT_EQ(container->getBoxLayout().getPadding(), glm::vec2(8.0f, 9.0f));
    EXPECT_TRUE(container->getBoxLayout().clipsChildren());
    EXPECT_TRUE(container->getBoxLayout().stretchesLastChild());
    const UIFrameSnapshot secondSnapshot = treeA.buildSnapshot({});
    ASSERT_GE(secondSnapshot.items.size(), 2u);
    auto* leftB = dynamic_cast<UIPanel*>(second->getChildren()[0].get());
    auto* rightB = dynamic_cast<UIPanel*>(second->getChildren()[1].get());
    ASSERT_NE(leftB, nullptr);
    ASSERT_NE(rightB, nullptr);
    EXPECT_FLOAT_EQ(leftB->_layoutRect.pos.x, 8.0f);
    EXPECT_FLOAT_EQ(rightB->_layoutRect.pos.x, 40.0f);
    EXPECT_EQ(rightB->_layoutRect.extent.x, 52.0f);

    WidgetTree clipTreeA({.width = 640, .height = 360});
    ui::UIReconciler clipReconcilerA(clipTreeA);
    auto clipped = clipReconcilerA.reconcile(
        ui::row("clip_root")
            .setSize({50.0f, 20.0f})
            .setClipChildren(false)
            .child(ui::panel("overflow").setSize({80.0f, 10.0f})));
    ASSERT_NE(clipped, nullptr);
    auto* overflow = dynamic_cast<UIPanel*>(clipped->getChildren()[0].get());
    ASSERT_NE(overflow, nullptr);
    const UIFrameSnapshot unclippedSnapshot = clipTreeA.buildSnapshot({});
    ASSERT_GE(unclippedSnapshot.items.size(), 1u);
    EXPECT_EQ(overflow->_layoutRect.extent.x, 80.0f);

    auto clippedAgain = clipReconcilerA.reconcile(
        ui::row("clip_root")
            .setSize({50.0f, 20.0f})
            .setClipChildren(true)
            .child(ui::panel("overflow").setSize({80.0f, 10.0f})));
    ASSERT_EQ(clippedAgain.get(), clipped.get());
    const UIFrameSnapshot clippedSnapshot = clipTreeA.buildSnapshot({});
    ASSERT_GE(clippedSnapshot.items.size(), 1u);
    EXPECT_TRUE(clippedSnapshot.items[0].bClipped);
    EXPECT_EQ(clippedSnapshot.items[0].clip.extent.x, 50.0f);
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
