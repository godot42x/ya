#pragma once

// ============================================================================
// EditorTheme - GameEditor chrome theme content.
//
// Mechanism (UITheme / resolveThemeStyle / generation token) lives in the
// GUI framework. This file owns the VALUES the editor chrome resolves.
// Family keys match the shared chrome palette (`panel.window`, `text.header`,
// `tree`, ...); `editor.*` is the same catalog with an overlay when editor
// density diverges (type scale, inspector field padding). Do not scatter
// `setFontSize` on GameEditor chrome — pick a family key or `editor.<key>`.
// ============================================================================

#include "GUI/Widgets/DefaultChromeTheme.h"

#include <string>
#include <string_view>
#include <utility>

namespace ya
{

/// Editor type scale. Family `gui_type` stays gallery/body (16/28); this is
/// the dense chrome baked into `editor.*` and the editor-tree text roles.
namespace editor_type
{
inline constexpr uint32_t kHeader  = 14;
inline constexpr uint32_t kBody    = 12;
inline constexpr uint32_t kSmall   = 12;
inline constexpr uint32_t kCaption = 11;
}

[[nodiscard]] inline std::string editorStyle(std::string_view familyKey)
{
    std::string key = "editor.";
    key.append(familyKey);
    return key;
}

/// Shared chrome metrics. Inspector rows, list rows, and the editor toolbar
/// read these instead of scattering 8/12/22/26 literals.
namespace editor_density
{
inline constexpr float kRowHeight         = 22.0f;
inline constexpr float kLabelColumn       = 140.0f;
inline constexpr float kRowSpacing        = 6.0f;
inline constexpr float kControlSpacing    = 6.0f;
inline constexpr float kPanelPadding      = 8.0f;
inline constexpr float kSectionSpacing    = 10.0f;
inline constexpr float kGroupHeaderHeight = 18.0f;
inline constexpr float kToolbarHeight     = 30.0f;
inline constexpr float kMenuHeight        = 24.0f;
/// Play dock split: inner tab strip + toolbar row. Pixel min only; ratio is
/// unchanged so the user can still drag extra space onto the Viewport.
inline constexpr float kPlayToolbarDockMin = kToolbarHeight + kMenuHeight;
inline constexpr float kListRowHeight     = 22.0f;
inline constexpr float kToolbarIconSize   = 16.0f;
inline constexpr float kListIconSize      = 16.0f;
inline constexpr float kGridThumbSize     = 72.0f;
inline constexpr float kGridLabelHeight   = 28.0f;
inline constexpr float kBrowseButtonWidth = 64.0f;
inline constexpr float kLocateButtonWidth = 48.0f;
inline constexpr float kAssetThumbSize    = 64.0f;
inline constexpr float kColorRowHeight    = kRowHeight;
inline constexpr float kChromeInset       = 1.0f;
inline constexpr float kDebugPreviewHeight = 180.0f;
inline constexpr glm::vec2 kFieldPadding  = {4.0f, 2.0f};
}

namespace editor_theme_detail
{
struct FEditorStyleBake
{
    UITheme& theme;

    template <typename TStyle>
    TStyle copyFamily(std::string_view familyKey) const
    {
        TStyle style{};
        if (auto found = theme.find<TStyle>(std::string(familyKey))) {
            style = found->value();
        }
        return style;
    }

    template <typename TStyle>
    void defineFamilyAndEditor(std::string_view familyKey, TStyle style) const
    {
        theme.define<TStyle>(std::string(familyKey), style);
        theme.define<TStyle>(editorStyle(familyKey), std::move(style));
    }

    template <typename TStyle>
    void defineEditorOnly(std::string_view familyKey, TStyle style) const
    {
        theme.define<TStyle>(editorStyle(familyKey), std::move(style));
    }

    void restyleText(std::string_view family, uint32_t fontSize) const
    {
        FTextStyle style = copyFamily<FTextStyle>(family);
        style.fontSize   = fontSize;
        defineFamilyAndEditor(family, std::move(style));
    }
};
} // namespace editor_theme_detail

inline std::shared_ptr<UITheme> buildEditorTheme(bool bDark)
{
    auto theme = buildDefaultChromeTheme(bDark);
    const editor_theme_detail::FEditorStyleBake bake{*theme};

    bake.restyleText(StyleKey::TextHeader, editor_type::kHeader);
    bake.restyleText(StyleKey::TextMuted, editor_type::kBody);
    bake.restyleText(StyleKey::TextSmall, editor_type::kSmall);
    bake.restyleText(StyleKey::TextEyebrow, editor_type::kCaption);
    bake.restyleText(StyleKey::TextError, editor_type::kBody);
    bake.restyleText(StyleKey::TextCaption, editor_type::kCaption);

    FTextStyle body = bake.copyFamily<FTextStyle>(StyleKey::Text);
    body.fontSize   = editor_type::kBody;
    body.padding    = {0.0f, 0.0f};
    body.fillColor  = FBrush::solid({0.0f, 0.0f, 0.0f, 0.0f});
    bake.defineEditorOnly(StyleKey::Text, std::move(body));

    FTextFieldStyle field = bake.copyFamily<FTextFieldStyle>(StyleKey::TextField);
    field.fontSize        = editor_type::kBody;
    field.padding         = editor_density::kFieldPadding;
    bake.defineEditorOnly(StyleKey::TextField, field);
    bake.defineEditorOnly(StyleKey::TextFieldCompact, field);

    FDragFloatStyle drag = bake.copyFamily<FDragFloatStyle>(StyleKey::DragFloat);
    drag.fontSize        = editor_type::kBody;
    drag.padding         = editor_density::kFieldPadding;
    bake.defineEditorOnly(StyleKey::DragFloat, std::move(drag));

    FColorEditStyle color = bake.copyFamily<FColorEditStyle>(StyleKey::ColorEdit);
    color.fontSize        = editor_type::kBody;
    color.padding         = editor_density::kFieldPadding;
    bake.defineEditorOnly(StyleKey::ColorEdit, std::move(color));

    FComboBoxStyle combo = bake.copyFamily<FComboBoxStyle>(StyleKey::ComboBox);
    combo.fontSize       = editor_type::kBody;
    bake.defineEditorOnly(StyleKey::ComboBox, std::move(combo));

    FSpinBoxStyle spin = bake.copyFamily<FSpinBoxStyle>(StyleKey::SpinBox);
    spin.fontSize      = editor_type::kBody;
    bake.defineEditorOnly(StyleKey::SpinBox, std::move(spin));

    FSearchComboStyle search = bake.copyFamily<FSearchComboStyle>(StyleKey::SearchCombo);
    search.fontSize          = editor_type::kBody;
    bake.defineEditorOnly(StyleKey::SearchCombo, std::move(search));

    FExpanderStyle expander = bake.copyFamily<FExpanderStyle>(StyleKey::Expander);
    expander.fontSize       = editor_type::kBody;
    bake.defineFamilyAndEditor(StyleKey::Expander, expander);
    FExpanderStyle expanderHeader = bake.copyFamily<FExpanderStyle>(StyleKey::ExpanderHeader);
    expanderHeader.fontSize       = editor_type::kBody;
    bake.defineFamilyAndEditor(StyleKey::ExpanderHeader, std::move(expanderHeader));

    FTreeViewStyle tree = bake.copyFamily<FTreeViewStyle>(StyleKey::Tree);
    tree.fontSize       = editor_type::kBody;
    bake.defineFamilyAndEditor(StyleKey::Tree, std::move(tree));

    FMenuStyle menu = bake.copyFamily<FMenuStyle>(StyleKey::Menu);
    menu.fontSize   = editor_type::kBody;
    bake.defineFamilyAndEditor(StyleKey::Menu, std::move(menu));
    return theme;
}

/// Editor chrome icon assets (same files `EditorLayer::onAttach` loads).
namespace editor_icons
{
inline constexpr const char* kPlay     = "Engine/Content/TestTextures/editor/play.png";
inline constexpr const char* kStop     = "Engine/Content/TestTextures/editor/stop.png";
inline constexpr const char* kSimulate = "Engine/Content/TestTextures/editor/simulate_button.png";
inline constexpr const char* kFolder   = "Engine/Content/TestTextures/editor/folder2.png";
inline constexpr const char* kFile     = "Engine/Content/TestTextures/editor/file.png";
}

} // namespace ya
