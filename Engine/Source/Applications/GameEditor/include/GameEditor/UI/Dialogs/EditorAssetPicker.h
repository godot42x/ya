#pragma once

#include "Core/TypeIndex.h"

#include <functional>
#include <string>

namespace ya
{

/// Host callback for retained asset-reference rows. `refType` is the asset
/// ref's type index; the picker title and extensions come from AssetTypeRegistry.
/// Widgettree chrome wires this to EditorSurface::openAssetPickerDialog; legacy
/// imgui chrome falls back to FilePicker.
using EditorAssetPickerCallback = std::function<void(type_index_t refType,
                                                       std::string currentPath,
                                                       std::function<void(std::string newPath)> onPicked)>;

/// Reveal an asset path in the Content Browser (VFS or filesystem path).
using EditorRevealAssetCallback = std::function<void(std::string vfsPath)>;

} // namespace ya
