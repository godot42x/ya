#include "GameEditor/UI/EditorSettingsDialog.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/EditorListRows.h"

#include <algorithm>

namespace ya
{

void EditorSettingsDialog::open(WidgetTree& tree, FEditorSettingsBindings bindings)
{
    if (!bindings.samplerIndex || !bindings.setSamplerIndex || !bindings.showCameraOverlay ||
        !bindings.setShowCameraOverlay || !bindings.scenePathDraft || !bindings.setScenePathDraft ||
        !bindings.scenePathDirty || !bindings.scenePathExists || !bindings.applyScenePath ||
        !bindings.resetScenePath)
    {
        return;
    }
    if (_overlay && _overlay->isAttached()) {
        return;
    }

    reset();
    _bindings = std::move(bindings);

    auto samplerCombo = ui::comboBox("EditorSettingsSampler")
                            .setItems({"Linear", "Nearest"})
                            .setSelectedIndex(_bindings.samplerIndex())
                            .setOnSelectionChanged([this](int index) {
                                if (_bindings.setSamplerIndex) {
                                    _bindings.setSamplerIndex(index);
                                }
                            });
    _samplerCombo = samplerCombo.share();

    auto overlayCheckbox = ui::checkBox("EditorSettingsCameraOverlay")
                               .setText("Show Viewport Camera Overlay")
                               .setChecked(_bindings.showCameraOverlay())
                               .setOnChanged([this](bool checked) {
                                   if (_bindings.setShowCameraOverlay) {
                                       _bindings.setShowCameraOverlay(checked);
                                   }
                               });
    _overlayCheckbox = overlayCheckbox.share();

    auto scenePathField = ui::textField("EditorSettingsScenePath")
                              .setText(_bindings.scenePathDraft())
                              .setOnTextChanged([this](const std::string& text) {
                                  if (_bindings.setScenePathDraft) {
                                      _bindings.setScenePathDraft(text);
                                  }
                              });
    _scenePathField = scenePathField.share();

    auto sceneStatusText = ui::text("EditorSettingsSceneStatus").setFontSize(12).setStyleKey("text.muted");
    _sceneStatusText = sceneStatusText.share();

    _applyButton = labeledButton("EditorSettingsApply", "Apply Default Scene Path")
                       .setOnClick([this]() {
                           if (_bindings.applyScenePath) {
                               _bindings.applyScenePath();
                           }
                       })
                       .share();
    _resetButton = labeledButton("EditorSettingsReset", "Reset")
                       .setOnClick([this]() {
                           if (_bindings.resetScenePath) {
                               _bindings.resetScenePath();
                           }
                           if (_scenePathField && _bindings.scenePathDraft) {
                               _scenePathField->setText(_bindings.scenePathDraft());
                           }
                       })
                       .share();

    auto sceneRow = ui::row("EditorSettingsSceneRow")
                        .setSpacing(6.0f)
                        .setStretchLastChild(true)
                        .child(ui::text("EditorSettingsSceneLabel")
                                   .setText("Startup Scene")
                                   .setFontSize(12)
                                   .setVAlign(EWidgetAlignV::Center),
                               ui::boxSlot().preferredSize({120.0f, 26.0f}))
                        .child(std::move(scenePathField), ui::boxSlot().fill())
                        .child(labeledButton("EditorSettingsBrowse", "Browse")
                                   .setOnClick([this]() { browseStartupScene(); }),
                               ui::boxSlot().preferredSize({84.0f, 26.0f}));
    auto sceneActions = ui::row("EditorSettingsSceneActions")
                            .setSpacing(8.0f)
                            .child(_applyButton, ui::boxSlot().preferredSize({180.0f, 26.0f}))
                            .child(_resetButton, ui::boxSlot().preferredSize({84.0f, 26.0f}));
    auto settingsRoot = ui::column("EditorSettingsRoot")
                            .setSpacing(10.0f)
                            .setPadding({12.0f, 12.0f})
                            .child(ui::text("EditorSettingsTitle")
                                       .setText("Editor Settings")
                                       .setStyleKey("text.header")
                                       .setFontSize(14))
                            .child(ui::row("EditorSettingsSamplerRow")
                                       .setSpacing(8.0f)
                                       .setStretchLastChild(true)
                                       .child(ui::text("EditorSettingsSamplerLabel")
                                                  .setText("Viewport Sampler")
                                                  .setFontSize(12)
                                                  .setVAlign(EWidgetAlignV::Center),
                                              ui::boxSlot().preferredSize({140.0f, 26.0f}))
                                       .child(std::move(samplerCombo), ui::boxSlot().preferredSize({160.0f, 26.0f})))
                            .child(std::move(overlayCheckbox))
                            .child(std::move(sceneRow), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                            .child(std::move(sceneStatusText))
                            .child(std::move(sceneActions))
                            .child(labeledButton("EditorSettingsClose", "Close")
                                       .setOnClick([this]() { close(); }),
                                   ui::boxSlot().preferredSize({84.0f, 26.0f}));
    auto dialogPanel = ui::panel("EditorSettingsPanel")
                           .setStyleKey("panel.window")
                           .child(std::move(settingsRoot), ui::canvasSlot().fill());

    _panel = dialogPanel.share();
    _overlay = ui::popupOverlay("EditorSettingsOverlay")
                   .setRole(UIPopupOverlay::EOverlayRole::Modal)
                   .setOnDismiss([this]() { reset(); })
                   .child(std::move(dialogPanel))
                   .share();
    _overlay->open(tree);
}

void EditorSettingsDialog::sync(WidgetTree& tree)
{
    if (!_overlay || !_panel) {
        return;
    }

    const Extent2D logicalExtent = tree.getLogicalExtent();
    const glm::vec2 extent = {static_cast<float>(logicalExtent.width), static_cast<float>(logicalExtent.height)};
    const glm::vec2 desired = _panel->computeDesiredSize();
    _overlay->_contentPos = {
        std::max(0.0f, (extent.x - desired.x) * 0.5f),
        std::max(0.0f, (extent.y - desired.y) * 0.5f),
    };

    if (_samplerCombo && _bindings.samplerIndex) {
        _samplerCombo->setSelectedIndex(_bindings.samplerIndex(), false);
    }
    if (_overlayCheckbox && _bindings.showCameraOverlay) {
        _overlayCheckbox->setChecked(_bindings.showCameraOverlay());
    }
    const bool dirty = _bindings.scenePathDirty && _bindings.scenePathDirty();
    if (_applyButton) {
        _applyButton->setEnabled(dirty);
    }
    if (_resetButton) {
        _resetButton->setEnabled(dirty);
    }
    if (_sceneStatusText && _bindings.scenePathDraft && _bindings.scenePathExists) {
        const FEditorSettingsScenePathStatus status =
            describeEditorSettingsScenePath(_bindings.scenePathDraft(), _bindings.scenePathExists());
        _sceneStatusText->setStyleKey(status.styleKey);
        _sceneStatusText->setText(status.text);
    }
}

void EditorSettingsDialog::close()
{
    if (_overlay) {
        _overlay->close();
        return;
    }
    reset();
}

void EditorSettingsDialog::reset()
{
    _overlay.reset();
    _panel.reset();
    _samplerCombo.reset();
    _overlayCheckbox.reset();
    _scenePathField.reset();
    _sceneStatusText.reset();
    _applyButton.reset();
    _resetButton.reset();
    _bindings = {};
}

bool EditorSettingsDialog::isOpen() const
{
    return _overlay && _overlay->isAttached();
}

bool EditorSettingsDialog::isApplyEnabled() const
{
    return _applyButton && _applyButton->isEnabled();
}

void EditorSettingsDialog::browseStartupScene()
{
    if (!_bindings.openFilePicker || !_bindings.scenePathDraft || !_bindings.setScenePathDraft) {
        return;
    }
    _bindings.openFilePicker(makeSceneJsonFilePickerRequest(
        _bindings.scenePathDraft(),
        [this](std::string path) {
            if (_bindings.setScenePathDraft) {
                _bindings.setScenePathDraft(std::move(path));
            }
            if (_scenePathField && _bindings.scenePathDraft) {
                _scenePathField->setText(_bindings.scenePathDraft());
            }
        }));
}

} // namespace ya
