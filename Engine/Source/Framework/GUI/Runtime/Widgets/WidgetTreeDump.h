#pragma once

// a11y-style structural dump of a live WidgetTree: layers + subtree as
// deterministic JSON (name / typeId / rect / visibility / zOrder / hit /
// focus policy / focused / hovered / captured / children). Scenario
// assertions evaluate the same dump (with $gt/$gte/$lt/$lte numeric
// predicates) so windowed and headless hosts share one verdict contract.

#include "Core/Api.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace ya
{

struct WidgetTree;

YA_GUI_API nlohmann::json dumpWidgetTree(const WidgetTree& tree);

YA_GUI_API const nlohmann::json* findWidgetNode(const nlohmann::json& root,
                                                const std::string& name);

/// Assert that the JSON-serialized scenario assertion holds against the live
/// tree dump: {"widget": "<name>", ...expected fields...} checks one node,
/// otherwise the expectation is matched against the whole tree. Numeric
/// values support the predicates $gt/$gte/$lt/$lte. On failure, `error`
/// carries a path-qualified message.
YA_GUI_API bool assertScenarioTree(const WidgetTree& tree,
                                   std::string_view assertion,
                                   std::string& error);

} // namespace ya
