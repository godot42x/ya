#pragma once

#include <cstdint>

namespace ya
{

/// What a draw candidate belongs to, so a view can declare which kinds it
/// draws.
///
/// View visibility is a per-view policy (the editor world view draws gizmos, a
/// game view does not), never a field on the component that owns the geometry.
/// Generated companions stay in the ECS in every mode; they are only filtered
/// when a view binds its draw buckets.
enum class ERenderFeature : uint32_t
{
    Game  = 1u << 0, ///< Authored content, including user-authored sprites.
    Gizmo = 1u << 1, ///< Editor companion visuals (camera body, light billboard).
    Debug = 1u << 2, ///< Reserved: collision shapes, AI ranges, navigation.
};

using FRenderFeatureMask = uint32_t;

[[nodiscard]] constexpr FRenderFeatureMask toMask(ERenderFeature feature)
{
    return static_cast<FRenderFeatureMask>(feature);
}

/// A draw item is visible in a view when their masks intersect.
[[nodiscard]] constexpr bool rendersFeature(FRenderFeatureMask itemFeatures, FRenderFeatureMask viewFeatures)
{
    return (itemFeatures & viewFeatures) != 0;
}

} // namespace ya
