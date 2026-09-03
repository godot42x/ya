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

/// Host callback for retained asset-reference rows. The inspector tab wires this
/// to the existing ImGui FilePicker; PropertyHandle stays free of editor UI.
using EditorAssetPickerCallback = std::function<void(EEditorAssetPickerKind kind,
                                                       std::string currentPath,
                                                       std::function<void(std::string newPath)> onPicked)>;

} // namespace ya
