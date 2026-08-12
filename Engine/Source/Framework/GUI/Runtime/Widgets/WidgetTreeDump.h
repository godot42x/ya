#pragma once

// a11y-style structural dump of a live WidgetTree: layers + subtree as
// deterministic JSON (name / typeId / rect / visibility / zOrder / hit /
// focus policy / focused / hovered / captured / children).

#include "Core/Api.h"

#include <nlohmann/json.hpp>

namespace ya
{

struct WidgetTree;

YA_GUI_API nlohmann::json dumpWidgetTree(const WidgetTree& tree);

YA_GUI_API const nlohmann::json* findWidgetNode(const nlohmann::json& root,
                                                const std::string& name);

} // namespace ya
