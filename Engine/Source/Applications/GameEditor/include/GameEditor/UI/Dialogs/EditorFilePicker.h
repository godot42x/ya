#pragma once

#include "Core/Common/AssetTypeRegistry.h"
#include "GameEditor/FileExplorer.h"
#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"

#include <functional>
#include <string>
#include <vector>

namespace ya
{

struct FEditorFilePickerRequest
{
    std::string                           title;
    std::string                           configScope;
    std::vector<std::string>              extensions;
    FileExplorer::FilterMode              filterMode    = FileExplorer::FilterMode::Both;
    FileExplorer::SelectionMode           selectionMode = FileExplorer::SelectionMode::File;
    std::string                           currentPath;
    std::function<void(std::string)>      onPicked;
    std::string                           confirmLabel = "Select";
    std::string                           saveAsName;
    std::string                           saveAsExtension;
    std::string                           nameFieldLabel = "Name";
    std::vector<FileExplorer::MountPoint> mounts;
};

[[nodiscard]] inline bool isRetainedPickerSelectionValid(const std::vector<FileExplorer::FEntry>& entries,
                                                         const std::filesystem::path& selectedPath,
                                                         FileExplorer::SelectionMode mode)
{
    if (selectedPath.empty()) {
        return false;
    }
    for (const auto& entry : entries) {
        if (entry.path == selectedPath) {
            return mode == FileExplorer::SelectionMode::Directory ? entry.bIsDirectory : !entry.bIsDirectory;
        }
    }
    return false;
}

/// Host callback for retained file/directory pickers. Widgettree chrome wires this
/// to EditorSurface::openFilePickerDialog; legacy imgui chrome falls back to FilePicker.
using EditorFilePickerCallback = std::function<void(FEditorFilePickerRequest request)>;

[[nodiscard]] inline FEditorFilePickerRequest makeScriptFilePickerRequest(std::string currentPath,
                                                                          std::function<void(std::string)> onPicked)
{
    FEditorFilePickerRequest request;
    request.title          = "Select Lua Script";
    request.configScope    = "filePicker.script";
    request.extensions     = {".lua"};
    request.currentPath    = std::move(currentPath);
    request.onPicked       = std::move(onPicked);
    return request;
}

[[nodiscard]] inline FEditorFilePickerRequest makeMaterialFilePickerRequest(std::string currentPath,
                                                                            std::function<void(std::string)> onPicked)
{
    FEditorFilePickerRequest request;
    request.title          = "Select Material";
    request.configScope    = "filePicker.material";
    request.extensions     = {".mat", ".material"};
    request.currentPath    = std::move(currentPath);
    request.onPicked       = std::move(onPicked);
    return request;
}

[[nodiscard]] inline FEditorFilePickerRequest makeDirectoryPickerRequest(std::string currentPath,
                                                                       std::function<void(std::string)> onPicked)
{
    FEditorFilePickerRequest request;
    request.title          = "Select Directory";
    request.configScope    = "filePicker.directory";
    request.filterMode     = FileExplorer::FilterMode::Directories;
    request.selectionMode  = FileExplorer::SelectionMode::Directory;
    request.currentPath    = std::move(currentPath);
    request.onPicked       = std::move(onPicked);
    return request;
}

[[nodiscard]] inline FEditorFilePickerRequest makeSceneOpenPickerRequest(std::string currentPath,
                                                                        std::function<void(std::string)> onPicked)
{
    FEditorFilePickerRequest request;
    request.title        = "Open Scene";
    request.configScope  = "filePicker.openScene";
    request.extensions   = {".scene.json"};
    request.confirmLabel = "Open";
    request.currentPath  = std::move(currentPath);
    request.onPicked     = std::move(onPicked);
    return request;
}

[[nodiscard]] inline FEditorFilePickerRequest makeSceneJsonFilePickerRequest(std::string currentPath,
                                                                           std::function<void(std::string)> onPicked)
{
    FEditorFilePickerRequest request;
    request.title          = "Select Default Scene";
    request.configScope    = "filePicker.defaultScene";
    request.extensions     = {".scene.json"};
    request.currentPath    = std::move(currentPath);
    request.onPicked       = std::move(onPicked);
    return request;
}

[[nodiscard]] inline FEditorFilePickerRequest makeSceneSavePickerRequest(std::string defaultName,
                                                                        std::string currentPath,
                                                                        std::function<void(std::string)> onPicked)
{
    FEditorFilePickerRequest request;
    request.title           = "Save Scene";
    request.configScope     = "sceneSaveDialog";
    request.filterMode      = FileExplorer::FilterMode::Directories;
    request.selectionMode   = FileExplorer::SelectionMode::Directory;
    request.confirmLabel    = "Save";
    request.saveAsName      = std::move(defaultName);
    request.saveAsExtension = ".scene.json";
    request.nameFieldLabel  = "Scene Name";
    request.currentPath     = std::move(currentPath);
    request.onPicked        = std::move(onPicked);
    return request;
}

[[nodiscard]] inline FEditorFilePickerRequest makeAssetPickerRequest(const AssetTypeDesc& desc,
                                                                     std::string currentPath,
                                                                     std::function<void(std::string)> onPicked)
{
    FEditorFilePickerRequest request;
    request.title        = desc.displayName;
    request.configScope  = "assetPickerDialog." + desc.name;
    request.extensions   = desc.extensions;
    request.currentPath  = std::move(currentPath);
    request.onPicked     = std::move(onPicked);
    return request;
}

} // namespace ya
