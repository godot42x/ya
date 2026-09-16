#pragma once

#include "GameEditor/UI/EditorFilePicker.h"
#include "GameRuntime/Utility/UiFontSettings.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct UIButton;
struct UICheckBox;
struct UIComboBox;
struct UIContainer;
struct UIBorder;
struct UIPopupOverlay;
struct UIText;
struct UITextField;
struct WidgetTree;

struct FEditorSettingsBindings
{
    std::function<int()> samplerIndex;
    std::function<void(int)> setSamplerIndex;
    std::function<bool()> showCameraOverlay;
    std::function<void(bool)> setShowCameraOverlay;
    std::function<std::string()> scenePathDraft;
    std::function<void(std::string)> setScenePathDraft;
    std::function<bool()> scenePathDirty;
    std::function<bool()> scenePathExists;
    std::function<void()> applyScenePath;
    std::function<void()> resetScenePath;
    /// UI face, by catalog id. The dialog renders the list and reports the pick;
    /// applying it means reloading the font stack, so the dialog cannot resolve
    /// the choice itself (it has no render backend) - the surface does.
    std::function<std::vector<ui_font_settings::FOption>()> fontOptions;
    std::function<std::string()> fontFace;
    std::function<void(std::string)> setFontFace;
    EditorFilePickerCallback openFilePicker;
};

struct FEditorSettingsScenePathStatus
{
    const char* styleKey = "text.muted";
    const char* text = "";
};

[[nodiscard]] inline FEditorSettingsScenePathStatus describeEditorSettingsScenePath(const std::string& path,
                                                                                    bool exists)
{
    if (path.empty()) {
        return {"text.muted", "Empty means startup falls back to an empty scene"};
    }
    if (exists) {
        return {"text.muted", "Used on next app start — scene exists"};
    }
    return {"text.error", "Used on next app start — scene not found"};
}

/// Retained editor-settings modal. EditorSurface hosts it but does not own
/// the overlay or the sampler/overlay/startup-scene controls.
class EditorSettingsDialog
{
  public:
    void open(WidgetTree& tree, FEditorSettingsBindings bindings);
    void sync(WidgetTree& tree);
    void close();
    void reset();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] bool isApplyEnabled() const;
    void browseStartupScene();

  private:
    FEditorSettingsBindings _bindings;
    std::shared_ptr<UIPopupOverlay> _overlay;
    std::shared_ptr<UIBorder> _panel;
    std::shared_ptr<UIContainer> _settingsRoot;
    std::shared_ptr<UIComboBox> _samplerCombo;
    std::shared_ptr<UIComboBox> _fontCombo;
    std::vector<std::string>    _fontOptionIds;
    std::shared_ptr<UICheckBox> _overlayCheckbox;
    std::shared_ptr<UITextField> _scenePathField;
    std::shared_ptr<UIText> _sceneStatusText;
    std::shared_ptr<UIButton> _applyButton;
    std::shared_ptr<UIButton> _resetButton;
};

} // namespace ya
