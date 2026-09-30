#pragma once

#include <cstdint>
#include <memory>

namespace ya
{

enum class EAssetSlotState : uint8_t
{
    Loading = 0,
    Ready,
    Failed,
};

// One loaded asset, shared by every ref that names it. The resource layer
// owns and fills the slot on the game thread; refs only read it, so a load
// completing (or a reload replacing the resource) is visible to all of them
// at once without anyone polling.
//
// Ready implies a non-null resource. A reload keeps the slot Ready with the
// previous resource until the replacement is uploaded. generation advances
// every time the slot is filled (ready, failed or replaced).
template <typename T>
struct AssetSlot
{
    EAssetSlotState    state = EAssetSlotState::Loading;
    std::shared_ptr<T> resource;
    uint64_t           generation = 0;
};

template <typename T>
using AssetHandle = std::shared_ptr<const AssetSlot<T>>;

} // namespace ya
