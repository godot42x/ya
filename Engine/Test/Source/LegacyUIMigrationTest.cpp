// Legacy Node2D-era scene UI migration unit tests. The importer lives in the
// serialization layer (Scene/Serialization/LegacyUIMigration.h) per the plan:
// compatibility logic stays in the serialization migration layer, never in
// the GUI framework. This suite is engine-only (ya-testing glob) because the
// GUI closure targets must stay free of scene/serialization dependencies.

#include "Scene/Serialization/LegacyUIMigration.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(LegacyUIMigrationTest, LegacyCanvasChildrenBecomeTopLevelEntries)
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

TEST(LegacyUIMigrationTest, LegacyVisibleBoolTranslatesToVisibilityEnum)
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

TEST(LegacyUIMigrationTest, LegacyNestedChildrenRecurseIntoDocument)
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

TEST(LegacyUIMigrationTest, UnknownLegacyTypeIsDroppedWithDiagnostic)
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
