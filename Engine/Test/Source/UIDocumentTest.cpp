// Phase 2a regression guards for UIDocument (ui-widget-tree-refactor): the
// UIDocument schema, independent instantiation, JSON roundtrip, and detached
// subtree authoring — all without a Scene or WidgetTree.

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/UIAdapterHost.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Layout/UILayout.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

/// Register the widget types used by these tests (idempotent).
void ensureTestTypesRegistered()
{
    auto& registry = UITypeRegistry::instance();
    if (registry.findType("test.doc_panel")) {
        return;
    }
    registry.registerType({.typeId = "test.doc_panel", .displayName = "Doc Panel"},
                          [] { return std::make_shared<UIPanel>("Panel"); });
    registry.registerType({.typeId = "test.doc_text", .displayName = "Doc Text"},
                          [] { return std::make_shared<UIText>("Text"); });
    registry.registerType({.typeId = "test.doc_button", .displayName = "Doc Button"},
                          [] { return std::make_shared<UIButton>("Button"); });
    registry.registerType({.typeId = "test.doc_container", .displayName = "Doc Container"},
                          [] { return std::make_shared<UIContainer>("Container"); });
    registry.registerType({.typeId = "test.doc_overlay", .displayName = "Doc Overlay"},
                          [] { return std::make_shared<UIOverlay>("Overlay"); });
    registry.registerType({.typeId = "test.doc_size_box", .displayName = "Doc SizeBox"},
                          [] { return std::make_shared<UISizeBox>("SizeBox"); });
    registry.registerType({.typeId = "test.doc_table", .displayName = "Doc Table"},
                          [] { return std::make_shared<UITableGrid>("Table"); });
}

} // namespace

TEST(UIDocumentTest, FromWidgetRoundtripsFieldsAndChildren)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    auto container = registry.createInstance("test.doc_container");
    auto title     = registry.createInstance("test.doc_text");
    auto ok        = registry.createInstance("test.doc_button");
    ASSERT_NE(container, nullptr);
    ASSERT_NE(title, nullptr);
    ASSERT_NE(ok, nullptr);

    auto* titleWidget = dynamic_cast<UIText*>(title.get());
    ASSERT_NE(titleWidget, nullptr);
    titleWidget->setText("Hello Doc");
    titleWidget->_fontSize = 24;
    titleWidget->setColor({1.0f, 0.0f, 0.0f, 1.0f});
    titleWidget->setStyleKey("text.header");
    container->addDetachedChild(title, [](UIElement&, UISlot& edge) {
        if (auto* slot = edge.as<UIBoxSlot>()) {
            slot->setPreferredSize({120.0f, 24.0f});
        }
    });
    container->addDetachedChild(ok, [](UIElement&, UISlot& edge) {
        if (auto* slot = edge.as<UIBoxSlot>()) {
            slot->setPreferredSize({80.0f, 32.0f});
        }
    });

    auto document = UIDocument::fromWidget(*container);
    ASSERT_NE(document, nullptr);
    EXPECT_EQ(document->typeId, "test.doc_container");
    EXPECT_EQ(document->children.size(), 2u);

    // Independent instances: same fields, no shared mutable state.
    UIElementRef instanceA = document->instantiate();
    UIElementRef instanceB = document->instantiate();
    ASSERT_NE(instanceA, nullptr);
    ASSERT_NE(instanceB, nullptr);

    // Root geometry is not part of a UIDocument; it is supplied by the
    // parent-owned edge (SceneWidgetEntry::rootSlot or another child slot).
    EXPECT_FALSE(document->fields["__base__"]["UIElement"].contains("_size"));
    ASSERT_EQ(instanceA->getChildren().size(), 2u);
    ASSERT_EQ(instanceB->getChildren().size(), 2u);

    auto* textA = dynamic_cast<UIText*>(instanceA->getChildren()[0].get());
    auto* textB = dynamic_cast<UIText*>(instanceB->getChildren()[0].get());
    ASSERT_NE(textA, nullptr);
    ASSERT_NE(textB, nullptr);
    EXPECT_EQ(textA->getText(), "Hello Doc");
    EXPECT_EQ(textA->_fontSize, 24u);
    EXPECT_EQ(textA->_color, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(textA->hasAuthoredStyle());
    EXPECT_EQ(textA->_styleKey, "text.header");

    // Mutating A must not leak into B.
    textA->setText("Changed");
    EXPECT_EQ(textB->getText(), "Hello Doc");

    // Instances are detached: no tree, no parent chain into a tree.
    EXPECT_FALSE(instanceA->isAttached());
    EXPECT_EQ(instanceA->getParent(), nullptr);
    EXPECT_EQ(textA->getParent(), instanceA.get());
}

TEST(UIDocumentTest, JsonRoundtrip)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    auto panel = registry.createInstance("test.doc_panel");
    auto* panelWidget  = dynamic_cast<UIPanel*>(panel.get());
    ASSERT_NE(panelWidget, nullptr);
    panelWidget->setColor({0.12f, 0.14f, 0.22f, 0.88f});
    EXPECT_TRUE(panelWidget->hasAuthoredStyle());
    panelWidget->_zOrder   = 5;
    auto label       = registry.createInstance("test.doc_text");
    auto* labelWidget = dynamic_cast<UIText*>(label.get());
    ASSERT_NE(labelWidget, nullptr);
    labelWidget->setText("JSON UI");
    panel->addDetachedChild(label);

    auto document = UIDocument::fromWidget(*panel);
    ASSERT_NE(document, nullptr);

    const nlohmann::json json = document->toJson();
    EXPECT_EQ(json["version"].get<uint32_t>(), UIDocument::kFormatVersion);
    EXPECT_EQ(json["typeId"].get<std::string>(), "test.doc_panel");
    EXPECT_TRUE(json["fields"].is_object());
    ASSERT_TRUE(json["children"].is_array());
    EXPECT_EQ(json["children"].size(), 1u);
    ASSERT_TRUE(json["childSlots"].is_array());
    ASSERT_EQ(json["childSlots"].size(), 1u);
    EXPECT_EQ(json["childSlots"][0]["type"], "canvas");

    auto reloaded = UIDocument::fromJson(json);
    ASSERT_NE(reloaded, nullptr);
    auto instance = reloaded->instantiate();
    ASSERT_NE(instance, nullptr);
    auto* panelInstance = dynamic_cast<UIPanel*>(instance.get());
    ASSERT_NE(panelInstance, nullptr);
    EXPECT_EQ(panelInstance->getColor(), glm::vec4(0.12f, 0.14f, 0.22f, 0.88f));
    EXPECT_EQ(panelInstance->_zOrder, 5);
    ASSERT_EQ(instance->getChildren().size(), 1u);
    auto* textInstance = dynamic_cast<UIText*>(instance->getChildren()[0].get());
    ASSERT_NE(textInstance, nullptr);
    EXPECT_EQ(textInstance->getText(), "JSON UI");
    EXPECT_EQ(panelInstance->_styleKey, "panel");
    EXPECT_TRUE(panelInstance->hasExplicitFill());
    EXPECT_TRUE(panelInstance->hasAuthoredStyle());
    ASSERT_TRUE(json["fields"].contains("_authoredStyle"));
    EXPECT_TRUE(json["fields"]["_authoredStyle"].is_object());
}

TEST(UIDocumentTest, BoxSlotIntentRoundtripsOnParentEdge)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();
    auto container = registry.createInstance("test.doc_container");
    auto child = registry.createInstance("test.doc_text");
    ASSERT_NE(container, nullptr);
    ASSERT_NE(child, nullptr);
    container->addDetachedChild(child, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UIBoxSlot>();
        ASSERT_NE(slot, nullptr);
        slot->setSizeRule(EUIBoxSlotSizeRule::Fill);
        slot->setWeight(2.5f);
        slot->setMargin(FMargin(3.0f, 4.0f, 5.0f, 6.0f));
        slot->setCrossAlignment(EUIBoxSlotCrossAlignment::Center);
        slot->setPreferredSize({120.0f, 22.0f});
        slot->setMinSize({40.0f, 10.0f});
        slot->setMaxSize({240.0f, 80.0f});
        slot->setParticipatesInLayout(false);
        slot->setReserveSpaceWhenHidden(false);
    });

    auto document = UIDocument::fromWidget(*container);
    ASSERT_NE(document, nullptr);
    ASSERT_EQ(document->childSlots.size(), 1u);
    EXPECT_EQ(document->childSlots[0]["type"], "box");
    auto restored = document->instantiate();
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->getChildren().size(), 1u);
    auto* slot = restored->getSlotForChild(*restored->getChildren()[0])->as<UIBoxSlot>();
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getSizeRule(), EUIBoxSlotSizeRule::Fill);
    EXPECT_FLOAT_EQ(slot->getWeight(), 2.5f);
    EXPECT_EQ(slot->getMargin(), FMargin(3.0f, 4.0f, 5.0f, 6.0f));
    EXPECT_EQ(slot->getCrossAlignment(), EUIBoxSlotCrossAlignment::Center);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(120.0f, 22.0f));
    EXPECT_EQ(slot->getMinSize(), glm::vec2(40.0f, 10.0f));
    EXPECT_EQ(slot->getMaxSize(), glm::vec2(240.0f, 80.0f));
    EXPECT_FALSE(slot->participatesInLayout());
    EXPECT_FALSE(slot->reservesSpaceWhenHidden());
}

TEST(UIDocumentTest, CanvasSlotIntentRoundtripsAnchorAndInsets)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();
    auto panel = registry.createInstance("test.doc_panel");
    auto child = registry.createInstance("test.doc_text");
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(child, nullptr);
    panel->addDetachedChild(child, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UICanvasSlot>();
        ASSERT_NE(slot, nullptr);
        FCanvasSlotArgs args;
        args.anchorMin = {0.25f, 0.1f};
        args.anchorMax = {0.9f, 0.8f};
        args.offset = {7.0f, 8.0f};
        args.offsets = FMargin(1.0f, 2.0f, 3.0f, 4.0f);
        args.minSize = {20.0f, 12.0f};
        args.maxSize = {500.0f, 300.0f};
        args.alignmentH = EWidgetAlignH::Center;
        args.alignmentV = EWidgetAlignV::Bottom;
        args.widthSizeMode = EWidgetSizeMode::Auto;
        args.heightSizeMode = EWidgetSizeMode::Fixed;
        args.pivot = {0.5f, 1.0f};
        args.preferredSize = {90.0f, 30.0f};
        args.fixedSize = {140.0f, 30.0f};
        slot->apply(args);
    });

    auto reloaded = UIDocument::fromJson(UIDocument::fromWidget(*panel)->toJson());
    ASSERT_NE(reloaded, nullptr);
    auto restored = reloaded->instantiate();
    ASSERT_NE(restored, nullptr);
    auto* slot = restored->getSlotForChild(*restored->getChildren()[0])->as<UICanvasSlot>();
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getAnchorMin(), glm::vec2(0.25f, 0.1f));
    EXPECT_EQ(slot->getAnchorMax(), glm::vec2(0.9f, 0.8f));
    EXPECT_EQ(slot->getOffset(), glm::vec2(7.0f, 8.0f));
    EXPECT_EQ(slot->getOffsets(), FMargin(1.0f, 2.0f, 3.0f, 4.0f));
    EXPECT_EQ(slot->getMinSize(), glm::vec2(20.0f, 12.0f));
    EXPECT_EQ(slot->getMaxSize(), glm::vec2(500.0f, 300.0f));
    EXPECT_EQ(slot->getAlignmentH(), EWidgetAlignH::Center);
    EXPECT_EQ(slot->getAlignmentV(), EWidgetAlignV::Bottom);
    EXPECT_EQ(slot->getWidthSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(slot->getHeightSizeMode(), EWidgetSizeMode::Fixed);
    EXPECT_EQ(slot->getPivot(), glm::vec2(0.5f, 1.0f));
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(90.0f, 30.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(140.0f, 30.0f));
}

TEST(UIDocumentTest, OverlaySlotIntentRoundtrips)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();
    auto parent = registry.createInstance("test.doc_overlay");
    auto child = registry.createInstance("test.doc_text");
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    parent->addDetachedChild(child, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UIOverlaySlot>();
        ASSERT_NE(slot, nullptr);
        FOverlaySlotArgs args;
        args.hAlign = EUIOverlayAlignment::End;
        args.vAlign = EUIOverlayAlignment::Center;
        args.padding = FMargin(2.0f, 3.0f, 5.0f, 7.0f);
        args.preferredSize = {160.0f, 28.0f};
        slot->apply(args);
    });
    auto restored = UIDocument::fromJson(UIDocument::fromWidget(*parent)->toJson())->instantiate();
    ASSERT_NE(restored, nullptr);
    auto* slot = restored->getSlotForChild(*restored->getChildren()[0])->as<UIOverlaySlot>();
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::End);
    EXPECT_EQ(slot->getVAlign(), EUIOverlayAlignment::Center);
    EXPECT_EQ(slot->getPadding(), FMargin(2.0f, 3.0f, 5.0f, 7.0f));
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(160.0f, 28.0f));
}

TEST(UIDocumentTest, SingleChildSlotIntentRoundtrips)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();
    auto parent = registry.createInstance("test.doc_size_box");
    auto child = registry.createInstance("test.doc_text");
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    parent->addDetachedChild(child, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UIOverlaySlot>();
        ASSERT_NE(slot, nullptr);
        FOverlaySlotArgs args;
        args.hAlign = EUIOverlayAlignment::Center;
        args.vAlign = EUIOverlayAlignment::End;
        args.preferredSize = {90.0f, 24.0f};
        slot->apply(args);
    });
    auto restored = UIDocument::fromJson(UIDocument::fromWidget(*parent)->toJson())->instantiate();
    ASSERT_NE(restored, nullptr);
    auto* slot = restored->getSlotForChild(*restored->getChildren()[0])->as<UIOverlaySlot>();
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::Center);
    EXPECT_EQ(slot->getVAlign(), EUIOverlayAlignment::End);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(90.0f, 24.0f));
}

TEST(UIDocumentTest, TableSlotIntentRoundtripsCell)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();
    auto parent = registry.createInstance("test.doc_table");
    auto child = registry.createInstance("test.doc_text");
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    parent->addDetachedChild(child, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UITableSlot>();
        ASSERT_NE(slot, nullptr);
        slot->setCell(3, 4);
    });
    auto restored = UIDocument::fromJson(UIDocument::fromWidget(*parent)->toJson())->instantiate();
    ASSERT_NE(restored, nullptr);
    auto* slot = restored->getSlotForChild(*restored->getChildren()[0])->as<UITableSlot>();
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getRow(), 3);
    EXPECT_EQ(slot->getColumn(), 4);
}

TEST(UIDocumentTest, AuthoredButtonStyleJsonRoundtrip)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    auto button = registry.createInstance("test.doc_button");
    auto* widget = dynamic_cast<UIButton*>(button.get());
    ASSERT_NE(widget, nullptr);
    FButtonStyle authored;
    authored.normalFill = FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f});
    widget->setStyle(authored);

    auto document = UIDocument::fromWidget(*button);
    ASSERT_NE(document, nullptr);
    const nlohmann::json json = document->toJson();
    ASSERT_TRUE(json["fields"].contains("_authoredStyle"));
    EXPECT_TRUE(json["fields"]["_authoredStyle"].is_object());

    auto reloaded = UIDocument::fromJson(json);
    ASSERT_NE(reloaded, nullptr);
    auto instance = reloaded->instantiate();
    auto* restored = dynamic_cast<UIButton*>(instance.get());
    ASSERT_NE(restored, nullptr);
    ASSERT_TRUE(restored->hasAuthoredStyle());
    EXPECT_EQ(resolveWidgetStyle<FButtonStyle>(*restored, restored->_authoredStyle).normalFill,
              FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f}));
}

TEST(UIDocumentTest, AuthoredPanelFillSurvivesThemeAfterReload)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    auto panel = registry.createInstance("test.doc_panel");
    auto* panelWidget = dynamic_cast<UIPanel*>(panel.get());
    ASSERT_NE(panelWidget, nullptr);
    panelWidget->setColor({0.12f, 0.14f, 0.22f, 0.88f});

    auto document = UIDocument::fromWidget(*panel);
    ASSERT_NE(document, nullptr);
    auto instance = UIDocument::fromJson(document->toJson())->instantiate();
    auto* restored = dynamic_cast<UIPanel*>(instance.get());
    ASSERT_NE(restored, nullptr);
    EXPECT_TRUE(restored->hasAuthoredStyle());

    WidgetTree tree({.width = 320, .height = 200});
    auto theme = std::make_shared<UITheme>();
    FPanelStyle themed;
    themed.fillColor = FBrush::solid({0.7f, 0.1f, 0.2f, 1.0f});
    theme->define<FPanelStyle>("panel", themed);
    tree.setTheme(theme.get());
    FCanvasSlotArgs slotArgs;
    slotArgs.fixedSize = {100.0f, 50.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), instance, slotArgs).valid());

    const UIFrameSnapshot snap = tree.buildSnapshot({});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items.front().color, glm::vec4(0.12f, 0.14f, 0.22f, 0.88f));
}

TEST(UIDocumentTest, UnknownTypeIdReportsDiagnostic)
{
    ensureTestTypesRegistered();
    nlohmann::json json;
    json["version"] = UIDocument::kFormatVersion;
    json["typeId"]  = "test.never_registered";
    json["fields"]  = nlohmann::json::object();
    json["children"] = nlohmann::json::array();

    auto document = UIDocument::fromJson(json);
    ASSERT_NE(document, nullptr);
    EXPECT_EQ(document->instantiate(), nullptr);
}

TEST(UIDocumentTest, UnsupportedVersionIsRejected)
{
    nlohmann::json json;
    json["version"] = UIDocument::kFormatVersion + 1;
    json["typeId"]  = "test.doc_panel";
    EXPECT_EQ(UIDocument::fromJson(json), nullptr);
}

TEST(UIDocumentTest, ChildSlotCountMustMatchChildren)
{
    ensureTestTypesRegistered();
    nlohmann::json json;
    json["version"] = UIDocument::kFormatVersion;
    json["typeId"] = "test.doc_panel";
    json["fields"] = nlohmann::json::object();
    json["children"] = nlohmann::json::array({
        {{"version", UIDocument::kFormatVersion}, {"typeId", "test.doc_text"},
         {"fields", nlohmann::json::object()}, {"children", nlohmann::json::array()},
         {"childSlots", nlohmann::json::array()}},
    });
    json["childSlots"] = nlohmann::json::array();
    EXPECT_EQ(UIDocument::fromJson(json), nullptr);
}

TEST(UIDocumentTest, FromWidgetRequiresRegistryTypeId)
{
    ensureTestTypesRegistered();
    auto plain = std::make_shared<UIPanel>("Plain");
    EXPECT_EQ(UIDocument::fromWidget(*plain), nullptr);
}

TEST(UIDocumentTest, InstantiatedSubtreeCanAttachToTree)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    auto container = registry.createInstance("test.doc_container");
    auto title     = registry.createInstance("test.doc_text");
    container->addDetachedChild(title);
    auto document = UIDocument::fromWidget(*container);
    ASSERT_NE(document, nullptr);

    WidgetTree tree({.width = 800, .height = 600});
    auto instance = document->instantiate();
    ASSERT_NE(instance, nullptr);

    auto attachment = tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), instance);
    EXPECT_TRUE(attachment.valid());
    EXPECT_TRUE(tree.contains(*instance));
    // Subtree members carry the same tree membership.
    EXPECT_EQ(instance->getChildren()[0]->getTree(), &tree);
    EXPECT_TRUE(instance->getChildren()[0]->isAttached());
}

TEST(UIDocumentTest, DocumentInstanceCanMountThroughAdapterHostAndPatchLiveRoot)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    auto source = registry.createInstance("test.doc_text");
    auto* sourceText = dynamic_cast<UIText*>(source.get());
    ASSERT_NE(sourceText, nullptr);
    sourceText->setText("Doc Title");
    sourceText->setColor({0.4f, 0.5f, 0.6f, 1.0f});

    auto document = UIDocument::fromWidget(*source);
    ASSERT_NE(document, nullptr);

    WidgetTree tree({.width = 800, .height = 600});
    UIAdapterHost adapterHost(tree, *tree.getLayer(WidgetTree::ELayer::Content));

    UIElementRef instance = document->instantiate();
    ASSERT_NE(instance, nullptr);
    auto* mounted = dynamic_cast<UIText*>(&adapterHost.mount(instance));
    ASSERT_NE(mounted, nullptr);
    EXPECT_EQ(adapterHost.getRoot(), mounted);
    EXPECT_TRUE(tree.contains(*mounted));
    EXPECT_EQ(mounted->resolvedText(), "Doc Title");
    EXPECT_EQ(mounted->_color, glm::vec4(0.4f, 0.5f, 0.6f, 1.0f));

    adapterHost.patch([](UIElement& root) {
        auto& text = static_cast<UIText&>(root);
        text.setText("Patched Title");
    });
    EXPECT_EQ(mounted->resolvedText(), "Patched Title");

    adapterHost.unmount();
    EXPECT_EQ(adapterHost.getRoot(), nullptr);
    EXPECT_FALSE(tree.contains(*mounted));
}

TEST(UIDocumentTest, RemountReplacesPreviousAdapterRootCleanly)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    auto first = registry.createInstance("test.doc_text");
    auto second = registry.createInstance("test.doc_text");
    auto* firstText = dynamic_cast<UIText*>(first.get());
    auto* secondText = dynamic_cast<UIText*>(second.get());
    ASSERT_NE(firstText, nullptr);
    ASSERT_NE(secondText, nullptr);
    firstText->setText("First");
    secondText->setText("Second");

    WidgetTree tree({.width = 800, .height = 600});
    UIAdapterHost adapterHost(tree, *tree.getLayer(WidgetTree::ELayer::Content));

    auto* firstMounted = dynamic_cast<UIText*>(&adapterHost.mount(first));
    ASSERT_NE(firstMounted, nullptr);
    tree.setFocus(firstMounted);
    ASSERT_EQ(tree.getFocused(), firstMounted);

    auto* secondMounted = dynamic_cast<UIText*>(&adapterHost.mount(second));
    ASSERT_NE(secondMounted, nullptr);
    EXPECT_NE(secondMounted, firstMounted);
    EXPECT_EQ(adapterHost.getRoot(), secondMounted);
    EXPECT_FALSE(tree.contains(*firstMounted));
    EXPECT_TRUE(tree.contains(*secondMounted));
    EXPECT_EQ(tree.getFocused(), nullptr);
    EXPECT_EQ(secondMounted->resolvedText(), "Second");
}

TEST(UIDocumentTest, DeserializeOnAttachedWidgetAggregatesSingleInvalidation)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    // A source widget whose serialized fields become the bulk-restore payload.
    auto source = registry.createInstance("test.doc_panel");
    auto* sourcePanel = dynamic_cast<UIPanel*>(source.get());
    ASSERT_NE(sourcePanel, nullptr);
    sourcePanel->setColor({0.5f, 0.5f, 0.5f, 1.0f});
    auto doc = UIDocument::fromWidget(*source);
    ASSERT_NE(doc, nullptr);

    // A live target attached to a tree.
    auto target = registry.createInstance("test.doc_panel");
    WidgetTree tree({.width = 800, .height = 600});
    EXPECT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target).valid());
    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;

    // Bulk field restore on a live widget: reflection writes bypass setters,
    // so the transaction must aggregate one Layout invalidation at its end.
    target->deserializeFields(doc->fields);
    tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore + 1);
}

TEST(UIDocumentTest, DeserializeOnDetachedWidgetIsNoOp)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    auto widget = registry.createInstance("test.doc_panel");
    auto* panel = dynamic_cast<UIPanel*>(widget.get());
    ASSERT_NE(panel, nullptr);

    // Detached: no tree, so the transaction's aggregated invalidation is a
    // no-op (no tree to invalidate). The paint-dirty flag may be set, but the
    // subsequent attach() invalidates layout and the first paint runs anyway.
    panel->deserializeFields(nlohmann::json::object());
    EXPECT_FALSE(panel->isAttached());
}

} // namespace ya
