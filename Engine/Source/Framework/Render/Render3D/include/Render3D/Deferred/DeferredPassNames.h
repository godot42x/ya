#pragma once

#include <string_view>

namespace ya
{

/// Graph pass names of the Deferred pipeline's composition. These strings are
/// the topology's vocabulary (diagnostics, tests, tooling), so the features that
/// own the passes and the passes that remain pipeline-tail read them from one
/// place instead of repeating literals.
namespace deferred_pass_names
{

inline constexpr std::string_view kGBuffer           = "Deferred GBuffer";
inline constexpr std::string_view kLight             = "Deferred Light";
inline constexpr std::string_view kSSAO              = "SSAO Pass";
inline constexpr std::string_view kSSR               = "Deferred SSR";
inline constexpr std::string_view kForwardOpaque     = "Deferred Forward Opaque";
inline constexpr std::string_view kSkybox            = "Deferred Skybox";
inline constexpr std::string_view kSprites           = "Deferred Sprites";
inline constexpr std::string_view kForwardTransparent = "Deferred Forward Transparent";
inline constexpr std::string_view kEntityId          = "Deferred EntityId";

/// Diagnostic import labels of feature-owned targets. Features are shared
/// between pipelines, so the pipeline that owns the map states the vocabulary.
inline constexpr std::string_view kEntityIdImport       = "DeferredView.EntityId";
inline constexpr std::string_view kDisplayImport        = "DeferredView.Display";
inline constexpr std::string_view kBloomImportPrefix    = "Deferred.";

} // namespace deferred_pass_names

/// Graph export names of the Deferred view's public outputs. These are the
/// publication contract the pipeline's output collector reads; features export
/// their own diagnostics (Bloom/Postprocessing) separately.
namespace deferred_graph_exports
{

inline constexpr std::string_view gBufferColor[4] = {
    "Deferred.GBuffer.Color0",
    "Deferred.GBuffer.Color1",
    "Deferred.GBuffer.Color2",
    "Deferred.GBuffer.Color3",
};
inline constexpr std::string_view gBufferDepth = "Deferred.GBuffer.Depth";
inline constexpr std::string_view viewColor    = "Deferred.View.Color";
inline constexpr std::string_view ssao         = "Deferred.SSAO.Output";
inline constexpr std::string_view entityId     = "Deferred.View.EntityId";

} // namespace deferred_graph_exports

} // namespace ya
