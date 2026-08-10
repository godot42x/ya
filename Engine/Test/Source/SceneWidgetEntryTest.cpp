// SceneWidgetEntry roundtrip and instance overrides regression guards.

#include "Scene/Core/SceneWidgetEntry.h"
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

TEST(SceneWidgetEntryTest, NonInstanceEditableFieldIsRejected)
{
    auto& registry = UITypeRegistry::instance();
    auto  panel    = registry.createInstance("engine.panel");
    ASSERT_NE(panel, nullptr);

    UIInstanceOverrideSet overrides;
    // `_pivot` is reflected (authorable in the document) but deliberately not
    // marked InstanceEditable: entry overrides must be filtered by metadata.
    overrides.fieldOverrides["_pivot"] = nlohmann::json{0.25, 0.25};

    EXPECT_FALSE(overrides.applyTo(*panel));
}



} // namespace ya
