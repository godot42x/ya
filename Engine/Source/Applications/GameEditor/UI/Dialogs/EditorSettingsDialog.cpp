#include "GameEditor/UI/Dialogs/EditorSettingsDialog.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Shell/EditorListRows.h"

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
                            .setStyleKey(editorStyle(StyleKey::ComboBox))
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
                              .setStyleKey(editorStyle(StyleKey::TextField))
                              .setText(_bindings.scenePathDraft())
                              .setOnTextChanged([this](const std::string& text) {
                                  if (_bindings.setScenePathDraft) {
                                      _bindings.setScenePathDraft(text);
                                  }
                              });
    _scenePathField = scenePathField.share();

    // Face picker. Options come from the binding, not from here: the dialog is
    // the view, and "which faces exist / which are installed" is app policy.
    std::vector<ui_font_settings::FOption> faceOptions;
    if (_bindings.fontOptions) {
        faceOptions = _bindings.fontOptions();
    }
    std::vector<std::string> faceLabels;
    faceLabels.reserve(faceOptions.size());
    _fontOptionIds.clear();
    int selectedFace = 0;
    const std::string currentFace = _bindings.fontFace ? _bindings.fontFace() : std::string{};
    for (size_t i = 0; i < faceOptions.size(); ++i) {
        const ui_font_settings::FOption& option = faceOptions[i];
        // Say WHY a face cannot be picked: a face that vanishes from the list
        // would read as "the setting was lost" rather than "the file is not on
        // this machine".
        faceLabels.push_back(option.bAvailable ? option.label : option.label + "  (unavailable)");
        _fontOptionIds.push_back(option.id);
        if (option.id == currentFace) {
            selectedFace = static_cast<int>(i);
        }
    }
    auto fontCombo = ui::comboBox("EditorSettingsFontFace")
                         .setStyleKey(editorStyle(StyleKey::ComboBox))
                         .setItems(std::move(faceLabels))
                         .setSelectedIndex(selectedFace)
                         .setOnSelectionChanged([this](int index) {
                             if (!_bindings.setFontFace) {
                                 return;
                             }
                             if (index >= 0 && index < static_cast<int>(_fontOptionIds.size())) {
                                 _bindings.setFontFace(_fontOptionIds[static_cast<size_t>(index)]);
                             }
                         });
    // The row is mounted conditionally below, so keep the handle only when this
    // dialog actually shows it - sync() must not poke a control that no tree
    // ever sees.
    if (!faceOptions.empty()) {
        _fontCombo = fontCombo.share();
    }
    auto fontRow = ui::row("EditorSettingsFontRow")
                       .setSpacing(8.0f)
                       .setStretchLastChild(true)
                       .child(ui::text("EditorSettingsFontLabel")
                                  .setText("UI Font")
                                  .setStyleKey("text.muted")
                                  .setVAlign(EWidgetAlignV::Center),
                              ui::boxSlot().preferredSize({140.0f, 26.0f}))
                       .child(std::move(fontCombo), ui::boxSlot().preferredSize({240.0f, 26.0f}));

    auto sceneStatusText = ui::text("EditorSettingsSceneStatus").setStyleKey("text.muted");
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
                           _scenePathField->setText(_bindings.scenePathDraft());
                       })
                       .share();

    auto sceneRow = ui::row("EditorSettingsSceneRow")
                        .setSpacing(6.0f)
                        .setStretchLastChild(true)
                        .child(ui::text("EditorSettingsSceneLabel")
                                   .setText("Startup Scene")
                                   .setStyleKey("text.muted")
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
                                       .setStyleKey("text.header"))
                            .child(ui::row("EditorSettingsSamplerRow")
                                       .setSpacing(8.0f)
                                       .setStretchLastChild(true)
                                       .child(ui::text("EditorSettingsSamplerLabel")
                                                  .setText("Viewport Sampler")
                                                  .setStyleKey("text.muted")
                                                  .setVAlign(EWidgetAlignV::Center),
                                              ui::boxSlot().preferredSize({140.0f, 26.0f}))
                            .child(std::move(samplerCombo), ui::boxSlot().preferredSize({160.0f, 26.0f})))
                            // A host with no font catalog gets no row at all: an
                            // empty picker reads as a broken dialog, not as "this
                            // application has no font setting".
                            .child(ui::when(!faceOptions.empty(), std::move(fontRow)))
                            .child(std::move(overlayCheckbox))
                            .child(std::move(sceneRow), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                            .child(std::move(sceneStatusText))
                            .child(std::move(sceneActions))
                            .child(labeledButton("EditorSettingsClose", "Close")
                                       .setOnClick([this]() { close(); }),
                                   ui::boxSlot().preferredSize({84.0f, 26.0f}));
    _settingsRoot = settingsRoot.share();
    auto dialogPanel = ui::border("EditorSettingsPanel")
                           .setStyleKey("panel.window")
                           .child(ui::scroll("EditorSettingsScroll")
                                      .setAxis(EScrollAxis::Vertical)
                                      .child(std::move(settingsRoot), ui::contentSlot().fill()),
                                  ui::contentSlot().fill());

    _panel = dialogPanel.share();
    _overlay = ui::popupOverlay("EditorSettingsOverlay")
                   .setRole(UIPopupOverlay::EOverlayRole::Modal)
                   .setOnDismiss([this]() { reset(); })
                   .child(std::move(dialogPanel))
                   .share();
    // Refresh from the tree's own tick, not from the shell: an open dialog is
    // visible, so the subtree walk reaches it and the shell only has to open and
    // close it. Nothing outside this file needs to know it has per-frame work.
    auto tick = std::make_shared<UITickBehavior>();
    tick->onTick = [this](UIElement& owner, float) {
        if (WidgetTree* ownerTree = owner.getTree()) {
            sync(*ownerTree);
        }
    };
    _overlay->addBehavior(std::move(tick));
    _overlay->open(tree);
}

void EditorSettingsDialog::sync(WidgetTree& tree)
{
    if (!_overlay || !_panel) {
        return;
    }

    const Extent2D logicalExtent = tree.getLogicalExtent();
    const glm::vec2 extent = {static_cast<float>(logicalExtent.width), static_cast<float>(logicalExtent.height)};
    const glm::vec2 desired = _settingsRoot->computeDesiredSize();
    const glm::vec2 size = {
        std::min(desired.x, std::max(1.0f, extent.x - 32.0f)),
        std::min(desired.y, std::max(1.0f, extent.y - 32.0f)),
    };
    _overlay->_contentExtent = size;
    _overlay->_contentPos = {
        std::max(0.0f, (extent.x - size.x) * 0.5f),
        std::max(0.0f, (extent.y - size.y) * 0.5f),
    };

    if (_bindings.samplerIndex) {
        _samplerCombo->setSelectedIndex(_bindings.samplerIndex(), false);
    }
    if (_bindings.fontFace && _fontCombo) {
        const std::string face = _bindings.fontFace();
        for (size_t i = 0; i < _fontOptionIds.size(); ++i) {
            if (_fontOptionIds[i] == face) {
                _fontCombo->setSelectedIndex(static_cast<int>(i), false);
                break;
            }
        }
    }
    if (_bindings.showCameraOverlay) {
        _overlayCheckbox->setChecked(_bindings.showCameraOverlay());
    }
    const bool dirty = _bindings.scenePathDirty && _bindings.scenePathDirty();
    _applyButton->setEnabled(dirty);
    _resetButton->setEnabled(dirty);
    if (_bindings.scenePathDraft && _bindings.scenePathExists) {
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
    _settingsRoot.reset();
    _samplerCombo.reset();
    _fontCombo.reset();
    _fontOptionIds.clear();
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
            _scenePathField->setText(_bindings.scenePathDraft());
        }));
}

} // namespace ya
