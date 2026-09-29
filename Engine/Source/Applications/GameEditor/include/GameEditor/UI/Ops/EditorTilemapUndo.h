#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ya
{

class UndoStack;
struct Scene;

// One tilemap layer's cell data before or after a stroke. A stroke (press
// to release) pushes exactly one of these pairs, so one undo step always
// means one stroke however many cells it touched.
struct FTileLayerSnapshot
{
    uint64_t             entityUUID = 0;
    size_t               layerIndex = 0;
    std::vector<int32_t> cells;
};

// False when there is nothing to record (null scene, empty or identical
// snapshots); otherwise pushes one undo step restoring whole-layer cells.
bool pushTileLayerUndo(UndoStack& undo, Scene* scene, FTileLayerSnapshot before, FTileLayerSnapshot after);

} // namespace ya

