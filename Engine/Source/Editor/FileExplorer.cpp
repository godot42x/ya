#include "FileExplorer.h"
#include "Core/System/VirtualFileSystem.h"
#include <algorithm>

namespace ya
{

void FileExplorer::init(const std::vector<MountPoint>  &mountPoints,
                        const std::vector<std::string> &extensions,
                        FilterMode                      filterMode,
                        SelectionMode                   selectionMode)
{
    _mountPoints      = mountPoints;
    _extensions       = extensions;
    _filterMode       = filterMode;
    _selectionMode    = selectionMode;
    _activeMountPoint = nullptr;
    _currentDirectory.clear();
    _selectedPath.clear();
    memset(_searchBuffer, 0, sizeof(_searchBuffer));

    // Activate first mount point by default
    if (!_mountPoints.empty())
    {
        switchToMountPoint(&_mountPoints[0]);
    }
}

void FileExplorer::initFromVFS()
{
    _mountPoints.clear();
    _currentDirectory.clear();
    _selectedPath.clear();

    auto vfs = VirtualFileSystem::get();
    if (!vfs) return;

    for (const auto &[mountName, root] : vfs->getMountPoints())
    {
        auto contentPath = root / "Content";

        // TODO: start with the input path
        // if (!_currentDirectory.empty())
        // {
        //     auto testPath  = contentPath / _currentDirectory;
        //     if(std::filesystem::exists(testPath))
        //     {
        //         if(std::filesystem::is_directory(testPath))
        //         {
        //             contentPath = testPath;
        //         }
        //         _currentDirectory = contentPath;
        //     }
        // }

        // Only add if the path exists
        if (std::filesystem::exists(contentPath))
        {
            _mountPoints.push_back({
                .name     = mountName,
                .path     = contentPath,
                .isActive = false,
            });
        }
    }

    // Sort: Engine first, then Game, then alphabetically
    std::ranges::sort(_mountPoints, [](const MountPoint &a, const MountPoint &b) {
        if (a.name == "Engine") return true;
        if (b.name == "Engine") return false;
        if (a.name == "Game") return true;
        if (b.name == "Game") return false;
        return a.name < b.name;
    });

    // Activate first mount point
    if (!_mountPoints.empty())
    {
        switchToMountPoint(&_mountPoints[0]);
    }
}

void FileExplorer::switchToMountPoint(MountPoint *mp)
{
    if (!mp) return;

    // Deactivate previous
    if (_activeMountPoint)
    {
        _activeMountPoint->isActive = false;
    }

    // Activate new
    _activeMountPoint           = mp;
    _activeMountPoint->isActive = true;
    _currentDirectory           = _activeMountPoint->path;
    _selectedPath.clear();
}

bool FileExplorer::isPathWithinActiveMountPoint(const std::filesystem::path &path) const
{
    if (!_activeMountPoint) return false;

    auto relativePath = std::filesystem::relative(path, _activeMountPoint->path);
    return !relativePath.empty() && !relativePath.string().starts_with("..");
}

void FileExplorer::setSelectedPath(const std::filesystem::path &path)
{
    _selectedPath = path;

    // Try to navigate to the parent directory if it's within a mount point
    for (auto &mp : _mountPoints)
    {
        auto relativePath = std::filesystem::relative(path, mp.path);
        if (!relativePath.empty() && !relativePath.string().starts_with(".."))
        {
            switchToMountPoint(&mp);
            if (std::filesystem::is_directory(path))
            {
                _currentDirectory = path;
            }
            else
            {
                _currentDirectory = path.parent_path();
            }
            break;
        }
    }
}

bool FileExplorer::matchesExtension(const std::filesystem::path &path) const
{
    if (_extensions.empty()) return true;

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto &allowedExt : _extensions)
    {
        std::string lowerAllowed = allowedExt;
        std::transform(lowerAllowed.begin(), lowerAllowed.end(), lowerAllowed.begin(), ::tolower);
        if (ext == lowerAllowed) return true;
    }
    return false;
}

bool FileExplorer::matchesSearch(const std::string &name) const
{
    if (_searchBuffer[0] == '\0') return true;

    std::string searchLower = _searchBuffer;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    std::string nameLower = name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    return nameLower.find(searchLower) != std::string::npos;
}

void FileExplorer::render(SelectionCallback onSelect, float height)
{
    ImVec2 size(0, height);

    // Left panel - Mount point selector
    ImGui::BeginChild("MountPoints", ImVec2(_leftPanelWidth, height), true);
    renderMountPointSelector();
    ImGui::EndChild();

    ImGui::SameLine();

    // Splitter
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(-19, 0));
    ImGui::Button("##splitter", ImVec2(8.0f, height > 0 ? height : -1));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(1);

    if (ImGui::IsItemActive())
    {
        _leftPanelWidth += ImGui::GetIO().MouseDelta.x;
        _leftPanelWidth = std::clamp(_leftPanelWidth, 80.0f, 300.0f);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::SameLine();

    // Right panel - Directory contents
    ImGui::BeginChild("DirectoryContents", ImVec2(0, height), false);

    // Navigation bar
    bool bFocused = ImGui::IsWindowFocused();
    bool bBack    = ImGui::IsMouseClicked(3) && bFocused;

    if (_activeMountPoint && _currentDirectory != _activeMountPoint->path)
    {
        if (ImGui::Button("< Back") || bBack)
        {
            auto parent = _currentDirectory.parent_path();
            if (isPathWithinActiveMountPoint(parent) || parent == _activeMountPoint->path)
            {
                _currentDirectory = parent;
                _selectedPath.clear();
            }
        }
        ImGui::SameLine();
    }

    // Show current path relative to mount point
    if (_activeMountPoint)
    {
        auto        relativePath = std::filesystem::relative(_currentDirectory, _activeMountPoint->path);
        std::string pathStr      = relativePath.empty() || relativePath == "." ? "." : relativePath.string();
        ImGui::Text("%s: %s", _activeMountPoint->name.c_str(), pathStr.c_str());
    }
    else
    {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No mount point selected");
    }

    // View mode toggle and search on the same line
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150);

    if (_showViewModeToggle)
    {
        const char *viewModeLabel = (_viewMode == ViewMode::List) ? "➖" : "📄";
        if (ImGui::Button(viewModeLabel))
        {
            _viewMode = (_viewMode == ViewMode::List) ? ViewMode::Icon : ViewMode::List;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(_viewMode == ViewMode::List ? "Switch to Icon View" : "Switch to List View");
        }
        ImGui::SameLine();
    }

    // Search box
    ImGui::SetNextItemWidth(120);
    ImGui::InputTextWithHint("##search", "Search...", _searchBuffer, sizeof(_searchBuffer));

    ImGui::Separator();

    // Directory contents
    if (_activeMountPoint)
    {
        renderDirectoryContents(onSelect);
    }

    ImGui::EndChild();
}

void FileExplorer::renderMountPointSelector()
{
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Content Roots");
    ImGui::Separator();

    if (_mountPoints.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No locations\navailable");
        return;
    }

    for (auto &mp : _mountPoints)
    {
        const char *icon  = "[+]";
        ImVec4      color = ImVec4(1.0f, 0.7f, 0.3f, 1.0f); // Orange for plugins

        if (mp.name == "Engine")
        {
            icon  = "[E]";
            color = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); // Light blue
        }
        else if (mp.name == "Game" || mp.name == "Project")
        {
            icon  = "[G]";
            color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); // Green
        }

        bool isSelected = (_activeMountPoint == &mp);

        if (isSelected)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.8f));
        }

        ImGui::PushID(&mp);

        ImGui::TextColored(color, "%s", icon);
        ImGui::SameLine();

        if (ImGui::Selectable(mp.name.c_str(), isSelected))
        {
            switchToMountPoint(&mp);
        }

        ImGui::PopID();

        if (isSelected)
        {
            ImGui::PopStyleColor();
        }
    }
}

void FileExplorer::renderDirectoryContents(SelectionCallback onSelect)
{
    if (!std::filesystem::exists(_currentDirectory))
    {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Directory not found");
        return;
    }

    // Collect and sort entries: directories first, then files
    std::vector<std::filesystem::directory_entry> directories;
    std::vector<std::filesystem::directory_entry> files;

    try
    {
        for (const auto &entry : std::filesystem::directory_iterator(_currentDirectory))
        {
            const auto &path     = entry.path();
            std::string filename = path.filename().string();

            // Skip hidden files
            if (!filename.empty() && filename[0] == '.') continue;

            // Apply search filter
            if (!matchesSearch(filename)) continue;

            if (entry.is_directory())
            {
                if (_filterMode != FilterMode::Files)
                {
                    directories.push_back(entry);
                }
            }
            else if (entry.is_regular_file())
            {
                if (_filterMode != FilterMode::Directories && matchesExtension(path))
                {
                    files.push_back(entry);
                }
            }
        }

        // Sort alphabetically
        auto sortByName = [](const auto &a, const auto &b) {
            return a.path().filename() < b.path().filename();
        };
        std::sort(directories.begin(), directories.end(), sortByName);
        std::sort(files.begin(), files.end(), sortByName);
    }
    catch (const std::exception &e)
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
        return;
    }

    // Render based on view mode
    if (_viewMode == ViewMode::Icon)
    {
        renderIconView(onSelect, directories, files);
    }
    else
    {
        renderListView(onSelect, directories, files);
    }
}

void FileExplorer::renderListView(SelectionCallback                                    onSelect,
                                  const std::vector<std::filesystem::directory_entry> &directories,
                                  const std::vector<std::filesystem::directory_entry> &files)
{
    ImGui::BeginChild("ItemsList", ImVec2(0, 0), true);

    // Render directories
    for (const auto &entry : directories)
    {
        const auto &path        = entry.path();
        std::string filename    = path.filename().string();
        std::string displayName = "📁 " + filename;

        bool isSelected = (_selectedPath == path);

        ImGui::PushStyleColor(ImGuiCol_Text,
                              isSelected ? ImVec4(0.3f, 0.8f, 1.0f, 1.0f) : ImVec4(1.0f, 0.9f, 0.4f, 1.0f));

        if (ImGui::Selectable(displayName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            if (_selectionMode == SelectionMode::Directory)
            {
                _selectedPath = path;
            }

            // Double-click to navigate into directory
            if (ImGui::IsMouseDoubleClicked(0))
            {
                if (_selectionMode == SelectionMode::Directory && onSelect)
                {
                    onSelect(path);
                }
                else
                {
                    // Navigate into directory
                    _currentDirectory = path;
                    _selectedPath.clear();
                }
            }
        }

        ImGui::PopStyleColor();
    }

    // Render files
    for (const auto &entry : files)
    {
        const auto &path        = entry.path();
        std::string filename    = path.filename().string();
        std::string displayName = "📄 " + filename;

        bool isSelected = (_selectedPath == path);

        if (isSelected)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        }

        if (ImGui::Selectable(displayName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            if (_selectionMode == SelectionMode::File)
            {
                _selectedPath = path;
            }

            // Double-click to confirm selection or trigger action
            if (ImGui::IsMouseDoubleClicked(0))
            {
                if (_itemActionCallback)
                {
                    _itemActionCallback(path);
                }
                else if (onSelect)
                {
                    onSelect(path);
                }
            }
        }

        if (isSelected)
        {
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
}

void FileExplorer::renderIconView(SelectionCallback                                    onSelect,
                                  const std::vector<std::filesystem::directory_entry> &directories,
                                  const std::vector<std::filesystem::directory_entry> &files)
{
    float cellSize    = _thumbnailSize + _padding;
    float panelWidth  = ImGui::GetContentRegionAvail().x;
    int   columnCount = static_cast<int>(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::BeginChild("IconsArea", ImVec2(0, _showSizeSlider ? -30.0f : 0), true);

    ImGui::Columns(columnCount, nullptr, false);

    // Render directories
    for (const auto &entry : directories)
    {
        const auto &path     = entry.path();
        std::string filename = path.filename().string();

        ImGui::PushID(filename.c_str());

        bool isSelected = (_selectedPath == path);

        // Draw icon button
        if (_icons.folder)
        {
            ImGui::ImageButton(filename.c_str(),
                               *_icons.folder,
                               {_thumbnailSize, _thumbnailSize},
                               {0, 0},
                               {1, 1});
        }
        else
        {
            // Fallback: use a colored button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.7f, 0.3f, 0.8f));
            ImGui::Button("DIR", ImVec2(_thumbnailSize, _thumbnailSize));
            ImGui::PopStyleColor();
        }

        // Handle selection and double-click
        if (ImGui::IsItemClicked())
        {
            if (_selectionMode == SelectionMode::Directory)
            {
                _selectedPath = path;
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (_selectionMode == SelectionMode::Directory && onSelect)
            {
                onSelect(path);
            }
            else
            {
                // Navigate into directory
                _currentDirectory = path;
                _selectedPath.clear();
            }
        }

        // Highlight selected
        if (isSelected)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        }

        ImGui::TextWrapped("%s", filename.c_str());

        if (isSelected)
        {
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    // Render files
    for (const auto &entry : files)
    {
        const auto &path     = entry.path();
        std::string filename = path.filename().string();

        ImGui::PushID(filename.c_str());

        bool isSelected = (_selectedPath == path);

        // Draw icon button
        if (_icons.file)
        {
            ImGui::ImageButton(filename.c_str(),
                               *_icons.file,
                               {_thumbnailSize, _thumbnailSize},
                               {0, 0},
                               {1, 1});
        }
        else
        {
            // Fallback: use a colored button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 0.8f));
            ImGui::Button("FILE", ImVec2(_thumbnailSize, _thumbnailSize));
            ImGui::PopStyleColor();
        }

        // Handle selection and double-click
        if (ImGui::IsItemClicked())
        {
            if (_selectionMode == SelectionMode::File)
            {
                _selectedPath = path;
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (_itemActionCallback)
            {
                _itemActionCallback(path);
            }
            else if (onSelect)
            {
                onSelect(path);
            }
        }

        // Highlight selected
        if (isSelected)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        }

        ImGui::TextWrapped("%s", filename.c_str());

        if (isSelected)
        {
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::Columns(1);

    ImGui::EndChild();

    // Size sliders at the bottom
    if (_showSizeSlider)
    {
        ImGui::DragFloat("Thumbnail Size", &_thumbnailSize, 0.5f, 32.0f, 256.0f);
        ImGui::DragFloat("Padding", &_padding, 0.1f, 0.0f, 64.0f);
    }
}

} // namespace ya
