#include "GameEditor/UI/EditorDockWorkspace.h"

#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/Panel.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <unordered_set>
#include <vector>

namespace ya
{

namespace
{

std::vector<std::string> leafKeys(const FDockContext& context, std::string_view panelKey)
{
    const FDockContext::FPanel* panel = context.findPanelByStableKey(panelKey);
    if (!panel) {
        return {};
    }
    const FDockNode* leaf = context.dockModel().findLeafForPanel(panel->id);
    if (!leaf) {
        return {};
    }
    std::vector<std::string> keys;
    for (const DockPanelId id : leaf->panelIds) {
        if (const FDockPanelRecord* record = context.dockModel().findPanel(id)) {
            keys.push_back(record->stableKey);
        }
    }
    return keys;
}

bool spawnLayoutPanels(FDockContext& context, const nlohmann::json& layout)
{
    for (const std::string& key : FDockContext::collectLayoutPanelKeys(layout)) {
        if (context.addPanel(key, key, std::make_shared<UIPanel>(key)) == kInvalidDockPanelId) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(EditorDockWorkspaceTest, FactoryLayoutPlacesDefaultTabs)
{
    FDockContext context;
    context.bAllowFloating = true;
    const nlohmann::json& factory = EditorDockWorkspace::factoryLayout();
    ASSERT_TRUE(spawnLayoutPanels(context, factory));
    ASSERT_TRUE(context.importLayoutJson(factory));

    EXPECT_EQ(context.dockModel().getRootNode()->kind, EDockNodeKind::Split);
    EXPECT_EQ(context.dockModel().getRootNode()->orientation, EDockSplitOrientation::Vertical);
    EXPECT_FLOAT_EQ(context.dockModel().getRootNode()->ratio, 0.74f);

    EXPECT_EQ(leafKeys(context, "viewport"), std::vector<std::string>({"viewport"}));
    EXPECT_EQ(leafKeys(context, "hierarchy"), std::vector<std::string>({"hierarchy"}));
    EXPECT_EQ(leafKeys(context, "inspector"), std::vector<std::string>({"inspector"}));
    EXPECT_EQ(leafKeys(context, "content-browser"),
              (std::vector<std::string>{"content-browser",
                                        "frame-stats",
                                        "runtime-tools",
                                        "ui-designer",
                                        "asset-inspector",
                                        "debug-images"}));
    EXPECT_TRUE(context.floatingWindows().empty());
}

TEST(EditorDockWorkspaceTest, SavedLayoutWithUnknownSpawnerStillRestoresKnownTabs)
{
    FDockContext context;
    context.bAllowFloating = true;
    const nlohmann::json saved = {
        {"version", 1},
        {"root",
         {{"kind", "split"},
          {"orientation", "vertical"},
          {"ratio", 0.6f},
          {"children",
           nlohmann::json::array({
               nlohmann::json{{"kind", "leaf"},
                              {"panels", nlohmann::json::array({"viewport", "gui-workbench"})},
                              {"selected", "viewport"}},
               nlohmann::json{{"kind", "leaf"},
                              {"panels", nlohmann::json::array({"inspector"})},
                              {"selected", "inspector"}},
           })}}},
        {"floating", nlohmann::json::array()},
    };

    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("I")), kInvalidDockPanelId);
    const nlohmann::json sanitized = FDockContext::sanitizeLayoutJson(
        saved, std::unordered_set<std::string>{"viewport", "inspector"});
    ASSERT_TRUE(context.importLayoutJson(sanitized));
    EXPECT_EQ(leafKeys(context, "viewport"), std::vector<std::string>({"viewport"}));
    EXPECT_EQ(leafKeys(context, "inspector"), std::vector<std::string>({"inspector"}));
    EXPECT_FALSE(context.hasPanel("gui-workbench"));
}

} // namespace ya
