#include "GameEditor/FileExplorerInternal.h"

namespace ya
{

void FileExplorer::render(SelectionCallback onSelect, float height)
{
    ImVec2 size(0, height);

    ImGui::BeginChild("MountPoints", ImVec2(_leftPanelWidth, height), true);
    renderMountPointSelector();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(-19, 0));
    ImGui::Button("##splitter", ImVec2(8.0f, height > 0 ? height : -1));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(1);

    if (ImGui::IsItemActive()) {
        _leftPanelWidth += ImGui::GetIO().MouseDelta.x;
        _leftPanelWidth = std::clamp(_leftPanelWidth, 80.0f, 300.0f);
        saveConfig();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::SameLine();

    ImGui::BeginChild("DirectoryContents", ImVec2(0, height), false);

    bool bFocused = ImGui::IsWindowFocused();
    bool bBack    = ImGui::IsMouseClicked(3) && bFocused;

    if (_activeMountPoint && _currentDirectory != _activeMountPoint->path) {
        if (ImGui::Button("< Back") || bBack) {
            auto parent = _currentDirectory.parent_path();
            if (isPathWithinActiveMountPoint(parent) || parent == _activeMountPoint->path) {
                _currentDirectory = parent;
                _selectedPath.clear();
                saveConfig();
            }
        }
        ImGui::SameLine();
    }

    if (_activeMountPoint) {
        auto        relativePath = std::filesystem::relative(_currentDirectory, _activeMountPoint->path);
        std::string pathStr      = relativePath.empty() || relativePath == "." ? "." : path_utils::pathToGenericUtf8String(relativePath);
        ImGui::Text("%s: %s", _activeMountPoint->name.c_str(), pathStr.c_str());
    }
    else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No mount point selected");
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150);

    if (_showViewModeToggle) {
        const char* viewModeLabel = (_viewMode == ViewMode::List) ? "➖" : "📄";
        if (ImGui::Button(viewModeLabel)) {
            _viewMode = (_viewMode == ViewMode::List) ? ViewMode::Icon : ViewMode::List;
            saveConfig();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(_viewMode == ViewMode::List ? "Switch to Icon View" : "Switch to List View");
        }
        ImGui::SameLine();
    }

    ImGui::SetNextItemWidth(120);
    ImGui::InputTextWithHint("##search", "Search...", _searchBuffer, sizeof(_searchBuffer));

    ImGui::Separator();

    if (_activeMountPoint) {
        renderDirectoryContents(onSelect);
    }

    ImGui::EndChild();
}

void FileExplorer::renderMountPointSelector()
{
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Content Roots");
    ImGui::Separator();

    if (_mountPoints.empty()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No locations\navailable");
        return;
    }

    for (auto& mp : _mountPoints) {
        const char* icon  = "[+]";
        ImVec4      color = ImVec4(1.0f, 0.7f, 0.3f, 1.0f);

        if (mp.name == "Engine") {
            icon  = "[E]";
            color = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
        }
        else if (mp.name == "Game" || mp.name == "Project") {
            icon  = "[G]";
            color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        }

        bool isSelected = (_activeMountPoint == &mp);

        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.8f));
        }

        ImGui::PushID(&mp);

        ImGui::TextColored(color, "%s", icon);
        ImGui::SameLine();

        if (ImGui::Selectable(mp.name.c_str(), isSelected)) {
            switchToMountPoint(&mp);
            saveConfig();
        }

        ImGui::PopID();

        if (isSelected) {
            ImGui::PopStyleColor();
        }
    }
}

void FileExplorer::renderDirectoryContents(SelectionCallback onSelect)
{
    if (!std::filesystem::exists(_currentDirectory)) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Directory not found");
        return;
    }

    std::vector<std::filesystem::directory_entry> directories;
    std::vector<std::filesystem::directory_entry> files;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(_currentDirectory)) {
            const auto& path     = entry.path();
            std::string filename = path_utils::pathToUtf8String(path.filename());

            if (!filename.empty() && filename[0] == '.') continue;
            if (!matchesSearch(filename)) continue;

            if (entry.is_directory()) {
                if (_filterMode != FilterMode::Files) {
                    directories.push_back(entry);
                }
            }
            else if (entry.is_regular_file()) {
                if (_filterMode != FilterMode::Directories && matchesExtension(path)) {
                    files.push_back(entry);
                }
            }
        }

        auto sortByName = [](const auto& a, const auto& b) {
            return a.path().filename() < b.path().filename();
        };
        std::sort(directories.begin(), directories.end(), sortByName);
        std::sort(files.begin(), files.end(), sortByName);
    }
    catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
        return;
    }

    if (_viewMode == ViewMode::Icon) {
        renderIconView(onSelect, directories, files);
    }
    else {
        renderListView(onSelect, directories, files);
    }
}

void FileExplorer::renderListView(SelectionCallback                                    onSelect,
                                  const std::vector<std::filesystem::directory_entry>& directories,
                                  const std::vector<std::filesystem::directory_entry>& files)
{
    ImGui::BeginChild("ItemsList", ImVec2(0, 0), true);

    for (const auto& entry : directories) {
        const auto& path        = entry.path();
        std::string filename    = path_utils::pathToUtf8String(path.filename());
        std::string displayName = "📁 " + filename;

        bool isSelected = (_selectedPath == path);

        ya::ImGuiStyleScope style;
        style.pushColor(ImGuiCol_Text,
                        isSelected ? ImVec4(0.3f, 0.8f, 1.0f, 1.0f) : ImVec4(1.0f, 0.9f, 0.4f, 1.0f));

        if (ImGui::Selectable(displayName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (_selectionMode == SelectionMode::Directory) {
                _selectedPath = path;
                saveConfig();
            }

            if (ImGui::IsMouseDoubleClicked(0)) {
                if (_selectionMode == SelectionMode::Directory && onSelect) {
                    onSelect(path);
                }
                else {
                    _currentDirectory = path;
                    _selectedPath.clear();
                    saveConfig();
                }
            }
        }
    }

    for (const auto& entry : files) {
        const auto& path        = entry.path();
        std::string filename    = path_utils::pathToUtf8String(path.filename());
        std::string displayName = "📄 " + filename;

        bool isSelected = (_selectedPath == path);

        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        }

        if (ImGui::Selectable(displayName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (_selectionMode == SelectionMode::File) {
                _selectedPath = path;
                saveConfig();
            }

            if (ImGui::IsMouseDoubleClicked(0)) {
                if (_itemActionCallback) {
                    _itemActionCallback(path);
                }
                else if (onSelect) {
                    onSelect(path);
                }
            }
        }

        if (isSelected) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
}

void FileExplorer::renderIconView(SelectionCallback                                    onSelect,
                                  const std::vector<std::filesystem::directory_entry>& directories,
                                  const std::vector<std::filesystem::directory_entry>& files)
{
    float cellSize    = _thumbnailSize + _padding;
    float panelWidth  = ImGui::GetContentRegionAvail().x;
    int   columnCount = static_cast<int>(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::BeginChild("IconsArea", ImVec2(0, _showSizeSlider ? -30.0f : 0), true);
    ImGui::Columns(columnCount, nullptr, false);

    for (const auto& entry : directories) {
        const auto& path     = entry.path();
        std::string filename = path_utils::pathToUtf8String(path.filename());

        ImGui::PushID(filename.c_str());

        bool isSelected = (_selectedPath == path);

        if (_icons.folder) {
            ImGui::ImageButton(filename.c_str(), *_icons.folder, {_thumbnailSize, _thumbnailSize}, {0, 0}, {1, 1});
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.7f, 0.3f, 0.8f));
            ImGui::Button("DIR", ImVec2(_thumbnailSize, _thumbnailSize));
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemClicked()) {
            if (_selectionMode == SelectionMode::Directory) {
                _selectedPath = path;
                saveConfig();
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (_selectionMode == SelectionMode::Directory && onSelect) {
                onSelect(path);
            }
            else {
                _currentDirectory = path;
                _selectedPath.clear();
                saveConfig();
            }
        }

        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        }

        ImGui::TextWrapped("%s", filename.c_str());

        if (isSelected) {
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    for (const auto& entry : files) {
        const auto& path     = entry.path();
        std::string filename = path_utils::pathToUtf8String(path.filename());

        ImGui::PushID(filename.c_str());

        bool isSelected = (_selectedPath == path);

        if (_icons.file) {
            ImGui::ImageButton(filename.c_str(), *_icons.file, {_thumbnailSize, _thumbnailSize}, {0, 0}, {1, 1});
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 0.8f));
            ImGui::Button("FILE", ImVec2(_thumbnailSize, _thumbnailSize));
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemClicked()) {
            if (_selectionMode == SelectionMode::File) {
                _selectedPath = path;
                saveConfig();
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (_itemActionCallback) {
                _itemActionCallback(path);
            }
            else if (onSelect) {
                onSelect(path);
            }
        }

        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        }

        ImGui::TextWrapped("%s", filename.c_str());

        if (isSelected) {
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::Columns(1);
    ImGui::EndChild();

    if (_showSizeSlider) {
        if (ImGui::DragFloat("Thumbnail Size", &_thumbnailSize, 0.5f, 32.0f, 256.0f)) {
            saveConfig();
        }
        if (ImGui::DragFloat("Padding", &_padding, 0.1f, 0.0f, 64.0f)) {
            saveConfig();
        }
    }
}

} // namespace ya
