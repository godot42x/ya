// Phase 2b regression guards: SceneWidgetEntry roundtrip, instance overrides,
// and the legacy Node2D -> UIDocument importer (ui-widget-tree-refactor).

#include "GUI/Widgets/LegacyUIMigration.h"
#include "GUI/Widgets/SceneWidgetEntry.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(SceneWidgetEntryTest, EntryJsonRoundtripWithInlineDocumentAndOverrides)
{
    SceneWidgetEntry entry;
    entry.entryId = "HUD";
    entry.inlineDocument = std::make_shared<UIDocument>();
    entry.inlineDocument->typeId = "engine.panel";
    entry.inlineDocument->fields = nlohmann::json{{"_color", {0.1, 0.2, 0.3, 0.9}}};
    entry.zOrder    = 7;
    entry.autoMount = false;
    entry.overrides.fieldOverrides["_color"] = nlohmann::json{1.0, 0.0, 0.0, 1.0};

    const nlohmann::json json = entry.toJson();
    EXPECT_EQ(json["entryId"], "HUD");
    EXPECT_EQ(json["zOrder"].get<int32_t>(), 7);
    EXPECT_FALSE(json["autoMount"].get<bool>());
    EXPECT_EQ(json["inline"]["typeId"], "engine.panel");
    EXPECT_FALSE(json.contains("document"));

    const SceneWidgetEntry reloaded = SceneWidgetEntry::fromJson(json);
    EXPECT_EQ(reloaded.entryId, "HUD");
    EXPECT_EQ(reloaded.zOrder, 7);
    EXPECT_FALSE(reloaded.autoMount);
    ASSERT_NE(reloaded.inlineDocument, nullptr);
    EXPECT_EQ(reloaded.inlineDocument->typeId, "engine.panel");
    EXPECT_EQ(reloaded.overrides.fieldOverrides.at("_color")[0], 1.0);
}

TEST(SceneWidgetEntryTest, DocumentPathReferenceRoundtripsWithoutResolution)
{
    SceneWidgetEntry entry;
    entry.entryId      = "Menu";
    entry.documentPath = "Content/UI/Menu.yaui";
    entry.autoMount    = true;

    const nlohmann::json json = entry.toJson();
    EXPECT_EQ(json["document"], "Content/UI/Menu.yaui");
    EXPECT_FALSE(json.contains("inline"));

    const SceneWidgetEntry reloaded = SceneWidgetEntry::fromJson(json);
    EXPECT_EQ(reloaded.documentPath, "Content/UI/Menu.yaui");
    EXPECT_EQ(reloaded.inlineDocument, nullptr);
}

TEST(SceneWidgetEntryTest, OverrideAppliesToOwnAndBaseFields)
{
    auto& registry = UITypeRegistry::instance();
    auto  panel    = registry.createInstance("engine.panel");
    ASSERT_NE(panel, nullptr);

    UIInstanceOverrideSet overrides;
    overrides.fieldOverrides["_color"]   = nlohmann::json{1.0, 0.0, 0.0, 1.0};
    overrides.fieldOverrides["_position"] = nlohmann::json{12.0, 34.0};

    EXPECT_TRUE(overrides.applyTo(*panel));
    auto* panelWidget = dynamic_cast<UIPanel*>(panel.get());
    ASSERT_NE(panelWidget, nullptr);
    EXPECT_EQ(panelWidget->_color, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(panelWidget->_position, glm::vec2(12.0f, 34.0f));
}

TEST(SceneWidgetEntryTest, UnknownOverrideFieldIsRejected)
{
    auto& registry = UITypeRegistry::instance();
    auto  panel    = registry.createInstance("engine.panel");
    ASSERT_NE(panel, nullptr);

    UIInstanceOverrideSet overrides;
    overrides.fieldOverrides["_not_a_field"] = nlohmann::json(1);

    EXPECT_FALSE(overrides.applyTo(*panel));
}

TEST(SceneWidgetEntryTest, LegacyCanvasChildrenBecomeTopLevelEntries)
{
    nlohmann::json canvas;
    canvas["name"]     = "Canvas";
    canvas["nodeType"] = "UICanvasNode";
    canvas["children"] = nlohmann::json::array();

    nlohmann::json text;
    text["name"]     = "Title";
    text["nodeType"] = "UITextNode";
    text["fields"]   = nlohmann::json{
        {"__base__", nlohmann::json{{"Node2D", nlohmann::json{{"_position", {10.0, 20.0}}, {"_zOrder", 3}}}}},
        {"_text", "Hello UI"},
        {"_hAlign", "Center"},
    };
    canvas["children"].push_back(text);

    nlohmann::json button;
    button["name"]     = "OK";
    button["nodeType"] = "UIButtonNode";
    button["fields"]   = nlohmann::json{
        {"__base__", nlohmann::json{{"Node2D", nlohmann::json{{"_hitFilter", "Stop"}}}}},
        {"_normalColor", {0.8, 0.8, 0.8, 1.0}},
    };
    canvas["children"].push_back(button);

    const auto documents = migrateLegacyUINode(canvas);
    ASSERT_EQ(documents.size(), 2u);

    EXPECT_EQ(documents[0].name, "Title");
    EXPECT_EQ(documents[0].zOrder, 3);
    EXPECT_EQ(documents[0].document->typeId, "engine.text");
    EXPECT_EQ(documents[0].document->fields["_text"], "Hello UI");
    EXPECT_EQ(documents[0].document->fields["_hAlign"], "Center");
    // Base block renamed Node2D -> UIElement.
    EXPECT_EQ(documents[0].document->fields["__base__"]["UIElement"]["_position"][0], 10.0);
    EXPECT_FALSE(documents[0].document->fields["__base__"].contains("Node2D"));

    EXPECT_EQ(documents[1].name, "OK");
    EXPECT_EQ(documents[1].document->typeId, "engine.button");
    EXPECT_EQ(documents[1].document->fields["__base__"]["UIElement"]["_hitFilter"], "Stop");
}

TEST(SceneWidgetEntryTest, LegacyVisibleBoolTranslatesToVisibilityEnum)
{
    nlohmann::json node;
    node["name"]     = "Panel";
    node["nodeType"] = "UIPanelNode";
    node["fields"]   = nlohmann::json{
        {"_visible", false},
        {"_color", {0.1, 0.2, 0.3, 0.4}},
    };

    const auto documents = migrateLegacyUINode(node);
    ASSERT_EQ(documents.size(), 1u);
    EXPECT_EQ(documents[0].document->fields["_visibility"], "Hidden");
    EXPECT_FALSE(documents[0].document->fields.contains("_visible"));
}

TEST(SceneWidgetEntryTest, LegacyNestedChildrenRecurseIntoDocument)
{
    nlohmann::json container;
    container["name"]     = "Box";
    container["nodeType"] = "UIContainerNode";
    container["fields"]   = nlohmann::json{
        {"__base__", nlohmann::json{{"Node2D", nlohmann::json{{"_position", {0.0, 0.0}}}}}},
        {"_direction", "Vertical"},
    };
    container["children"] = nlohmann::json::array();
    nlohmann::json child;
    child["name"]     = "Inner";
    child["nodeType"] = "UIPanelNode";
    child["fields"]   = nlohmann::json{{"_color", {0.5, 0.5, 0.5, 1.0}}};
    container["children"].push_back(child);

    const auto documents = migrateLegacyUINode(container);
    ASSERT_EQ(documents.size(), 1u);
    EXPECT_EQ(documents[0].document->typeId, "engine.container");
    EXPECT_EQ(documents[0].document->fields["_direction"], "Vertical");
    ASSERT_EQ(documents[0].document->children.size(), 1u);
    EXPECT_EQ(documents[0].document->children[0]->typeId, "engine.panel");

    // The migrated document instantiates into a runnable detached subtree.
    auto instance = documents[0].document->instantiate();
    ASSERT_NE(instance, nullptr);
    ASSERT_EQ(instance->getChildren().size(), 1u);
    EXPECT_EQ(instance->getChildren()[0]->_typeId, "engine.panel");
}

TEST(SceneWidgetEntryTest, UnknownLegacyTypeIsDroppedWithDiagnostic)
{
    nlohmann::json node;
    node["name"]     = "Mystery";
    node["nodeType"] = "UIMysteryNode";
    node["fields"]   = nlohmann::json::object();

    EXPECT_TRUE(migrateLegacyUINode(node).empty());
    EXPECT_TRUE(legacyUITypeToTypeId("UIMysteryNode").empty());
    EXPECT_EQ(legacyUITypeToTypeId("UITextNode"), "engine.text");
}

} // namespace ya
