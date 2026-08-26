// Contract guards for the static live-construct DSL (Slate Construct / SNew).
// Builders materialize UIElement directly. There is no UIDescription,
// UIReconciler, or UIRenderController.

#include "GUI/Declarative/Declarative.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UITypeIds.h"
#include "Render/Resources/FontManager.h"

#include "Core/Event.h"
#include <gtest/gtest.h>

namespace ya
{

namespace
{
UIElement* findChildByKey(UIElement* parent, const char* key)
{
    for (const auto& c : parent->getChildren()) {
        if (c->_stableKey == key) {
            return c.get();
        }
    }
    return nullptr;
}
} // namespace

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

TEST(DeclarativeContractTest, DslCreatedWidgetsCarryRegistryTypeId)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    const UIElementRef root = ui::build(
        ui::column("root")
            .children(
                ui::row("row"),
                ui::panel("panel").setColor({0.3f, 0.4f, 0.5f, 1.0f}),
                ui::text("label").setText("hi"),
                ui::button("btn").setText("go"),
                ui::textField("field")),
        tree,
        *host);

    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->_typeId, kTypeIdContainer);
    EXPECT_EQ(findChildByKey(root.get(), "row")->_typeId, kTypeIdContainer);
    EXPECT_EQ(findChildByKey(root.get(), "panel")->_typeId, kTypeIdPanel);
    EXPECT_EQ(findChildByKey(root.get(), "label")->_typeId, kTypeIdText);
    EXPECT_EQ(findChildByKey(root.get(), "btn")->_typeId, kTypeIdButton);
    EXPECT_EQ(findChildByKey(root.get(), "field")->_typeId, kTypeIdTextField);
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
    WidgetTree noTheme({.width = 320, .height = 200});
    auto fallback = std::make_shared<UIPanel>("Fallback");
    fallback->setSize({120.0f, 60.0f});
    ASSERT_TRUE(noTheme.attachToLayer(WidgetTree::ELayer::Content, fallback).valid());
    const auto fallbackSnapshot = noTheme.buildSnapshot({});
    ASSERT_EQ(fallbackSnapshot.items.size(), 1u);

    WidgetTree authored({.width = 320, .height = 200});
    auto authoredPanel = std::make_shared<UIPanel>("Authored");
    authoredPanel->setSize({120.0f, 60.0f});
    authoredPanel->setColor({0.1f, 0.2f, 0.3f, 1.0f});
    ASSERT_TRUE(authored.attachToLayer(WidgetTree::ELayer::Content, authoredPanel).valid());
    const auto authoredSnapshot = authored.buildSnapshot({});
    ASSERT_EQ(authoredSnapshot.items.size(), 1u);
    EXPECT_EQ(authoredSnapshot.items.front().color, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));

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

TEST(DeclarativeContractTest, DirectConstructSnapshotIsStableAcrossRepeatedBuilds)
{
    WidgetTree tree({.width = 320, .height = 200});
    (void)ui::build(
        ui::column("root")
            .setSize({200.0f, 100.0f})
            .children(
                ui::panel("panel").setSize({120.0f, 60.0f}).setColor({0.3f, 0.4f, 0.5f, 1.0f}),
                ui::text("label").setText("stable")),
        tree,
        *tree.getLayer(WidgetTree::ELayer::Content));

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

TEST(DeclarativeContractTest, DirectConstructMutatesEnabledAndFocusPolicyInPlace)
{
    WidgetTree tree({.width = 640, .height = 360});
    const UIElementRef root = ui::build(
        ui::column("root").child(
            ui::button("action")
                .setText("Action")
                .setEnabled(false)
                .setFocusPolicy(EWidgetFocusPolicy::Focusable)),
        tree,
        *tree.getLayer(WidgetTree::ELayer::Content));

    auto* button = dynamic_cast<UIButton*>(root->getChildren()[0].get());
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isEnabled());
    EXPECT_EQ(button->_focusPolicy, EWidgetFocusPolicy::Focusable);

    button->setEnabled(true);
    button->_focusPolicy = EWidgetFocusPolicy::None;
    EXPECT_EQ(root->getChildren()[0].get(), button);
    EXPECT_TRUE(button->isEnabled());
    EXPECT_EQ(button->_focusPolicy, EWidgetFocusPolicy::None);
}

TEST(DeclarativeContractTest, DirectConstructButtonLabelIsContentChild)
{
    WidgetTree tree({.width = 640, .height = 360});
    const UIElementRef root = ui::build(
        ui::button("action").setText("Save").setSize({120.0f, 30.0f}),
        tree,
        *tree.getLayer(WidgetTree::ELayer::Content));

    auto* button = dynamic_cast<UIButton*>(root.get());
    ASSERT_NE(button, nullptr);
    ASSERT_EQ(button->getChildren().size(), 1u);
    auto* label = dynamic_cast<UIText*>(button->getChildren()[0].get());
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->_stableKey, "action__label");
    EXPECT_EQ(label->getText(), "Save");
}

TEST(DeclarativeContractTest, DirectConstructTextFieldKeepsFocusAcrossSetText)
{
    WidgetTree tree({.width = 640, .height = 360});
    const UIElementRef root = ui::build(
        ui::textField("editor").setText("hello").setSize({180.0f, 28.0f}),
        tree,
        *tree.getLayer(WidgetTree::ELayer::Content));

    auto* field = dynamic_cast<UITextField*>(root.get());
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->_text, "hello");

    tree.setFocus(field);
    ASSERT_EQ(tree.getFocused(), field);

    field->setText("world");
    EXPECT_EQ(root.get(), field);
    EXPECT_EQ(field->_text, "world");
    EXPECT_EQ(tree.getFocused(), field);
}

TEST(DeclarativeContractTest, DirectConstructContainerLayoutHonorsAuthoredSizeAndClip)
{
    WidgetTree treeA({.width = 640, .height = 360});
    const UIElementRef first = ui::build(
        ui::row("root")
            .setSize({100.0f, 20.0f})
            .setSpacing(4.0f)
            .setPadding({2.0f, 3.0f})
            .children(
                ui::panel("left").setSize({20.0f, 10.0f}),
                ui::panel("right").setSize({30.0f, 10.0f})),
        treeA,
        *treeA.getLayer(WidgetTree::ELayer::Content));

    auto* container = dynamic_cast<UIContainer*>(first.get());
    ASSERT_NE(container, nullptr);
    container->setClipChildren(false);
    container->setStretchLastChild(false);
    EXPECT_EQ(container->getBoxLayout().getDirection(), EWidgetBoxLayout::Horizontal);
    EXPECT_FLOAT_EQ(container->getBoxLayout().getSpacing(), 4.0f);
    EXPECT_EQ(container->getBoxLayout().getPadding(), glm::vec2(2.0f, 3.0f));

    (void)treeA.buildSnapshot({});
    auto* left = dynamic_cast<UIPanel*>(first->getChildren()[0].get());
    auto* right = dynamic_cast<UIPanel*>(first->getChildren()[1].get());
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_FLOAT_EQ(left->_layoutRect.pos.x, 2.0f);
    EXPECT_FLOAT_EQ(right->_layoutRect.pos.x, 26.0f);
    EXPECT_EQ(right->_layoutRect.extent.x, 30.0f);

    container->setSpacing(12.0f);
    container->setPadding({8.0f, 9.0f});
    container->setClipChildren(true);
    container->setStretchLastChild(true);
    EXPECT_EQ(first.get(), container);
    (void)treeA.buildSnapshot({});
    EXPECT_FLOAT_EQ(left->_layoutRect.pos.x, 8.0f);
    EXPECT_FLOAT_EQ(right->_layoutRect.pos.x, 40.0f);
    EXPECT_EQ(right->_layoutRect.extent.x, 52.0f);

    WidgetTree clipTree({.width = 640, .height = 360});
    const UIElementRef clipped = ui::build(
        ui::row("clip_root")
            .setSize({50.0f, 20.0f})
            .child(ui::panel("overflow").setSize({80.0f, 10.0f})),
        clipTree,
        *clipTree.getLayer(WidgetTree::ELayer::Content));
    dynamic_cast<UIContainer*>(clipped.get())->setClipChildren(true);

    const UIFrameSnapshot clippedSnapshot = clipTree.buildSnapshot({});
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

TEST(DeclarativeContractTest, DirectConstructAttachesLiveWidgetsWithoutDescription)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    const UIElementRef root = ui::build(
        ui::column("root")
            .children(
                ui::text("title").setText("Hello"),
                ui::button("go").setText("Go")),
        tree,
        *host);

    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->_typeId, kTypeIdContainer);
    EXPECT_EQ(root->_stableKey, "root");
    EXPECT_TRUE(root->_bAutoSize);
    ASSERT_EQ(root->getChildren().size(), 2u);
    EXPECT_EQ(root->getChildren()[0]->_typeId, kTypeIdText);
    EXPECT_EQ(root->getChildren()[1]->_typeId, kTypeIdButton);
    EXPECT_EQ(dynamic_cast<UIText*>(root->getChildren()[0].get())->getText(), "Hello");
    ASSERT_FALSE(root->getChildren()[1]->getChildren().empty());
    EXPECT_EQ(dynamic_cast<UIText*>(root->getChildren()[1]->getChildren()[0].get())->getText(), "Go");
}

TEST(DeclarativeContractTest, DirectConstructBindTextUpdatesWithoutRebuild)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto label = std::make_shared<Reactive<std::string>>("Pressed: 0");

    const UIElementRef root = ui::build(
        ui::column("root").child(ui::text("counter").bindText(label)),
        tree,
        *tree.getLayer(WidgetTree::ELayer::Content));

    auto* text = dynamic_cast<UIText*>(root->getChildren()[0].get());
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->resolvedText(), "Pressed: 0");
    const UIElement* identity = text;

    label->set("Pressed: 1");
    EXPECT_EQ(root->getChildren()[0].get(), identity);
    EXPECT_EQ(text->resolvedText(), "Pressed: 1");
}

TEST(DeclarativeContractTest, DirectConstructInputWidgetsCarryRegistryTypeId)
{
    WidgetTree tree({.width = 320, .height = 200});
    const UIElementRef root = ui::build(
        ui::column("root")
            .children(
                ui::checkBox("c").setChecked(true).setText("on"),
                ui::slider("s").setValue(0.4f).setSize({100.0f, 22.0f}),
                ui::comboBox("cb").setItems({"A", "B"}).setSelectedIndex(1).setSize({80.0f, 26.0f}),
                ui::image("img").setAssetPath("builtin/checkerboard").setSize({16.0f, 16.0f})),
        tree,
        *tree.getLayer(WidgetTree::ELayer::Content));

    ASSERT_EQ(root->getChildren().size(), 4u);
    auto* check = dynamic_cast<UICheckBox*>(root->getChildren()[0].get());
    auto* slider = dynamic_cast<UISlider*>(root->getChildren()[1].get());
    auto* combo = dynamic_cast<UIComboBox*>(root->getChildren()[2].get());
    auto* image = dynamic_cast<UIImage*>(root->getChildren()[3].get());
    ASSERT_NE(check, nullptr);
    ASSERT_NE(slider, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(check->_typeId, kTypeIdCheckBox);
    EXPECT_TRUE(check->_bChecked);
    EXPECT_EQ(slider->_typeId, kTypeIdSlider);
    EXPECT_FLOAT_EQ(slider->_value, 0.4f);
    EXPECT_EQ(combo->_typeId, kTypeIdComboBox);
    EXPECT_EQ(combo->_selectedIndex, 1);
    EXPECT_EQ(combo->currentLabel(), "B");
    EXPECT_EQ(image->_typeId, kTypeIdImage);
    EXPECT_EQ(image->_assetPath, "builtin/checkerboard");
}

TEST(DeclarativeContractTest, DirectConstructContainerClipAndMainAxisAlignment)
{
    WidgetTree tree({.width = 320, .height = 200});
    const UIElementRef root = ui::build(
        ui::column("root")
            .setSize({80.0f, 100.0f})
            .setClipChildren(true)
            .setMainAxisAlignment(EWidgetMainAxisAlignment::End)
            .child(ui::panel("cell").setSize({20.0f, 10.0f})),
        tree,
        *tree.getLayer(WidgetTree::ELayer::Content));

    auto* container = dynamic_cast<UIContainer*>(root.get());
    ASSERT_NE(container, nullptr);
    EXPECT_TRUE(container->getBoxLayout().clipsChildren());
    EXPECT_EQ(container->getBoxLayout().getMainAxisAlignment(), EWidgetMainAxisAlignment::End);
}

TEST(DeclarativeContractTest, DirectConstructDetachStopsButtonClicks)
{
    WidgetTree tree({.width = 320, .height = 200});
    int clicks = 0;
    const UIElementRef root = ui::build(
        ui::button("go").setText("Go").setSize({80.0f, 32.0f}).setOnClick([&] { ++clicks; }),
        tree,
        *tree.getLayer(WidgetTree::ELayer::Content));

    auto* button = dynamic_cast<UIButton*>(root.get());
    ASSERT_NE(button, nullptr);
    tree.layout();
    WidgetEventContext ctx;
    ctx.logicalPoint = button->_layoutRect.pos + button->_layoutRect.extent * 0.5f;

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), ctx),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), ctx),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);

    tree.detach(*button);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), ctx),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), ctx),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 1);
}

} // namespace ya
