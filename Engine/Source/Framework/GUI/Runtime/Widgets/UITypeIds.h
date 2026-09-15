#pragma once

// ============================================================================
// UITypeIds - single source of truth for built-in widget typeId strings.
//
// Both the UITypeRegistry built-in registration (UITypeRegistry.cpp) and the
// declarative builder constructors reference these constants, so a control
// rename is a one-line change and string drift between the two sites is
// impossible. Header-only, zero includes: safe to pull into any TU without
// creating a dependency cycle.
// ============================================================================

#include <string>

namespace ya
{

inline constexpr const char* kTypeIdCanvasPanel   = "engine.panel";
inline constexpr const char* kTypeIdBorder         = "engine.border";
inline constexpr const char* kTypeIdText           = "engine.text";
inline constexpr const char* kTypeIdButton         = "engine.button";
inline constexpr const char* kTypeIdContainer      = "engine.container";
inline constexpr const char* kTypeIdTextField      = "engine.text_field";
inline constexpr const char* kTypeIdSplitPane      = "engine.split_pane";
inline constexpr const char* kTypeIdScrollViewport = "engine.scroll_viewport";
inline constexpr const char* kTypeIdSelectableRow  = "engine.selectable_row";
inline constexpr const char* kTypeIdCheckBox       = "engine.check_box";
inline constexpr const char* kTypeIdSwitch         = "engine.switch";
inline constexpr const char* kTypeIdSlider         = "engine.slider";
inline constexpr const char* kTypeIdComboBox       = "engine.combo_box";
inline constexpr const char* kTypeIdImage          = "engine.image";
inline constexpr const char* kTypeIdMenuBar        = "engine.menu_bar";
inline constexpr const char* kTypeIdMenu           = "engine.menu";
inline constexpr const char* kTypeIdTabBar         = "engine.tab_bar";
inline constexpr const char* kTypeIdOverlay        = "engine.overlay";
inline constexpr const char* kTypeIdSizeBox        = "engine.size_box";
inline constexpr const char* kTypeIdTreeView       = "engine.tree_view";
inline constexpr const char* kTypeIdExpander       = "engine.expander";
inline constexpr const char* kTypeIdDockSpace      = "engine.dock_space";
inline constexpr const char* kTypeIdPopupOverlay   = "engine.popup_overlay";

} // namespace ya
