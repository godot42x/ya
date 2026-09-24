#pragma once

#include <cstdint>

namespace ya
{

/// Identity of one presentation surface inside one device's surface registry.
///
/// A surface is one OS window's present destination (swapchain + acquire/
/// present sync). It is not a viewport, not a Scene and not a rank: the device
/// keeps every surface in one registry, and a caller that wants to present,
/// query or destroy one names it by this id.
///
/// `index` names the registry slot and `generation` names which tenant of that
/// slot the id refers to, so an id kept across a destroy/recreate cycle fails to
/// resolve instead of quietly naming the surface that took the slot's place.
/// The default value is invalid (`generation == 0`), which is what a caller
/// holds before it has registered anything.
struct SurfaceId
{
    uint32_t index      = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool valid() const { return generation != 0; }

    bool operator==(const SurfaceId&) const = default;
};

} // namespace ya
