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
inline constexpr std::string_view kForwardOpaque     = "Deferred Forward Opaque";
inline constexpr std::string_view kSkybox            = "Deferred Skybox";
inline constexpr std::string_view kSprites           = "Deferred Sprites";
inline constexpr std::string_view kForwardTransparent = "Deferred Forward Transparent";
inline constexpr std::string_view kEntityId          = "Deferred EntityId";

} // namespace deferred_pass_names

} // namespace ya
