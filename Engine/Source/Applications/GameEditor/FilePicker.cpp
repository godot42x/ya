#include "GameEditor/FilePicker.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "Core/System/PathUtils.h"
#include <format>

namespace ya
{

void FilePicker::applyCommonSettings()
{
    _fileExplorer.setConfigScope(_configScope);
    _fileExplorer.setViewMode(_defaultViewMode);
    _fileExplorer.setShowViewModeToggle(true);
    _fileExplorer.setShowSizeSlider(false);
    _fileExplorer.setThumbnailSize(64.0f);
    _fileExplorer.setPadding(12.0f);
    _fileExplorer.loadConfig();
}

void FilePicker::open(const std::string              &title,
                      const std::string              &currentPath,
                      const std::vector<std::string> &extensions,
                      Callback                        onConfirm)
{
    YA_CORE_ASSERT(_isOpen == false, "FilePicker is already open");

    _isOpen         = true;
    _bSceneSaveMode = false;
    _title          = title;
    _onConfirm      = onConfirm;
    _onSaveConfirm  = nullptr;
    _configScope    = std::format("filePicker.{}", title);
    _fileExplorer.setConfigScope(_configScope);

    // Initialize file explorer from VFS
    _fileExplorer.initFromVFS();
    _fileExplorer.setExtensions(extensions);
    _fileExplorer.setFilterMode(FileExplorer::FilterMode::Both);
    _fileExplorer.setSelectionMode(FileExplorer::SelectionMode::File);
    applyCommonSettings();

    // Set initial selection if provided
    if (!currentPath.empty())
    {
        _fileExplorer.setSelectedPath(currentPath);
    }
}

void FilePicker::close()
{
    _isOpen         = false;
    _onConfirm      = nullptr;
    _onSaveConfirm  = nullptr;
    _bSceneSaveMode = false;
}

void FilePicker::openScriptPicker(const std::string &currentPath, Callback onConfirm)
{
    open("Select Lua Script", currentPath, {".lua"}, onConfirm);
}

void FilePicker::openMaterialPicker(const std::string &currentPath, Callback onConfirm)
{
    open("Select Material", currentPath, {".mat", ".material"}, onConfirm);
}

void FilePicker::openTexturePicker(const std::string &currentPath, Callback onConfirm)
{
    open("Select Texture", currentPath, {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".hdr",".ktx", ".ktx2"}, onConfirm);
}

void FilePicker::openModelPicker(const std::string &currentPath, Callback onConfirm)
{
    open("Select Model", currentPath, {".obj", ".fbx", ".gltf", ".glb", ".dae"}, onConfirm);
}

void FilePicker::openAssetPicker(const std::string &title,
                                 const std::vector<std::string> &extensions,
                                 const std::string &currentPath,
                                 Callback onConfirm)
{
    open(title, currentPath, extensions, onConfirm);
}

void FilePicker::openDirectoryPicker(const std::string &currentPath,
                                     Callback           onConfirm)
{
    _isOpen         = true;
    _bSceneSaveMode = false;
    _title          = "Select Directory";
    _onConfirm      = onConfirm;
    _onSaveConfirm  = nullptr;
    _configScope    = "filePicker.Select Directory";
    _fileExplorer.setConfigScope(_configScope);

    _fileExplorer.initFromVFS();
    _fileExplorer.setExtensions({});
    _fileExplorer.setFilterMode(FileExplorer::FilterMode::Directories);
    _fileExplorer.setSelectionMode(FileExplorer::SelectionMode::Directory);
    applyCommonSettings();

    if (!currentPath.empty())
    {
        _fileExplorer.setSelectedPath(currentPath);
    }
}

void FilePicker::openSceneSavePicker(const std::string &defaultName, SaveCallback onConfirm)
{
    _isOpen         = true;
    _bSceneSaveMode = true;
    _title          = "Save Scene";
    _onConfirm      = nullptr;
    _onSaveConfirm  = onConfirm;
    _configScope    = "filePicker.Save Scene";
    _fileExplorer.setConfigScope(_configScope);

    // Set default scene name
    strncpy_s(_sceneNameBuffer, defaultName.c_str(), _TRUNCATE);

    // Initialize file explorer for Scenes directory
    _fileExplorer.initFromVFS();
    _fileExplorer.setExtensions({});
    _fileExplorer.setFilterMode(FileExplorer::FilterMode::Directories);
    _fileExplorer.setSelectionMode(FileExplorer::SelectionMode::Directory);
    applyCommonSettings();

    const std::string lastSaveDirectory = ConfigManager::get().getOr<std::string>("editor", "filePicker.lastSaveDirectory", "");
    if (!lastSaveDirectory.empty()) {
        _fileExplorer.setSelectedPath(path_utils::pathFromUtf8String(lastSaveDirectory));
    }
}

} // namespace ya
