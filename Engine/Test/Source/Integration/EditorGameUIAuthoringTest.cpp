#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include "GUI/Widgets/UIDocumentStore.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneWidgetEntry.h"

#include <gtest/gtest.h>

#include <string>

namespace ya
{

TEST(EditorGameUIAuthoringTest, ChosenNameStepsZOrderAndEntryFields)
{
    EditorDocumentRegistry documents;
    UIDocumentStore        uiDocuments;
    EditorLayer            layer{nullptr};
    layer.bindDocumentServices(&documents, &uiDocuments);

    Scene scene{"World"};
    layer.setEditableScene(&scene);
    SceneWidgetEntry existing;
    existing.entryId      = "Old";
    existing.documentPath = "Content:UI/Old.yaui.json";
    existing.zOrder       = 30;
    scene.addWidgetEntry(existing);

    ASSERT_TRUE(layer.createAndMountGameUIAt("Content:UI", "HUD"));
    ASSERT_EQ(scene.getWidgetEntries().size(), 2u);
    const SceneWidgetEntry* hud = nullptr;
    for (const SceneWidgetEntry& entry : scene.getWidgetEntries()) {
        if (entry.entryId == "HUD") {
            hud = &entry;
        }
    }
    ASSERT_NE(hud, nullptr);
    EXPECT_EQ(hud->documentPath, "Content:UI/HUD.yaui.json");
    EXPECT_EQ(hud->zOrder, 40);
    EXPECT_FALSE(hud->bModal);
    EXPECT_FALSE(hud->bPausesGame);
    EXPECT_EQ(hud->clock, ESceneWidgetClock::Real);

    EXPECT_FALSE(layer.createAndMountGameUIAt("Content:UI", "HUD"));
    EXPECT_FALSE(layer.createAndMountGameUIAt("Content:UI", ""));
    EXPECT_FALSE(layer.createAndMountGameUIAt("Content:UI", "A/B"));
    EXPECT_EQ(scene.getWidgetEntries().size(), 2u);

    layer.setSelectedWidgetEntryId("HUD");
    layer.setSelectedWidgetEntryModal(true);
    layer.setSelectedWidgetEntryPausesGame(true);
    layer.setSelectedWidgetEntryClock(ESceneWidgetClock::Game);
    layer.setSelectedWidgetEntryZOrder(12);
    EXPECT_TRUE(hud->bModal);
    EXPECT_TRUE(hud->bPausesGame);
    EXPECT_EQ(hud->clock, ESceneWidgetClock::Game);
    EXPECT_EQ(hud->zOrder, 12);

    Scene empty{"Empty"};
    layer.setEditableScene(&empty);
    ASSERT_TRUE(layer.createAndMountGameUIAt("Content:Screens", "Panel"));
    ASSERT_EQ(empty.getWidgetEntries().size(), 1u);
    EXPECT_EQ(empty.getWidgetEntries().front().documentPath, "Content:Screens/Panel.yaui.json");
    EXPECT_EQ(empty.getWidgetEntries().front().zOrder, 10);
}

TEST(EditorGameUIAuthoringTest, UiScriptTemplateShowsClickAndCancel)
{
    const std::string source = EditorLayer::uiScriptTemplate();
    EXPECT_NE(source.find("function S:onInit()"), std::string::npos);
    EXPECT_NE(source.find("function S:onUpdate(dt)"), std::string::npos);
    EXPECT_NE(source.find("function S:onCancel()"), std::string::npos);
    EXPECT_NE(source.find("self:find(\"Button\")"), std::string::npos);
    EXPECT_NE(source.find("onClicked:add(self, function()"), std::string::npos);
    EXPECT_NE(source.find("return S"), std::string::npos);
}

} // namespace ya
