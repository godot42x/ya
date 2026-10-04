// SceneWidgetEntry roundtrip and instance overrides regression guards.

#include "Scene/Core/SceneWidgetEntry.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(SceneWidgetEntryTest, EntryJsonRoundtripWithDocumentReferenceAndOverrides)
{
    SceneWidgetEntry entry;
    entry.entryId      = "HUD";
    entry.documentPath = "Example/Game/Content/UI/HUD.yaui";
    entry.zOrder    = 7;
    entry.autoMount = false;
    entry.overrides.fieldOverrides["_color"] = nlohmann::json{1.0, 0.0, 0.0, 1.0};

    const nlohmann::json json = entry.toJson();
    EXPECT_EQ(json["entryId"], "HUD");
    EXPECT_EQ(json["zOrder"].get<int32_t>(), 7);
    EXPECT_FALSE(json["autoMount"].get<bool>());
    EXPECT_EQ(json["document"], "Example/Game/Content/UI/HUD.yaui");
    ASSERT_TRUE(json["rootSlot"].is_object());
    EXPECT_EQ(json["rootSlot"]["anchorMax"][0], 1.0f);
    EXPECT_FALSE(json.contains("inline"));

    const SceneWidgetEntry reloaded = SceneWidgetEntry::fromJson(json);
    EXPECT_EQ(reloaded.entryId, "HUD");
    EXPECT_EQ(reloaded.zOrder, 7);
    EXPECT_FALSE(reloaded.autoMount);
    EXPECT_EQ(reloaded.documentPath, "Example/Game/Content/UI/HUD.yaui");
    EXPECT_EQ(reloaded.rootSlot.anchorMin, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(reloaded.rootSlot.anchorMax, glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(reloaded.overrides.fieldOverrides.at("_color")[0], 1.0);
}

TEST(SceneWidgetEntryTest, EmptyOverridesAreOmittedAndLoadAsNone)
{
    SceneWidgetEntry entry;
    entry.entryId      = "HUD";
    entry.documentPath = "Example/Game/Content/UI/HUD.yaui";

    const nlohmann::json json = entry.toJson();
    EXPECT_FALSE(json.contains("overrides"));

    nlohmann::json legacy = json;
    legacy["overrides"]   = nlohmann::json::object();

    const SceneWidgetEntry fromMissing = SceneWidgetEntry::fromJson(json);
    const SceneWidgetEntry fromEmpty   = SceneWidgetEntry::fromJson(legacy);
    EXPECT_TRUE(fromMissing.overrides.empty());
    EXPECT_TRUE(fromEmpty.overrides.empty());
    EXPECT_EQ(fromMissing.entryId, fromEmpty.entryId);
    EXPECT_EQ(fromMissing.documentPath, fromEmpty.documentPath);
}

TEST(SceneWidgetEntryTest, OverrideAppliesToOwnAndBaseFields)
{
    auto& registry = UITypeRegistry::instance();
    auto  panel    = registry.createInstance("engine.border");
    ASSERT_NE(panel, nullptr);

    UIInstanceOverrideSet overrides;
    overrides.fieldOverrides["_color"]   = nlohmann::json{1.0, 0.0, 0.0, 1.0};

    EXPECT_TRUE(overrides.applyTo(*panel));
    auto* panelWidget = dynamic_cast<UIBorder*>(panel.get());
    ASSERT_NE(panelWidget, nullptr);
    EXPECT_EQ(panelWidget->getColor(), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

TEST(SceneWidgetEntryTest, UnknownOverrideFieldIsRejected)
{
    auto& registry = UITypeRegistry::instance();
    auto  panel    = registry.createInstance("engine.border");
    ASSERT_NE(panel, nullptr);

    UIInstanceOverrideSet overrides;
    overrides.fieldOverrides["_not_a_field"] = nlohmann::json(1);

    EXPECT_FALSE(overrides.applyTo(*panel));
}

TEST(SceneWidgetEntryTest, NonInstanceEditableFieldIsRejected)
{
    auto& registry = UITypeRegistry::instance();
    auto  panel    = registry.createInstance("engine.border");
    ASSERT_NE(panel, nullptr);

    UIInstanceOverrideSet overrides;
    // `_pivot` is reflected (authorable in the document) but deliberately not
    // marked InstanceEditable: entry overrides must be filtered by metadata.
    overrides.fieldOverrides["_pivot"] = nlohmann::json{0.25, 0.25};

    EXPECT_FALSE(overrides.applyTo(*panel));
}



} // namespace ya
