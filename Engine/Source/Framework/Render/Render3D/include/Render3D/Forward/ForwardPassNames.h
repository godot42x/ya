#pragma once

#include <string_view>

namespace ya
{

/// Graph pass names of the Forward pipeline's composition. These strings are the
/// topology's vocabulary (diagnostics, tests, tooling), so the features that own
/// the passes read them from one place instead of repeating literals.
namespace forward_pass_names
{

inline constexpr std::string_view kOpaque      = "Forward Opaque";
inline constexpr std::string_view kSkybox      = "Forward Skybox";
inline constexpr std::string_view kTransparent = "Forward Transparent";
inline constexpr std::string_view kEntityId    = "Forward EntityId";

/// Diagnostic import labels of feature-owned targets. Features are shared
/// between pipelines, so the pipeline that owns the map states the vocabulary.
inline constexpr std::string_view kEntityIdImport    = "ForwardView.EntityId";
inline constexpr std::string_view kDisplayImport     = "ForwardView.Display";
inline constexpr std::string_view kBloomImportPrefix = "ForwardView.";

} // namespace forward_pass_names

/// Graph export names of the Forward view's public outputs. These are the
/// publication contract the pipeline's output collector reads; features export
/// their own diagnostics (Bloom/Postprocessing) separately.
namespace forward_graph_exports
{

inline constexpr std::string_view viewColor   = "ForwardView.Color";
inline constexpr std::string_view viewDepth   = "ForwardView.Depth";
inline constexpr std::string_view viewResolve = "ForwardView.Resolve";
inline constexpr std::string_view entityId    = "ForwardView.EntityId";

} // namespace forward_graph_exports

} // namespace ya
