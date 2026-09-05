#include "GameEditor/UI/EditorSettingsDialog.h"

#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorSettingsDialogTest, ScenePathStatusCoversEmptyExistsAndMissing)
{
    const FEditorSettingsScenePathStatus empty = describeEditorSettingsScenePath("", true);
    EXPECT_STREQ(empty.styleKey, "text.muted");
    EXPECT_STREQ(empty.text, "Empty means startup falls back to an empty scene");

    const FEditorSettingsScenePathStatus exists = describeEditorSettingsScenePath("Forest.scene.json", true);
    EXPECT_STREQ(exists.styleKey, "text.muted");
    EXPECT_STREQ(exists.text, "Used on next app start — scene exists");

    const FEditorSettingsScenePathStatus missing = describeEditorSettingsScenePath("Missing.scene.json", false);
    EXPECT_STREQ(missing.styleKey, "text.error");
    EXPECT_STREQ(missing.text, "Used on next app start — scene not found");
}

TEST(EditorSettingsDialogTest, SyncEnablesApplyWhenDraftIsDirty)
{
    int sampler = 0;
    bool overlay = true;
    std::string path;
    bool dirty = false;
    bool exists = false;

    FEditorSettingsBindings bindings;
    bindings.samplerIndex = [&]() { return sampler; };
    bindings.setSamplerIndex = [&](int index) { sampler = index; };
    bindings.showCameraOverlay = [&]() { return overlay; };
    bindings.setShowCameraOverlay = [&](bool value) { overlay = value; };
    bindings.scenePathDraft = [&]() { return path; };
    bindings.setScenePathDraft = [&](std::string value) { path = std::move(value); };
    bindings.scenePathDirty = [&]() { return dirty; };
    bindings.scenePathExists = [&]() { return exists; };
    bindings.applyScenePath = []() {};
    bindings.resetScenePath = []() {};

    WidgetTree tree({.width = 800, .height = 600});
    EditorSettingsDialog dialog;
    dialog.open(tree, bindings);
    ASSERT_TRUE(dialog.isOpen());
    dialog.sync(tree);
    EXPECT_FALSE(dialog.isApplyEnabled());

    path = "Castle.scene.json";
    dirty = true;
    dialog.sync(tree);
    EXPECT_TRUE(dialog.isApplyEnabled());
}

TEST(EditorSettingsDialogTest, BrowseOpensSceneJsonFilePicker)
{
    std::string path = "draft.scene.json";
    FEditorFilePickerRequest opened;
    FEditorSettingsBindings bindings;
    bindings.samplerIndex = []() { return 0; };
    bindings.setSamplerIndex = [](int) {};
    bindings.showCameraOverlay = []() { return false; };
    bindings.setShowCameraOverlay = [](bool) {};
    bindings.scenePathDraft = [&]() { return path; };
    bindings.setScenePathDraft = [&](std::string value) { path = std::move(value); };
    bindings.scenePathDirty = []() { return false; };
    bindings.scenePathExists = []() { return false; };
    bindings.applyScenePath = []() {};
    bindings.resetScenePath = []() {};
    bindings.openFilePicker = [&](FEditorFilePickerRequest request) { opened = std::move(request); };

    WidgetTree tree({.width = 800, .height = 600});
    EditorSettingsDialog dialog;
    dialog.open(tree, bindings);
    dialog.browseStartupScene();
    EXPECT_EQ(opened.title, "Select Default Scene");
    EXPECT_EQ(opened.currentPath, "draft.scene.json");
    ASSERT_TRUE(static_cast<bool>(opened.onPicked));
    opened.onPicked("Forest.scene.json");
    EXPECT_EQ(path, "Forest.scene.json");
}

} // namespace ya
