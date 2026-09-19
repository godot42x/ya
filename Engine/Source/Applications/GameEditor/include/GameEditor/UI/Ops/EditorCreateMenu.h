#pragma once

#include "GUI/Widgets/Controls/Menu.h"

#include <vector>

namespace ya
{

class ActionMap;
struct EditorLayer;

/// Viewport and Hierarchy share this Create menu: Empty Node, 3D Object
/// presets, and Light presets from `NodeCreateRegistry`.
[[nodiscard]] std::vector<UIMenu::FItem> makeEditorCreateMenuItems(EditorLayer& layer, ActionMap& actions);

} // namespace ya
