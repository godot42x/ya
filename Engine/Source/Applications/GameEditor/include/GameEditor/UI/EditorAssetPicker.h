#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ya
{

enum class EEditorAssetPickerKind : uint8_t
{
    Texture = 0,
    Model,
    Mesh,
};

/// Host callback for retained asset-reference rows. Widgettree chrome wires this
/// to EditorSurface::openAssetPickerDialog; legacy imgui chrome falls back to FilePicker.
using EditorAssetPickerCallback = std::function<void(EEditorAssetPickerKind kind,
                                                       std::string currentPath,
                                                       std::function<void(std::string newPath)> onPicked)>;

} // namespace ya
