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
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/UIAdapterHost.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UITypeIds.h"
#include "Render/Resources/FontManager.h"

#include "Core/Event.h"
#include <gtest/gtest.h>

namespace ya
{

namespace
{
template<typename TParent, typename TChild, typename TSlot>
concept AcceptsTypedChildSlot = requires(TParent&& parent, TChild&& child, TSlot&& slot) {
    std::move(parent).child(std::move(child), std::move(slot));
};

static_assert(AcceptsTypedChildSlot<ya::ui::UIPanelWidgetBuilder,
                                    ya::ui::UITextWidgetBuilder,
                                    ya::ui::FCanvasSlotBuilder>);
static_assert(!AcceptsTypedChildSlot<ya::ui::UIPanelWidgetBuilder,
                                     ya::ui::UITextWidgetBuilder,
                                     ya::ui::FBoxSlotBuilder>);
static_assert(AcceptsTypedChildSlot<ya::ui::UIContainerWidgetBuilder,
                                    ya::ui::UITextWidgetBuilder,
                                    ya::ui::FBoxSlotBuilder>);
static_assert(!AcceptsTypedChildSlot<ya::ui::UIContainerWidgetBuilder,
                                     ya::ui::UITextWidgetBuilder,
                                     ya::ui::FCanvasSlotBuilder>);

struct FTestCompoundWidget final : UICompoundWidget
{
    explicit FTestCompoundWidget(std::string name) : UICompoundWidget(std::move(name))
    {
        enableTick();
    }

    int constructCount = 0;
    int tickCount = 0;
    float accumulatedDelta = 0.0f;

    void construct() override
    {
        ++constructCount;
        auto panel = ui::panel("compound_root").release();
        addDetachedChild(std::move(panel), [](UIElement&, UISlot& edge) {
            if (auto* overlay = edge.as<UIOverlaySlot>()) {
                overlay->setPreferredSize({123.0f, 45.0f});
            }
        });
    }

    void tick(float deltaSeconds) override
    {
        ++tickCount;
        accumulatedDelta += deltaSeconds;
    }
};

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
    FCanvasSlotArgs panelSlot; panelSlot.fixedSize = {120.0f, 60.0f};
    panel->setColor({0.12f, 0.24f, 0.36f, 1.0f});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot).valid());

    const UIFrameSnapshot snapshot = tree.buildSnapshot({});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items.front().color, glm::vec4(0.12f, 0.24f, 0.36f, 1.0f));
}

TEST(DeclarativeContractTest, DslCreatedWidgetsCarryRegistryTypeId)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    auto page = ui::column("root")
                    .children(
                        ui::row("row"),
                        ui::panel("panel").setColor({0.3f, 0.4f, 0.5f, 1.0f}),
                        ui::text("label").setText("hi"),
                        ui::button("btn").child(ui::text("btn_Label").setText("go")),
                        ui::textField("field"));
    const UIElementRef root = ui::build(tree, *host, std::move(page), ui::canvasSlot().fill());

    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->_typeId, kTypeIdContainer);
    EXPECT_EQ(findChildByKey(root.get(), "row")->_typeId, kTypeIdContainer);
    EXPECT_EQ(findChildByKey(root.get(), "panel")->_typeId, kTypeIdPanel);
    EXPECT_EQ(findChildByKey(root.get(), "label")->_typeId, kTypeIdText);
    EXPECT_EQ(findChildByKey(root.get(), "btn")->_typeId, kTypeIdButton);
    EXPECT_EQ(findChildByKey(root.get(), "field")->_typeId, kTypeIdTextField);
}

TEST(DeclarativeContractTest, DslNodesAreAnonymousUnlessIdentityIsRequested)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    auto page = ui::column()
                    .children(ui::text().setText("anonymous"),
                              ui::text().displayName("Title").setText("named"),
                              ui::panel().key("panel_id"));
    const UIElementRef root = ui::build(tree, *host, std::move(page), ui::canvasSlot().fill());

    ASSERT_NE(root, nullptr);
    EXPECT_TRUE(root->_stableKey.empty());
    EXPECT_EQ(root->_name, "Container");
    ASSERT_EQ(root->getChildren().size(), 3u);
    EXPECT_TRUE(root->getChildren()[0]->_stableKey.empty());
    EXPECT_EQ(root->getChildren()[0]->_name, "Text");
    EXPECT_TRUE(root->getChildren()[1]->_stableKey.empty());
    EXPECT_EQ(root->getChildren()[1]->_name, "Title");
    EXPECT_EQ(root->getChildren()[2]->_stableKey, "panel_id");
    EXPECT_EQ(root->getChildren()[2]->_name, "panel_id");
}

TEST(DeclarativeContractTest, FragmentsAndConstructionConditionalsFlattenIntoTheParent)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    auto page = ui::column()
                    .child(ui::group(ui::text().setText("A"),
                                     ui::when(true, ui::text().setText("B")),
                                     ui::when(false, ui::text().setText("hidden")),
                                     ui::ifElse(false, ui::text().setText("wrong"), ui::text().setText("C"))))
                    .child(ui::unless(true, ui::text().setText("also_hidden")));
    const UIElementRef root = ui::build(tree, *host, std::move(page), ui::canvasSlot().fill());

    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->getChildren().size(), 3u);
    EXPECT_EQ(root->getChildren()[0]->_typeId, kTypeIdText);
    EXPECT_EQ(root->getChildren()[1]->_typeId, kTypeIdText);
    EXPECT_EQ(root->getChildren()[2]->_typeId, kTypeIdText);
    EXPECT_EQ(static_cast<UIText*>(root->getChildren()[0].get())->getText(), "A");
    EXPECT_EQ(static_cast<UIText*>(root->getChildren()[1].get())->getText(), "B");
    EXPECT_EQ(static_cast<UIText*>(root->getChildren()[2].get())->getText(), "C");
}

TEST(DeclarativeContractTest, DuplicateSiblingKeysAreRejected)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    auto page = ui::column("root")
                    .child(ui::text("same").setText("first"))
                    .child(ui::text("same").setText("duplicate"))
                    .child(ui::text().setText("anonymous"));
    const UIElementRef root = ui::build(tree, *host, std::move(page), ui::canvasSlot().fill());

    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->getChildren().size(), 2u);
    EXPECT_EQ(root->getChildren()[0]->_stableKey, "same");
    EXPECT_TRUE(root->getChildren()[1]->_stableKey.empty());
}

TEST(DeclarativeContractTest, PanelChildAndChildrenShareTheSameDefaultCanvasSlot)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    auto childPath = ui::panel("child_path")
                         .child(ui::text("label_a").setText("A"));
    const UIElementRef childRoot = ui::build(tree, *host, std::move(childPath), ui::canvasSlot().fill());

    tree.detach(*childRoot);

    auto childrenPath = ui::panel("children_path")
                            .children(ui::text("label_b").setText("B"));
    const UIElementRef childrenRoot = ui::build(tree, *host, std::move(childrenPath), ui::canvasSlot().fill());

    auto* childPanel = dynamic_cast<UIPanel*>(childRoot.get());
    auto* childrenPanel = dynamic_cast<UIPanel*>(childrenRoot.get());
    ASSERT_NE(childPanel, nullptr);
    ASSERT_NE(childrenPanel, nullptr);
    ASSERT_EQ(childPanel->getChildren().size(), 1u);
    ASSERT_EQ(childrenPanel->getChildren().size(), 1u);

    auto* childSlot = dynamic_cast<UICanvasSlot*>(childPanel->getSlotForChild(*childPanel->getChildren()[0]));
    auto* childrenSlot = dynamic_cast<UICanvasSlot*>(childrenPanel->getSlotForChild(*childrenPanel->getChildren()[0]));
    ASSERT_NE(childSlot, nullptr);
    ASSERT_NE(childrenSlot, nullptr);

    EXPECT_EQ(childSlot->getAnchorMin(), childrenSlot->getAnchorMin());
    EXPECT_EQ(childSlot->getAnchorMax(), childrenSlot->getAnchorMax());
    EXPECT_EQ(childSlot->getWidthSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(childSlot->getHeightSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(childrenSlot->getWidthSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(childrenSlot->getHeightSizeMode(), EWidgetSizeMode::Auto);
}

TEST(DeclarativeContractTest, ExplicitSlotBuildersApplyTypedParentChildIntent)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    auto page = ui::panel("root")
                    .child(ui::text("canvas_child").setText("A"),
                           ui::canvasSlot().anchor({0.25f, 0.0f}, {0.75f, 1.0f})
                               .insets(FMargin::all(4.0f))
                               .preferredSize({80.0f, 20.0f}))
                    .child(ui::button("overlay_child"),
                           ui::canvasSlot().size({90.0f, 30.0f}));
    const UIElementRef root = ui::build(tree, *host, std::move(page), ui::canvasSlot().fill());

    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->getChildren().size(), 2u);
    auto* firstSlot = root->getSlotForChild(*root->getChildren()[0]);
    auto* secondSlot = root->getSlotForChild(*root->getChildren()[1]);
    ASSERT_NE(firstSlot, nullptr);
    ASSERT_NE(secondSlot, nullptr);
    auto* firstCanvas = firstSlot->as<UICanvasSlot>();
    auto* secondCanvas = secondSlot->as<UICanvasSlot>();
    ASSERT_NE(firstCanvas, nullptr);
    ASSERT_NE(secondCanvas, nullptr);
    EXPECT_EQ(firstCanvas->getAnchorMin(), glm::vec2(0.25f, 0.0f));
    EXPECT_EQ(firstCanvas->getAnchorMax(), glm::vec2(0.75f, 1.0f));
    EXPECT_EQ(firstCanvas->getOffsets(), FMargin::all(4.0f));
    EXPECT_EQ(firstCanvas->getPreferredSize(), glm::vec2(80.0f, 20.0f));
    EXPECT_EQ(secondCanvas->getFixedSize(), glm::vec2(90.0f, 30.0f));
    EXPECT_EQ(secondCanvas->getWidthSizeMode(), EWidgetSizeMode::Fixed);

    auto single = ui::button("single")
                      .child(ui::text("label").setText("B"),
                             ui::overlaySlot().align(EUIOverlayAlignment::Center, EUIOverlayAlignment::End)
                                 .inset(glm::vec2(3.0f)));
    const UIElementRef singleRoot = ui::build(tree, *host, std::move(single), ui::canvasSlot().size({120.0f, 40.0f}));
    ASSERT_NE(singleRoot, nullptr);
    ASSERT_EQ(singleRoot->getChildren().size(), 1u);
    auto* overlaySlot = singleRoot->getSlotForChild(*singleRoot->getChildren()[0]);
    ASSERT_NE(overlaySlot, nullptr);
    auto* typedOverlay = overlaySlot->as<UIOverlaySlot>();
    ASSERT_NE(typedOverlay, nullptr);
    EXPECT_EQ(typedOverlay->getHAlign(), EUIOverlayAlignment::Center);
    EXPECT_EQ(typedOverlay->getVAlign(), EUIOverlayAlignment::End);
    EXPECT_EQ(typedOverlay->getPadding(), FMargin::hv({3.0f, 3.0f}));

    auto box = ui::column("box")
                   .child(ui::text("box_label").setText("C"),
                          ui::boxSlot().fill(2.0f).margin(FMargin::all(5.0f)).preferredSize({70.0f, 18.0f}));
    const UIElementRef boxRoot = ui::build(tree, *host, std::move(box), ui::canvasSlot().size({140.0f, 80.0f}));
    ASSERT_NE(boxRoot, nullptr);
    ASSERT_EQ(boxRoot->getChildren().size(), 1u);
    auto* boxSlot = boxRoot->getSlotForChild(*boxRoot->getChildren()[0]);
    ASSERT_NE(boxSlot, nullptr);
    auto* typedBox = boxSlot->as<UIBoxSlot>();
    ASSERT_NE(typedBox, nullptr);
    EXPECT_EQ(typedBox->getSizeRule(), EUIBoxSlotSizeRule::Fill);
    EXPECT_FLOAT_EQ(typedBox->getWeight(), 2.0f);
    EXPECT_EQ(typedBox->getMargin(), FMargin::all(5.0f));
    EXPECT_EQ(typedBox->getPreferredSize(), glm::vec2(70.0f, 18.0f));
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
    FCanvasSlotArgs panelSlot; panelSlot.fixedSize = {120.0f, 60.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot).valid());

    (void)tree.buildSnapshot({});
    (void)tree.buildSnapshot({});
    const GuiPerfStats before = tree.getPerfStats();

    auto* content = tree.getLayer(WidgetTree::ELayer::Content);
    auto* slot = dynamic_cast<UICanvasSlot*>(content->getSlotForChild(*panel));
    ASSERT_NE(slot, nullptr);
    const glm::vec2 authoredSize = slot->getFixedSize();
    slot->setFixedSize(authoredSize);
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
    FCanvasSlotArgs fallbackSlot; fallbackSlot.fixedSize = {120.0f, 60.0f};
    ASSERT_TRUE(noTheme.attach(*noTheme.getLayer(WidgetTree::ELayer::Content), fallback, fallbackSlot).valid());
    const auto fallbackSnapshot = noTheme.buildSnapshot({});
    ASSERT_EQ(fallbackSnapshot.items.size(), 1u);

    WidgetTree authored({.width = 320, .height = 200});
    auto authoredPanel = std::make_shared<UIPanel>("Authored");
    FCanvasSlotArgs authoredSlot; authoredSlot.fixedSize = {120.0f, 60.0f};
    authoredPanel->setColor({0.1f, 0.2f, 0.3f, 1.0f});
    ASSERT_TRUE(authored.attach(*authored.getLayer(WidgetTree::ELayer::Content), authoredPanel, authoredSlot).valid());
    const auto authoredSnapshot = authored.buildSnapshot({});
    ASSERT_EQ(authoredSnapshot.items.size(), 1u);
    EXPECT_EQ(authoredSnapshot.items.front().color, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));

    WidgetTree themed({.width = 320, .height = 200});
    auto theme = std::make_shared<UITheme>();
    FPanelStyle panelStyle;
    panelStyle.fillColor = FBrush::solid({0.7f, 0.1f, 0.2f, 1.0f});
    theme->define<FPanelStyle>("panel", panelStyle);
    themed.setTheme(theme.get());
    auto themedPanel = std::make_shared<UIPanel>("Themed");
    FCanvasSlotArgs themedSlot; themedSlot.fixedSize = {120.0f, 60.0f};
    ASSERT_TRUE(themed.attach(*themed.getLayer(WidgetTree::ELayer::Content), themedPanel, themedSlot).valid());
    const auto themedSnapshot = themed.buildSnapshot({});
    ASSERT_EQ(themedSnapshot.items.size(), 1u);
    EXPECT_EQ(themedSnapshot.items.front().color, glm::vec4(0.7f, 0.1f, 0.2f, 1.0f));
}

TEST(DeclarativeContractTest, DslSetStyleOverridesTheme)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto       theme = std::make_shared<UITheme>();
    FButtonStyle themed;
    themed.normalFill = FBrush::solid({0.1f, 0.2f, 0.9f, 1.0f});
    theme->define<FButtonStyle>("button", themed);
    tree.setTheme(theme.get());

    FButtonStyle authored;
    authored.normalFill = FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f});
    auto page = ui::button("go").setStyle(authored);
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {80.0f, 32.0f};
    (void)ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    const UIFrameSnapshot snap = tree.buildSnapshot({});
    ASSERT_FALSE(snap.items.empty());
    EXPECT_EQ(snap.items.front().color, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
}

TEST(DeclarativeContractTest, DslSetStyleFieldInheritsUnpatchedThemeFields)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto       theme = std::make_shared<UITheme>();
    FButtonStyle themed;
    themed.normalFill  = FBrush::solid({0.1f, 0.2f, 0.9f, 1.0f});
    themed.hoveredFill = FBrush::solid({0.2f, 0.9f, 0.2f, 1.0f});
    theme->define<FButtonStyle>("button", themed);
    tree.setTheme(theme.get());

    auto page = ui::button("go").setStyleField("normalFill", FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f}));
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {80.0f, 32.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);
    auto* button = dynamic_cast<UIButton*>(root.get());
    ASSERT_NE(button, nullptr);

    const UIFrameSnapshot snap = tree.buildSnapshot({});
    ASSERT_FALSE(snap.items.empty());
    EXPECT_EQ(snap.items.front().color, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
    const FButtonStyle resolved = resolveWidgetStyle<FButtonStyle>(*button, button->_authoredStyle);
    EXPECT_EQ(resolved.normalFill, FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f}));
    EXPECT_EQ(resolved.hoveredFill, FBrush::solid({0.2f, 0.9f, 0.2f, 1.0f}));
}

TEST(DeclarativeContractTest, SnapshotDoesNotDependOnLiveWidgetAfterDetach)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto panel = std::make_shared<UIPanel>("SnapshotPanel");
    FCanvasSlotArgs panelSlot; panelSlot.fixedSize = {120.0f, 60.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot).valid());

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
    auto page = ui::column("root")
                    .children(
                        ui::panel("panel").setColor({0.3f, 0.4f, 0.5f, 1.0f}),
                        ui::text("label").setText("stable"));
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {200.0f, 100.0f};
    (void)ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

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
    auto page = ui::column("root").child(
        ui::button("action")
            .child(ui::text("action_Label").setText("Action"))
            .setEnabled(false)
            .setFocusPolicy(EWidgetFocusPolicy::Focusable));
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), ui::canvasSlot().fill());

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
    auto page = ui::button("action")
                    .child(ui::text("action_Label").setText("Save"));
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {120.0f, 30.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    auto* button = dynamic_cast<UIButton*>(root.get());
    ASSERT_NE(button, nullptr);
    ASSERT_EQ(button->getChildren().size(), 1u);
    auto* label = dynamic_cast<UIText*>(button->getChildren()[0].get());
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->_stableKey, "action_Label");
    EXPECT_EQ(label->getText(), "Save");
}

TEST(DeclarativeContractTest, DirectConstructTextFieldKeepsFocusAcrossSetText)
{
    WidgetTree tree({.width = 640, .height = 360});
    auto page = ui::textField("editor").setText("hello");
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {180.0f, 28.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

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

TEST(DeclarativeContractTest, DirectConstructExternalPatchClampsFocusedTextFieldCursorWithoutCommit)
{
    WidgetTree tree({.width = 640, .height = 360});
    int commits = 0;
    auto page = ui::textField("editor")
                    .setText("hello world")
                    .setOnCommit([&](const std::string&) { ++commits; });
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {180.0f, 28.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    auto* field = dynamic_cast<UITextField*>(root.get());
    ASSERT_NE(field, nullptr);
    tree.setFocus(field);
    ASSERT_EQ(tree.getFocused(), field);

    KeyPressedEvent endEvent;
    endEvent._keyCode = EKey::End;
    WidgetEventContext ctx;
    ctx.logicalPoint = {0.0f, 0.0f};
    EXPECT_EQ(tree.dispatchEvent(endEvent, ctx), EWidgetRouteResult::HandledExclusive);
    ASSERT_EQ(field->getCursorIndex(), std::string("hello world").size());

    field->setText("hi");
    EXPECT_EQ(root.get(), field);
    EXPECT_EQ(tree.getFocused(), field);
    EXPECT_EQ(commits, 0);
    EXPECT_EQ(field->_text, "hi");
    EXPECT_EQ(field->getCursorIndex(), field->_text.size());
}

TEST(DeclarativeContractTest, AdapterHostPatchClampsFocusedTextFieldCursorWithoutDsl)
{
    WidgetTree tree({.width = 640, .height = 360});
    UIAdapterHost adapterHost(tree, *tree.getLayer(WidgetTree::ELayer::Content));

    auto fieldRef = std::make_shared<UITextField>("AdapterField");
    fieldRef->setText("hello world");
    FCanvasSlotArgs fieldSlot; fieldSlot.fixedSize = {180.0f, 28.0f};

    int commits = 0;
    fieldRef->_onCommit = [&](const std::string&) { ++commits; };

    auto* field = dynamic_cast<UITextField*>(&adapterHost.mount(fieldRef));
    ASSERT_NE(field, nullptr);
    if (auto* slot = adapterHost.getParent().getSlotForChild(*field)) {
        if (auto* canvas = slot->as<UICanvasSlot>()) {
            canvas->apply(fieldSlot);
        }
    }

    tree.setFocus(field);
    ASSERT_EQ(tree.getFocused(), field);

    KeyPressedEvent endEvent;
    endEvent._keyCode = EKey::End;
    WidgetEventContext ctx;
    ctx.logicalPoint = {0.0f, 0.0f};
    EXPECT_EQ(tree.dispatchEvent(endEvent, ctx), EWidgetRouteResult::HandledExclusive);
    ASSERT_EQ(field->getCursorIndex(), std::string("hello world").size());

    adapterHost.patch([](UIElement& root) {
        auto& textField = static_cast<UITextField&>(root);
        textField.setText("hi");
    });

    EXPECT_EQ(adapterHost.getRoot(), field);
    EXPECT_EQ(tree.getFocused(), field);
    EXPECT_EQ(commits, 0);
    EXPECT_EQ(field->_text, "hi");
    EXPECT_EQ(field->getCursorIndex(), field->_text.size());

    adapterHost.unmount();
    EXPECT_EQ(adapterHost.getRoot(), nullptr);
    EXPECT_EQ(tree.getFocused(), nullptr);
}

TEST(DeclarativeContractTest, DirectConstructExternalPatchKeepsPressedButtonSession)
{
    WidgetTree tree({.width = 640, .height = 360});
    int clicks = 0;
    auto page = ui::button("action")
                    .child(ui::text("action_Label").setText("Save"))
                    .setOnClick([&] { ++clicks; });
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {120.0f, 32.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    auto* button = dynamic_cast<UIButton*>(root.get());
    ASSERT_NE(button, nullptr);
    ASSERT_EQ(button->getChildren().size(), 1u);
    auto* label = dynamic_cast<UIText*>(button->getChildren()[0].get());
    ASSERT_NE(label, nullptr);
    const UIElement* buttonIdentity = button;
    const UIElement* labelIdentity = label;

    tree.layout();
    WidgetEventContext ctx;
    ctx.logicalPoint = button->_layoutRect.pos + button->_layoutRect.extent * 0.5f;

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), ctx),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(button->_bPressed.get());
    ASSERT_EQ(tree.getPointerCapture(), button);

    label->setText("Patched While Pressed");
    EXPECT_EQ(root.get(), buttonIdentity);
    EXPECT_EQ(button->getChildren()[0].get(), labelIdentity);
    EXPECT_TRUE(button->_bPressed.get());
    EXPECT_EQ(tree.getPointerCapture(), button);
    EXPECT_EQ(label->getText(), "Patched While Pressed");

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), ctx),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(button->_bPressed.get());
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_EQ(clicks, 1);
}

TEST(DeclarativeContractTest, DirectConstructExternalPatchCanReplaceButtonLabelSubtreeMidPress)
{
    WidgetTree tree({.width = 640, .height = 360});
    int clicks = 0;
    auto page = ui::button("action")
                    .child(ui::text("action_Label").setText("Save"))
                    .setOnClick([&] { ++clicks; });
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {120.0f, 32.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    auto* button = dynamic_cast<UIButton*>(root.get());
    ASSERT_NE(button, nullptr);
    ASSERT_EQ(button->getChildren().size(), 1u);
    auto* oldLabel = dynamic_cast<UIText*>(button->getChildren()[0].get());
    ASSERT_NE(oldLabel, nullptr);
    const UIElement* buttonIdentity = button;

    tree.layout();
    WidgetEventContext ctx;
    ctx.logicalPoint = button->_layoutRect.pos + button->_layoutRect.extent * 0.5f;

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), ctx),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(button->_bPressed.get());
    ASSERT_EQ(tree.getPointerCapture(), button);

    tree.detach(*oldLabel);
    auto newLabel = std::make_shared<UIText>("action_Label_Patched");
    newLabel->setText("Patched Child");
    ASSERT_TRUE(tree.attach(*button, newLabel).valid());

    EXPECT_EQ(root.get(), buttonIdentity);
    ASSERT_EQ(button->getChildren().size(), 1u);
    auto* patchedLabel = dynamic_cast<UIText*>(button->getChildren()[0].get());
    ASSERT_NE(patchedLabel, nullptr);
    EXPECT_EQ(patchedLabel->getText(), "Patched Child");
    EXPECT_TRUE(button->_bPressed.get());
    EXPECT_EQ(tree.getPointerCapture(), button);

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), ctx),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(button->_bPressed.get());
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_EQ(clicks, 1);
}

TEST(DeclarativeContractTest, DirectConstructExternalPatchOnlyChangesFallbackUnderBinding)
{
    WidgetTree tree({.width = 640, .height = 360});
    auto label = std::make_shared<Reactive<std::string>>("Bound");
    auto page = ui::column("root").child(ui::text("caption").setText("Fallback").bindText(label));
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), ui::canvasSlot().fill());

    auto* text = dynamic_cast<UIText*>(root->getChildren()[0].get());
    ASSERT_NE(text, nullptr);
    const UIElement* identity = text;

    EXPECT_EQ(text->getText(), "Fallback");
    EXPECT_EQ(text->resolvedText(), "Bound");

    text->setText("Patched Fallback");
    EXPECT_EQ(root->getChildren()[0].get(), identity);
    EXPECT_EQ(text->getText(), "Patched Fallback");
    EXPECT_EQ(text->resolvedText(), "Bound");

    label->set("Bound Next");
    EXPECT_EQ(text->resolvedText(), "Bound Next");

    text->bindText(nullptr);
    EXPECT_EQ(text->resolvedText(), "Patched Fallback");
}

TEST(DeclarativeContractTest, DirectConstructContainerLayoutHonorsAuthoredSizeAndClip)
{
    WidgetTree treeA({.width = 640, .height = 360});
    auto page = ui::row("root")
                    .setSpacing(4.0f)
                    .setPadding({2.0f, 3.0f})
                    .child(ui::panel("left"), ui::boxSlot().preferredSize({20.0f, 10.0f}))
                    .child(ui::panel("right"), ui::boxSlot().preferredSize({30.0f, 10.0f}));
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {100.0f, 20.0f};
    const UIElementRef first = ui::build(treeA, *treeA.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

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
    auto clipPage = ui::row("clip_root")
                        .child(ui::panel("overflow"), ui::boxSlot().preferredSize({80.0f, 10.0f}));
    FCanvasSlotArgs clipRootSlot;
    clipRootSlot.fixedSize = {50.0f, 20.0f};
    const UIElementRef clipped =
        ui::build(clipTree, *clipTree.getLayer(WidgetTree::ELayer::Content), std::move(clipPage), clipRootSlot);
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
    FCanvasSlotArgs buttonSlot; buttonSlot.fixedSize = {120.0f, 60.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot).valid());
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

    auto page = ui::column("root")
                    .children(
                        ui::text("title").setText("Hello"),
                        ui::button("go").child(ui::text("go_Label").setText("Go")));
    const UIElementRef root = ui::build(tree, *host, std::move(page));

    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->_typeId, kTypeIdContainer);
    EXPECT_EQ(root->_stableKey, "root");
    // Builder defaults do not write widget-owned geometry; if the host is a
    // canvas and the caller omitted a root spec, build() now exposes the
    // mistake by promoting the otherwise-invisible default edge to Auto/Auto
    // instead of leaving the root at Fixed 0x0.
    const auto* rootSlot = dynamic_cast<const UICanvasSlot*>(root->getSlot());
    ASSERT_NE(rootSlot, nullptr);
    EXPECT_EQ(rootSlot->getWidthSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(rootSlot->getHeightSizeMode(), EWidgetSizeMode::Auto);

    ASSERT_EQ(root->getChildren().size(), 2u);
    EXPECT_EQ(root->getChildren()[0]->_typeId, kTypeIdText);
    EXPECT_EQ(root->getChildren()[1]->_typeId, kTypeIdButton);
    EXPECT_EQ(dynamic_cast<UIText*>(root->getChildren()[0].get())->getText(), "Hello");
    ASSERT_FALSE(root->getChildren()[1]->getChildren().empty());
    EXPECT_EQ(dynamic_cast<UIText*>(root->getChildren()[1]->getChildren()[0].get())->getText(), "Go");
}

TEST(DeclarativeContractTest, BuilderAutoSizeIsStoredOnParentCanvasSlot)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    FCanvasSlotArgs autoSlot;
    autoSlot.widthSizeMode  = EWidgetSizeMode::Auto;
    autoSlot.heightSizeMode = EWidgetSizeMode::Auto;
    const UIElementRef label = ui::build(tree, *host, ui::text("auto").setText("Hello"), autoSlot);
    ASSERT_NE(label, nullptr);

    const auto* slot = dynamic_cast<const UICanvasSlot*>(label->getSlot());
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getWidthSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(slot->getHeightSizeMode(), EWidgetSizeMode::Auto);
}

TEST(DeclarativeContractTest, BuildWithoutRootSpecOnCanvasHostFallsBackToVisibleAutoSlot)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    auto page = ui::column("root").child(ui::panel("box"), ui::boxSlot().preferredSize({48.0f, 18.0f}));
    const UIElementRef root = ui::build(tree, *host, std::move(page));
    ASSERT_NE(root, nullptr);

    const auto* slot = dynamic_cast<const UICanvasSlot*>(root->getSlot());
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getWidthSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(slot->getHeightSizeMode(), EWidgetSizeMode::Auto);

    tree.layout();
    EXPECT_GT(root->getLayoutRect().extent.x, 0.0f);
    EXPECT_GT(root->getLayoutRect().extent.y, 0.0f);
}

TEST(DeclarativeContractTest, CompoundWidgetConstructsOnceAndTicksOnlyWhileAttached)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto compound = std::make_shared<FTestCompoundWidget>("compound");

    EXPECT_EQ(compound->constructCount, 0);
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), compound).valid());
    EXPECT_EQ(compound->constructCount, 1);
    ASSERT_EQ(compound->getChildren().size(), 1u);

    tree.tick(0.25f);
    tree.tick(0.5f);
    EXPECT_EQ(compound->tickCount, 2);
    EXPECT_FLOAT_EQ(compound->accumulatedDelta, 0.75f);

    tree.detach(*compound);
    tree.tick(1.0f);
    EXPECT_EQ(compound->tickCount, 2);

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), compound).valid());
    EXPECT_EQ(compound->constructCount, 1);
    tree.tick(1.0f);
    EXPECT_EQ(compound->tickCount, 3);
}

TEST(DeclarativeContractTest, AddDetachedChildOntoAttachedHostConstructsCompound)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto host = std::make_shared<UIPanel>("Host");
    FCanvasSlotArgs hostSlot;
    hostSlot.anchorMin = {0.0f, 0.0f};
    hostSlot.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), host, hostSlot).valid());

    auto compound = std::make_shared<FTestCompoundWidget>("grafted");
    EXPECT_EQ(compound->constructCount, 0);
    host->addDetachedChild(compound);
    EXPECT_EQ(compound->constructCount, 1);
    EXPECT_TRUE(compound->isAttached());
    ASSERT_EQ(compound->getChildren().size(), 1u);
    tree.tick(0.25f);
    EXPECT_EQ(compound->tickCount, 1);
}

TEST(DeclarativeContractTest, CompoundWidgetBuilderBuildsTypedLiveWidget)
{
    WidgetTree tree({.width = 320, .height = 200});
    UIElement* host = tree.getLayer(WidgetTree::ELayer::Content);

    auto builder = ui::compound<FTestCompoundWidget>("compound_builder");
    auto ref = builder.share();
    UIElementRef root = ui::build(tree, *host, std::move(builder), ui::canvasSlot().fill());

    ASSERT_NE(root, nullptr);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(root.get(), ref.get());
    EXPECT_EQ(root->_stableKey, "compound_builder");
    EXPECT_EQ(root->_name, "compound_builder");
    EXPECT_EQ(ref->constructCount, 1);
}

TEST(DeclarativeContractTest, CompoundWidgetForwardsDesiredSizeAndLayoutToCompositionRoot)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto compound = std::make_shared<FTestCompoundWidget>("compound_layout");

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), compound).valid());
    ASSERT_EQ(compound->getChildren().size(), 1u);

    EXPECT_EQ(compound->computeDesiredSize(), glm::vec2(123.0f, 45.0f));

    tree.layout();
    const UIElement* contentRoot = compound->getChildren().front().get();
    ASSERT_NE(contentRoot, nullptr);
    const auto* slot = dynamic_cast<const UIOverlaySlot*>(compound->getSlotForChild(*contentRoot));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(slot->getVAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(contentRoot->_layoutRect.pos, compound->_layoutRect.pos);
    EXPECT_EQ(contentRoot->_layoutRect.extent, compound->_layoutRect.extent);
}

TEST(DeclarativeContractTest, DirectConstructBindTextUpdatesWithoutRebuild)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto label = std::make_shared<Reactive<std::string>>("Pressed: 0");

    auto page = ui::column("root").child(ui::text("counter").bindText(label));
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), ui::canvasSlot().fill());

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
    auto page = ui::column("root")
                    .children(
                        ui::checkBox("c").setChecked(true).setText("on"),
                        ui::slider("s").setValue(0.4f),
                        ui::comboBox("cb").setItems({"A", "B"}).setSelectedIndex(1),
                        ui::image("img").setAssetPath("builtin/checkerboard"));
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), ui::canvasSlot().fill());

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
    auto page = ui::column("root")
                    .setClipChildren(true)
                    .setMainAxisAlignment(EWidgetMainAxisAlignment::End)
                    .child(ui::panel("cell"), ui::boxSlot().preferredSize({20.0f, 10.0f}));
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {80.0f, 100.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    auto* container = dynamic_cast<UIContainer*>(root.get());
    ASSERT_NE(container, nullptr);
    EXPECT_TRUE(container->getBoxLayout().clipsChildren());
    EXPECT_EQ(container->getBoxLayout().getMainAxisAlignment(), EWidgetMainAxisAlignment::End);
}

TEST(DeclarativeContractTest, DirectConstructPanelCornerRadiusAndAnchors)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto page = ui::panel("card")
                    .setColor({0.2f, 0.3f, 0.4f, 1.0f})
                    .setCornerRadius(8.0f)
                    .setStyleKey("panel.canvas")
                    .child(ui::text("caption").setText("r=8"),
                           ui::canvasSlot().anchor({0.1f, 0.2f}, {0.9f, 0.8f}));
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {80.0f, 40.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    auto* panel = dynamic_cast<UIPanel*>(root.get());
    ASSERT_NE(panel, nullptr);
    EXPECT_FLOAT_EQ(panel->getCornerRadius(), 8.0f);
    EXPECT_EQ(panel->_styleKey, "panel.canvas");
    ASSERT_EQ(panel->getChildren().size(), 1u);
    // Anchor intent lives on the parent->child slot edge, not on the child.
    const auto* slot = dynamic_cast<const UICanvasSlot*>(panel->getSlotForChild(*panel->getChildren()[0]));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getAnchorMin(), glm::vec2(0.1f, 0.2f));
    EXPECT_EQ(slot->getAnchorMax(), glm::vec2(0.9f, 0.8f));
}

TEST(DeclarativeContractTest, DirectConstructSplitScrollAndFillSlot)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto split = ui::splitPane("split")
                     .setSplitRatio(0.4f)
                     .setPadding({0.0f, 8.0f})
                     .children(
                         ui::scroll("scroll").child(ui::panel("content"), ui::overlaySlot().preferredSize({20.0f, 40.0f})),
                         ui::panel("right"));
    auto page = ui::column("root").child(ui::text("title").setText("h"));
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {200.0f, 120.0f};
    page.child(std::move(split), ui::boxSlot().fill());
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    auto* column = dynamic_cast<UIContainer*>(root.get());
    ASSERT_NE(column, nullptr);
    ASSERT_EQ(column->getChildren().size(), 2u);
    auto* pane = dynamic_cast<UISplitPane*>(column->getChildren()[1].get());
    ASSERT_NE(pane, nullptr);
    EXPECT_EQ(pane->_typeId, kTypeIdSplitPane);
    EXPECT_FLOAT_EQ(pane->getSplitRatio(), 0.4f);
    EXPECT_EQ(pane->getSplitLayout().getPadding(), glm::vec2(0.0f, 8.0f));
    auto* slot = column->getBoxSlot(*pane);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getSizeRule(), EUIBoxSlotSizeRule::Fill);
    ASSERT_EQ(pane->getChildren().size(), 2u);
    EXPECT_EQ(pane->getChildren()[0]->_typeId, kTypeIdScrollViewport);
}

TEST(DeclarativeContractTest, DirectConstructBoxSlotOverlayAndSizeBox)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto stack = ui::overlay("stack")
                     .child(ui::panel("bg"), ui::overlaySlot().preferredSize({8.0f, 8.0f}))
                     .child(ui::panel("badge"),
                            FOverlaySlotArgs{
                                .hAlign  = EUIOverlayAlignment::End,
                                .vAlign  = EUIOverlayAlignment::Start,
                                .padding = FMargin::all(2.0f),
                                .preferredSize = {6.0f, 6.0f},
                            });
    auto page = ui::column("root");
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {200.0f, 80.0f};
    page.child(ui::panel("fixed"),
               FBoxSlotArgs{
                   .sizeRule = EUIBoxSlotSizeRule::Auto,
                   .weight   = 1.0f,
                   .margin   = FMargin::hv(4.0f, 0.0f),
                   .preferredSize = {20.0f, 10.0f},
               });
    page.child(std::move(stack), FBoxSlotArgs{.sizeRule = EUIBoxSlotSizeRule::Fill});
    page.child(ui::sizeBox("pad")
                   .setPadding(FMargin::all(3.0f))
                   .setWidth(30.0f)
                   .child(ui::panel("inner"), ui::overlaySlot().preferredSize({4.0f, 4.0f})));
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

    auto* column = dynamic_cast<UIContainer*>(root.get());
    ASSERT_NE(column, nullptr);
    ASSERT_EQ(column->getChildren().size(), 3u);
    auto* fixedSlot = column->getBoxSlot(*column->getChildren()[0]);
    ASSERT_NE(fixedSlot, nullptr);
    EXPECT_EQ(fixedSlot->getMargin(), FMargin::hv(4.0f, 0.0f));

    auto* overlay = dynamic_cast<UIOverlay*>(column->getChildren()[1].get());
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->_typeId, kTypeIdOverlay);
    ASSERT_EQ(overlay->getChildren().size(), 2u);
    auto* badgeSlot = overlay->getOverlaySlot(*overlay->getChildren()[1]);
    ASSERT_NE(badgeSlot, nullptr);
    EXPECT_EQ(badgeSlot->getHAlign(), EUIOverlayAlignment::End);

    auto* sizeBox = dynamic_cast<UISizeBox*>(column->getChildren()[2].get());
    ASSERT_NE(sizeBox, nullptr);
    EXPECT_EQ(sizeBox->_typeId, kTypeIdSizeBox);
    EXPECT_FLOAT_EQ(sizeBox->getWidthOverride(), 30.0f);
    EXPECT_EQ(sizeBox->getPadding(), FMargin::all(3.0f));
}

TEST(DeclarativeContractTest, DirectConstructTooltipAndWrap)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto page = ui::column("root").children(
        ui::button("tip")
            .setTooltip("hello")
            .child(ui::text("tip_Label").setText("T")),
        ui::text("wrap").setText("long wrap").setWrap(true).setMaxWrapWidth(120.0f));
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), ui::canvasSlot().fill());

    auto* column = dynamic_cast<UIContainer*>(root.get());
    ASSERT_NE(column, nullptr);
    ASSERT_EQ(column->getChildren().size(), 2u);
    EXPECT_EQ(column->getChildren()[0]->getTooltip(), "hello");
    auto* wrap = dynamic_cast<UIText*>(column->getChildren()[1].get());
    ASSERT_NE(wrap, nullptr);
    EXPECT_TRUE(wrap->_bWrap);
    EXPECT_FLOAT_EQ(wrap->_maxWrapWidth, 120.0f);
}

TEST(DeclarativeContractTest, DirectConstructDetachStopsButtonClicks)
{
    WidgetTree tree({.width = 320, .height = 200});
    int clicks = 0;
    auto page = ui::button("go")
                    .child(ui::text("go_Label").setText("Go"))
                    .setOnClick([&] { ++clicks; });
    FCanvasSlotArgs rootSlot;
    rootSlot.fixedSize = {80.0f, 32.0f};
    const UIElementRef root = ui::build(tree, *tree.getLayer(WidgetTree::ELayer::Content), std::move(page), rootSlot);

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
