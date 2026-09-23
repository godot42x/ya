#include "GameEditor/UI/Dialogs/EditorSettingsDialog.h"

#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

/// Walk the attached tree for a stable key. The dialog owns its control pointers
/// privately, so the test has to find the widget the same way the shell does
/// (by key) instead of reaching into the dialog.
[[nodiscard]] UIElement* findByKey(UIElement& element, std::string_view key)
{
    if (element._stableKey == key) {
        return &element;
    }
    for (const auto& child : element.getChildren()) {
        if (child) {
            if (UIElement* found = findByKey(*child, key)) {
                return found;
            }
        }
    }
    return nullptr;
}

/// The bindings the dialog REQUIRES (it refuses to open without them). Tests
/// that only care about one section still have to satisfy this contract.
FEditorSettingsBindings requiredBindings(int& sampler, bool& overlay, std::string& path, bool& dirty, bool& exists)
{
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
    return bindings;
}

} // namespace

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
    tree.tick(1.0f / 60.0f);
    EXPECT_FALSE(dialog.isApplyEnabled());

    path = "Castle.scene.json";
    dirty = true;
    tree.tick(1.0f / 60.0f);
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

TEST(EditorSettingsDialogTest, FontFacePickReportsCatalogIdNotIndex)
{
    const std::vector<ui_font_settings::FOption> options = {
        {.id = "inter", .label = "Inter (bundled)", .bAvailable = true},
        {.id = "system", .label = "System UI", .bAvailable = false},
    };
    std::string              face = "inter";
    std::vector<std::string> applied;
    int  sampler = 0;
    bool overlay = false;
    bool dirty   = false;
    bool exists  = false;
    std::string path;

    FEditorSettingsBindings bindings = requiredBindings(sampler, overlay, path, dirty, exists);
    bindings.fontOptions = [&]() { return options; };
    bindings.fontFace = [&]() { return face; };
    bindings.setFontFace = [&](std::string id) { applied.push_back(std::move(id)); };

    WidgetTree tree({.width = 800, .height = 600});
    EditorSettingsDialog dialog;
    dialog.open(tree, bindings);
    ASSERT_TRUE(dialog.isOpen());
    tree.tick(1.0f / 60.0f);

    UIElement* root = tree.getRoot();
    ASSERT_NE(root, nullptr);
    auto* combo = dynamic_cast<UIComboBox*>(findByKey(*root, "EditorSettingsFontFace"));
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->_items.size(), 2u);
    // An unavailable face stays in the list (the choices must not shift under the
    // user) and is marked in the label rather than silently dropped.
    EXPECT_NE(combo->_items[1].find("unavailable"), std::string::npos);
    EXPECT_EQ(combo->_selectedIndex, 0);

    // The combo reports an INDEX; the dialog must translate it back to the
    // catalog id - only the id survives a restart, and an index shifts whenever
    // the catalog grows.
    combo->setSelectedIndex(1);
    ASSERT_EQ(applied.size(), 1u);
    EXPECT_EQ(applied.front(), "system");
}

TEST(EditorSettingsDialogTest, MissingFontOptionsStillOpensTheDialog)
{
    // A host that does not expose font settings must not take the dialog down
    // with it: the face row is optional, the rest of the panel is not.
    int  sampler = 0;
    bool overlay = false;
    bool dirty   = false;
    bool exists  = false;
    std::string path;

    WidgetTree tree({.width = 800, .height = 600});
    EditorSettingsDialog dialog;
    dialog.open(tree, requiredBindings(sampler, overlay, path, dirty, exists));
    ASSERT_TRUE(dialog.isOpen());
    tree.tick(1.0f / 60.0f);

    UIElement* root = tree.getRoot();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(findByKey(*root, "EditorSettingsFontFace"), nullptr);
}

} // namespace ya
