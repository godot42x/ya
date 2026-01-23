#include "FilePicker.h"
#include "Core/Log.h"
#include "ImGuiHelper.h"
#include <imgui.h>

namespace ya
{

void FilePicker::setIcons(const ImGuiImageEntry *folderIcon, const ImGuiImageEntry *fileIcon)
{
    _icons.folder = folderIcon;
    _icons.file   = fileIcon;
}

void FilePicker::applyCommonSettings()
{
    _fileExplorer.setIcons(_icons);
    _fileExplorer.setViewMode(_defaultViewMode);
    _fileExplorer.setShowViewModeToggle(true);
    _fileExplorer.setShowSizeSlider(false); // Less clutter in picker dialogs
    _fileExplorer.setThumbnailSize(64.0f);  // Smaller thumbnails for picker
    _fileExplorer.setPadding(12.0f);
}

void FilePicker::open(const std::string              &title,
                      const std::string              &currentPath,
                      const std::vector<std::string> &extensions,
                      Callback                        onConfirm)
{
    YA_CORE_ASSERT(_isOpen == false, "FilePicker is already open");

    _isOpen         = true;
    _pendingClose   = false;
    _bSceneSaveMode = false;
    _title          = title;
    _onConfirm      = onConfirm;
    _onSaveConfirm  = nullptr;

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
    _pendingClose = true;
}

void FilePicker::render()
{
    // Handle deferred close
    if (_pendingClose)
    {
        _isOpen         = false;
        _pendingClose   = false;
        _onConfirm      = nullptr;
        _onSaveConfirm  = nullptr;
        _bSceneSaveMode = false;
        return;
    }

    if (!_isOpen) return;

    ImGui::OpenPopup(_title.c_str());

    // Center the popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

    bool open = _isOpen;
    if (ImGui::BeginPopupModal(_title.c_str(), &open, ImGuiWindowFlags_None))
    {
        if (_bSceneSaveMode)
        {
            renderSceneSaveContent();
        }
        else
        {
            renderFileSelectContent();
        }
        ImGui::EndPopup();
    }

    // Check if closed via X button
    if (!open)
    {
        close();
    }
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
    open("Select Texture", currentPath, {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".hdr"}, onConfirm);
}

void FilePicker::openModelPicker(const std::string &currentPath, Callback onConfirm)
{
    open("Select Model", currentPath, {".obj", ".fbx", ".gltf", ".glb", ".dae"}, onConfirm);
}

void FilePicker::openDirectoryPicker(const std::string &currentPath,
                                     Callback           onConfirm)
{
    _isOpen         = true;
    _pendingClose   = false;
    _bSceneSaveMode = false;
    _title          = "Select Directory";
    _onConfirm      = onConfirm;
    _onSaveConfirm  = nullptr;

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
    _pendingClose   = false;
    _bSceneSaveMode = true;
    _title          = "Save Scene";
    _onConfirm      = nullptr;
    _onSaveConfirm  = onConfirm;

    // Set default scene name
    strncpy(_sceneNameBuffer, defaultName.c_str(), sizeof(_sceneNameBuffer) - 1);
    _sceneNameBuffer[sizeof(_sceneNameBuffer) - 1] = '\0';

    // Initialize file explorer for Scenes directory
    _fileExplorer.initFromVFS();
    _fileExplorer.setExtensions({});
    _fileExplorer.setFilterMode(FileExplorer::FilterMode::Directories);
    _fileExplorer.setSelectionMode(FileExplorer::SelectionMode::Directory);
    applyCommonSettings();
}

void FilePicker::renderFileSelectContent()
{
    // File explorer takes most of the space
    float footerHeight    = 35.0f;
    float availableHeight = ImGui::GetContentRegionAvail().y - footerHeight;

    _fileExplorer.render([this](const std::filesystem::path &path) {
        // Double-click callback
        if (_onConfirm)
        {
            _onConfirm(path.string());
        }
        close();
    },
                         availableHeight);

    ImGui::Separator();

    // Footer with OK/Cancel buttons
    if (ImGui::Button("OK", ImVec2(120, 0)))
    {
        auto selectedPath = _fileExplorer.getSelectedPath();
        if (_onConfirm && !selectedPath.empty())
        {
            _onConfirm(selectedPath.string());
        }
        close();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        close();
    }

    ImGui::SameLine();

    // Show selected path
    auto selectedPath = _fileExplorer.getSelectedPath();
    if (!selectedPath.empty())
    {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%s", selectedPath.filename().string().c_str());
    }
}

void FilePicker::renderSceneSaveContent()
{
    // Scene name input at top
    ImGui::Text("Scene Name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##sceneName", _sceneNameBuffer, sizeof(_sceneNameBuffer));

    ImGui::Separator();

    // File explorer in the middle
    float footerHeight    = 60.0f;
    float availableHeight = ImGui::GetContentRegionAvail().y - footerHeight;

    _fileExplorer.render([this](const std::filesystem::path &path) {
        // Double-click on directory - navigate (handled by FileExplorer)
    },
                         availableHeight);

    // Preview path
    auto                  selectedPath = _fileExplorer.getSelectedPath();
    std::filesystem::path saveDir;

    if (!selectedPath.empty())
    {
        saveDir = selectedPath;
    }
    else if (_fileExplorer.getActiveMountPoint())
    {
        saveDir = _fileExplorer.getCurrentDirectory();
    }

    std::string previewPath = (saveDir / (_sceneNameBuffer + std::string(".scene.json"))).string();
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Will save to: %s", previewPath.c_str());

    ImGui::Separator();

    // Footer with Save/Cancel buttons
    bool canSave = strlen(_sceneNameBuffer) > 0;

    if (!canSave)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Save", ImVec2(120, 0)))
    {
        if (_onSaveConfirm)
        {
            _onSaveConfirm(saveDir.string(), std::string(_sceneNameBuffer));
        }
        close();
    }

    if (!canSave)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        close();
    }
}

} // namespace ya
