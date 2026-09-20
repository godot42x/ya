#pragma once

#include "Graph/RenderGraph.h"

namespace ya
{

namespace forward_graph_exports
{

inline constexpr std::string_view viewColor   = "ForwardView.Color";
inline constexpr std::string_view viewDepth   = "ForwardView.Depth";
inline constexpr std::string_view viewResolve = "ForwardView.Resolve";
inline constexpr std::string_view entityId        = "ForwardView.EntityId";

} // namespace forward_graph_exports

} // namespace ya
