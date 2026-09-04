#pragma once

#include "GameEditor/FileExplorer.h"

#include <functional>
#include <string>
#include <vector>

namespace ya
{

struct FEditorFilePickerRequest
{
    std::string                        title;
    std::string                        configScope;
    std::vector<std::string>           extensions;
    FileExplorer::FilterMode           filterMode    = FileExplorer::FilterMode::Both;
    FileExplorer::SelectionMode        selectionMode = FileExplorer::SelectionMode::File;
    std::string                        currentPath;
    std::function<void(std::string)>   onPicked;
};

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

} // namespace ya
