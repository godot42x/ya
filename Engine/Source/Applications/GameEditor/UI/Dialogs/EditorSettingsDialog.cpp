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
#include "GameEditor/Localization/EditorLocalization.h"
#include "GameEditor/UI/Shell/EditorListRows.h"

#include <algorithm>

namespace ya
{

namespace
{
/// Two-state sampler labels. These are ComboBox item strings, which carry no
/// reactive binding, so they are resolved on demand (see relocalize) rather
/// than bound like the text labels.
std::vector<std::string> samplerLabels()
{
    return {EditorLocalization::editorTr("Linear"), EditorLocalization::editorTr("Nearest")};
}
} // namespace

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
                            .setItems(samplerLabels())
                            .setSelectedIndex(_bindings.samplerIndex())
                            .setOnSelectionChanged([this](int index) {
                                if (_bindings.setSamplerIndex) {
                                    _bindings.setSamplerIndex(index);
                                }
                            });
    _samplerCombo = samplerCombo.share();

    auto overlayCheckbox = ui::checkBox("EditorSettingsCameraOverlay")
                               .localizeText("editor.settings.camera_overlay",
                                             "Show Viewport Camera Overlay")
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
        faceLabels.push_back(option.bAvailable
                                 ? option.label
                                 : option.label + "  " +
                                       EditorLocalization::editorTr("editor.settings.font.unavailable",
                                                                    "(unavailable)"));
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
    // Labels below bind reactively (`.localize`), so they need no member: the
    // binding lives on the widget, and a language switch re-resolves it there.
    auto fontLabelText = ui::text("EditorSettingsFontLabel")
                             .localize("editor.settings.ui_font", "UI Font")
                             .setStyleKey("text.muted")
                             .setVAlign(EWidgetAlignV::Center)
                             .share();
    auto fontRow = ui::row("EditorSettingsFontRow")
                       .setSpacing(8.0f)
                       .setStretchLastChild(true)
                       .child(fontLabelText, ui::boxSlot().preferredSize({140.0f, 26.0f}))
                       .child(std::move(fontCombo), ui::boxSlot().preferredSize({240.0f, 26.0f}));

    auto sceneStatusText = ui::text("EditorSettingsSceneStatus").setStyleKey("text.muted");
    _sceneStatusText = sceneStatusText.share();

    auto applyButton = labeledButton("EditorSettingsApply",
                                     "editor.settings.apply_scene_path",
                                     "Apply Default Scene Path")
                           .setOnClick([this]() {
                               if (_bindings.applyScenePath) {
                                   _bindings.applyScenePath();
                               }
                           });
    _applyButton = applyButton.share();
    auto resetButton = labeledButton("EditorSettingsReset",
                                     "editor.settings.reset", "Reset")
                           .setOnClick([this]() {
                               if (_bindings.resetScenePath) {
                                   _bindings.resetScenePath();
                               }
                               _scenePathField->setText(_bindings.scenePathDraft());
                           });
    _resetButton = resetButton.share();

    auto sceneLabelText = ui::text("EditorSettingsSceneLabel")
                              .localize("editor.settings.startup_scene", "Startup Scene")
                              .setStyleKey("text.muted")
                              .setVAlign(EWidgetAlignV::Center)
                              .share();
    auto sceneRow = ui::row("EditorSettingsSceneRow")
                        .setSpacing(6.0f)
                        .setStretchLastChild(true)
                        .child(sceneLabelText, ui::boxSlot().preferredSize({120.0f, 26.0f}))
                        .child(std::move(scenePathField), ui::boxSlot().fill())
                        .child(labeledButton("EditorSettingsBrowse",
                                             "editor.settings.browse", "Browse")
                                   .setOnClick([this]() { browseStartupScene(); }),
                               ui::boxSlot().preferredSize({84.0f, 26.0f}));
    auto sceneActions = ui::row("EditorSettingsSceneActions")
                            .setSpacing(8.0f)
                            .child(_applyButton, ui::boxSlot().preferredSize({180.0f, 26.0f}))
                            .child(_resetButton, ui::boxSlot().preferredSize({84.0f, 26.0f}));

    // Language row. Options come from the binding (the surface owns the set of
    // selectable languages); the pick is reported back for the surface to apply
    // and persist.
    std::vector<std::string> languageIds;
    if (_bindings.languageOptions) {
        languageIds = _bindings.languageOptions();
    }
    _languageOptionIds = languageIds;
    int selectedLanguage = 0;
    const std::string currentLanguage = _bindings.language ? _bindings.language() : std::string{};
    for (size_t i = 0; i < languageIds.size(); ++i) {
        if (languageIds[i] == currentLanguage) {
            selectedLanguage = static_cast<int>(i);
        }
    }
    auto languageLabelText = ui::text("EditorSettingsLanguageLabel")
                                 .localize("editor.settings.language", "Language")
                                 .setStyleKey("text.muted")
                                 .setVAlign(EWidgetAlignV::Center)
                                 .share();
    auto languageCombo = ui::comboBox("EditorSettingsLanguage")
                             .setStyleKey(editorStyle(StyleKey::ComboBox))
                             .setItems(std::move(languageIds))
                             .setSelectedIndex(selectedLanguage)
                             .setOnSelectionChanged([this](int index) {
                                 if (!_bindings.setLanguage) {
                                     return;
                                 }
                                 if (index >= 0 && index < static_cast<int>(_languageOptionIds.size())) {
                                     _bindings.setLanguage(_languageOptionIds[static_cast<size_t>(index)]);
                                 }
                             });
    _languageCombo = languageCombo.share();
    auto languageRow = ui::row("EditorSettingsLanguageRow")
                           .setSpacing(8.0f)
                           .setStretchLastChild(true)
                           .child(languageLabelText, ui::boxSlot().preferredSize({140.0f, 26.0f}))
                           .child(std::move(languageCombo), ui::boxSlot().preferredSize({160.0f, 26.0f}));

    auto titleText = ui::text("EditorSettingsTitle")
                         .localize("editor.settings.title", "Editor Settings")
                         .setStyleKey("text.header")
                         .share();
    auto samplerLabelText = ui::text("EditorSettingsSamplerLabel")
                                .localize("editor.settings.sampler", "Viewport Sampler")
                                .setStyleKey("text.muted")
                                .setVAlign(EWidgetAlignV::Center)
                                .share();
    auto closeButton = labeledButton("EditorSettingsClose",
                                     "editor.settings.close", "Close")
                           .setOnClick([this]() { close(); });
    auto closeButtonShared = closeButton.share();

    auto settingsRoot = ui::column("EditorSettingsRoot")
                            .setSpacing(10.0f)
                            .setPadding({12.0f, 12.0f})
                            .child(titleText)
                            .child(ui::row("EditorSettingsSamplerRow")
                                       .setSpacing(8.0f)
                                       .setStretchLastChild(true)
                                       .child(samplerLabelText, ui::boxSlot().preferredSize({140.0f, 26.0f}))
                                       .child(std::move(samplerCombo), ui::boxSlot().preferredSize({160.0f, 26.0f})))
                            // Language switch, above the font row so a user
                            // changing language can immediately read the result.
                            .child(std::move(languageRow))
                            // A host with no font catalog gets no row at all: an
                            // empty picker reads as a broken dialog, not as "this
                            // application has no font setting".
                            .child(ui::when(!faceOptions.empty(), std::move(fontRow)))
                            .child(std::move(overlayCheckbox))
                            .child(std::move(sceneRow), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                            .child(std::move(sceneStatusText))
                            .child(std::move(sceneActions))
                            .child(std::move(closeButtonShared), ui::boxSlot().preferredSize({84.0f, 26.0f}));
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
    if (_bindings.language && _languageCombo) {
        const std::string language = _bindings.language();
        for (size_t i = 0; i < _languageOptionIds.size(); ++i) {
            if (_languageOptionIds[i] == language) {
                _languageCombo->setSelectedIndex(static_cast<int>(i), false);
                break;
            }
        }
    }
    // Static labels are localized reactively at build time (see `localize`), so
    // a language pick re-resolves them in place. Only the sampler combo's plain
    // string items need a nudge; relocalize() is a no-op unless they changed.
    relocalize();
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

void EditorSettingsDialog::relocalize()
{
    if (!_settingsRoot) {
        return;
    }
    // The text labels localize reactively at build time (see `localize`), so
    // they need nothing here. The ComboBox items are plain string data with no
    // reactive binding, so this dialogs' fixed option lists are the only thing
    // refreshed: the sampler's two labels.
    if (_samplerCombo) {
        _samplerCombo->_items = samplerLabels();
        _samplerCombo->markPaintDirty();
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
    _languageCombo.reset();
    _languageOptionIds.clear();
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
