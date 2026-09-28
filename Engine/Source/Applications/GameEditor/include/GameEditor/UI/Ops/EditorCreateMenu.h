#pragma once

#include "GUI/Widgets/Controls/Menu.h"

#include <vector>

namespace ya
{

struct EditorLayer;
struct Node;

/// Viewport and Hierarchy share this Create menu: Empty Node, then one
/// submenu per `NodeCreateRegistry` category in registration order. A new
/// registered category shows up here without touching the menu. Created
/// nodes go under `parent` (nullptr = scene root).
[[nodiscard]] std::vector<UIMenu::FItem> makeEditorCreateMenuItems(EditorLayer& layer, Node* parent = nullptr);

} // namespace ya
