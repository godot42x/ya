// Phase 2a regression guards for UIDocument (ui-widget-tree-refactor): the
// `.yaui` schema, independent instantiation, JSON roundtrip, and detached
// subtree authoring — all without a Scene or WidgetTree.

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"

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

    container->_size = {300.0f, 120.0f};
    auto* titleWidget = dynamic_cast<UIText*>(title.get());
    ASSERT_NE(titleWidget, nullptr);
    titleWidget->_position = {10.0f, 20.0f};
    titleWidget->_text     = "Hello Doc";
    titleWidget->_fontSize = 24;
    titleWidget->_color    = {1.0f, 0.0f, 0.0f, 1.0f};
    ok->_position          = {100.0f, 200.0f};
    ok->_size              = {80.0f, 32.0f};
    container->addDetachedChild(title);
    container->addDetachedChild(ok);

    auto document = UIDocument::fromWidget(*container);
    ASSERT_NE(document, nullptr);
    EXPECT_EQ(document->typeId, "test.doc_container");
    EXPECT_EQ(document->children.size(), 2u);

    // Independent instances: same fields, no shared mutable state.
    UIElementRef instanceA = document->instantiate();
    UIElementRef instanceB = document->instantiate();
    ASSERT_NE(instanceA, nullptr);
    ASSERT_NE(instanceB, nullptr);

    EXPECT_EQ(instanceA->_size, glm::vec2(300.0f, 120.0f));
    EXPECT_EQ(instanceB->_size, glm::vec2(300.0f, 120.0f));
    ASSERT_EQ(instanceA->getChildren().size(), 2u);
    ASSERT_EQ(instanceB->getChildren().size(), 2u);

    auto* textA = dynamic_cast<UIText*>(instanceA->getChildren()[0].get());
    auto* textB = dynamic_cast<UIText*>(instanceB->getChildren()[0].get());
    ASSERT_NE(textA, nullptr);
    ASSERT_NE(textB, nullptr);
    EXPECT_EQ(textA->_text, "Hello Doc");
    EXPECT_EQ(textA->_fontSize, 24u);
    EXPECT_EQ(textA->_color, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    // Mutating A must not leak into B.
    textA->_text = "Changed";
    EXPECT_EQ(textB->_text, "Hello Doc");

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
    panelWidget->_color    = {0.12f, 0.14f, 0.22f, 0.88f};
    panelWidget->_zOrder   = 5;
    panelWidget->_position = {20.0f, 20.0f};
    auto label       = registry.createInstance("test.doc_text");
    auto* labelWidget = dynamic_cast<UIText*>(label.get());
    ASSERT_NE(labelWidget, nullptr);
    labelWidget->_text = "JSON UI";
    panel->addDetachedChild(label);

    auto document = UIDocument::fromWidget(*panel);
    ASSERT_NE(document, nullptr);

    const nlohmann::json json = document->toJson();
    EXPECT_EQ(json["version"].get<uint32_t>(), UIDocument::kFormatVersion);
    EXPECT_EQ(json["typeId"].get<std::string>(), "test.doc_panel");
    EXPECT_TRUE(json["fields"].is_object());
    ASSERT_TRUE(json["children"].is_array());
    EXPECT_EQ(json["children"].size(), 1u);

    auto reloaded = UIDocument::fromJson(json);
    ASSERT_NE(reloaded, nullptr);
    auto instance = reloaded->instantiate();
    ASSERT_NE(instance, nullptr);
    auto* panelInstance = dynamic_cast<UIPanel*>(instance.get());
    ASSERT_NE(panelInstance, nullptr);
    EXPECT_EQ(panelInstance->_color, glm::vec4(0.12f, 0.14f, 0.22f, 0.88f));
    EXPECT_EQ(panelInstance->_zOrder, 5);
    ASSERT_EQ(instance->getChildren().size(), 1u);
    auto* textInstance = dynamic_cast<UIText*>(instance->getChildren()[0].get());
    ASSERT_NE(textInstance, nullptr);
    EXPECT_EQ(textInstance->_text, "JSON UI");
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

    auto attachment = tree.attachToLayer(WidgetTree::ELayer::Content, instance);
    EXPECT_TRUE(attachment.valid());
    EXPECT_TRUE(tree.contains(*instance));
    // Subtree members carry the same tree membership.
    EXPECT_EQ(instance->getChildren()[0]->getTree(), &tree);
    EXPECT_TRUE(instance->getChildren()[0]->isAttached());
}

TEST(UIDocumentTest, DeserializeOnAttachedWidgetAggregatesSingleInvalidation)
{
    ensureTestTypesRegistered();
    auto& registry = UITypeRegistry::instance();

    // A source widget whose serialized fields become the bulk-restore payload.
    auto source = registry.createInstance("test.doc_panel");
    auto* sourcePanel = dynamic_cast<UIPanel*>(source.get());
    ASSERT_NE(sourcePanel, nullptr);
    sourcePanel->_size  = {300.0f, 120.0f};
    sourcePanel->_color = {0.5f, 0.5f, 0.5f, 1.0f};
    auto doc = UIDocument::fromWidget(*source);
    ASSERT_NE(doc, nullptr);

    // A live target attached to a tree.
    auto target = registry.createInstance("test.doc_panel");
    WidgetTree tree({.width = 800, .height = 600});
    EXPECT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, target).valid());
    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;

    // Bulk field restore on a live widget: reflection writes bypass setters,
    // so the transaction must aggregate one Layout invalidation at its end.
    target->deserializeFields(doc->fields);
    tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(target->_size, glm::vec2(300.0f, 120.0f));
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
