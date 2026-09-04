#pragma once

#include "Render3D/Common/RenderViewportSnapshot.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ya
{

[[nodiscard]] inline uint32_t debugGroupPreviewSlotIndex(uint32_t beginIndex,
                                                         uint32_t groupSize,
                                                         uint32_t selectedGroup,
                                                         uint32_t selectedSlot)
{
    return beginIndex + selectedGroup * groupSize + selectedSlot;
}

[[nodiscard]] inline std::vector<int> debugGroupIndicesForCategory(const RenderViewportDebugCatalog& catalog,
                                                                   int categoryFilter)
{
    std::vector<int> indices;
    indices.reserve(catalog.groups.size());
    for (int groupIndex = 0; groupIndex < static_cast<int>(catalog.groups.size()); ++groupIndex) {
        if (categoryFilter >= 0 && static_cast<int>(catalog.groups[static_cast<size_t>(groupIndex)].categoryIndex) != categoryFilter) {
            continue;
        }
        indices.push_back(groupIndex);
    }
    return indices;
}

[[nodiscard]] inline std::vector<int> debugStandaloneSlotIndices(const RenderViewportDebugCatalog& catalog,
                                                                int categoryFilter)
{
    std::vector<uint8_t> grouped(catalog.slots.size(), 0);
    for (const auto& group : catalog.groups) {
        if (categoryFilter >= 0 && static_cast<int>(group.categoryIndex) != categoryFilter) {
            continue;
        }
        const uint32_t slotEnd = std::min(group.beginIndex + group.slotCount, static_cast<uint32_t>(catalog.slots.size()));
        for (uint32_t slotIndex = group.beginIndex; slotIndex < slotEnd; ++slotIndex) {
            grouped[slotIndex] = 1;
        }
    }

    std::vector<int> indices;
    indices.reserve(catalog.slots.size());
    for (int slotIndex = 0; slotIndex < static_cast<int>(catalog.slots.size()); ++slotIndex) {
        if (grouped[static_cast<size_t>(slotIndex)] != 0) {
            continue;
        }
        if (categoryFilter >= 0 && static_cast<int>(catalog.slots[static_cast<size_t>(slotIndex)].categoryIndex) != categoryFilter) {
            continue;
        }
        indices.push_back(slotIndex);
    }
    return indices;
}

} // namespace ya
