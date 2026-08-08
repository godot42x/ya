#pragma once

#include "Framework/Game/Render/Graph/RenderGraph.h"

namespace ya
{

namespace forward_graph_exports
{

inline constexpr std::string_view viewportColor   = "ForwardViewport.Color";
inline constexpr std::string_view viewportDepth   = "ForwardViewport.Depth";
inline constexpr std::string_view viewportResolve = "ForwardViewport.Resolve";
inline constexpr std::string_view entityId        = "ForwardViewport.EntityId";

} // namespace forward_graph_exports

} // namespace ya
