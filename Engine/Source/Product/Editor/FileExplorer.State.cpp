#include "Editor/FileExplorerInternal.h"

namespace ya
{

void FileExplorer::init(const std::vector<MountPoint>&  mountPoints,
                        const std::vector<std::string>& extensions,
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

    if (!_mountPoints.empty()) {
        switchToMountPoint(&_mountPoints[0]);
    }

    loadConfig();
}

void FileExplorer::initFromVFS()
{
    _mountPoints.clear();
    _currentDirectory.clear();
    _selectedPath.clear();

    auto vfs = VirtualFileSystem::get();
    if (!vfs) return;

    for (const auto& [mountName, root] : vfs->getMountPoints()) {
        auto contentPath = root / "Content";

        if (std::filesystem::exists(contentPath)) {
            _mountPoints.push_back({
                .name     = mountName,
                .path     = contentPath,
                .isActive = false,
            });
        }
        else {
            _mountPoints.push_back({
                .name     = mountName,
                .path     = root,
                .isActive = false,
            });
        }
    }

    std::ranges::sort(_mountPoints, [](const MountPoint& a, const MountPoint& b) {
        if (a.name == "Engine") return true;
        if (b.name == "Engine") return false;
        if (a.name == "Content") return true;
        if (b.name == "Content") return false;
        if (a.name == "GameRoot") return true;
        if (b.name == "GameRoot") return false;
        return a.name < b.name;
    });

    if (!_mountPoints.empty()) {
        switchToMountPoint(&_mountPoints[0]);
    }

    loadConfig();
}

void FileExplorer::loadConfig()
{
    if (_configScope.empty()) {
        return;
    }

    _configDirty = false;

    auto& cfg = ConfigManager::get();
    if (!cfg.hasDocument("editor")) {
        return;
    }

    const std::string baseKey  = makeConfigKey("");
    int               viewMode = cfg.getOr<int>("editor", baseKey + "viewMode", static_cast<int>(_viewMode));
    if (viewMode >= static_cast<int>(ViewMode::List) && viewMode <= static_cast<int>(ViewMode::Icon)) {
        _viewMode = static_cast<ViewMode>(viewMode);
    }

    _leftPanelWidth = cfg.getOr<float>("editor", baseKey + "leftPanelWidth", _leftPanelWidth);
    _thumbnailSize  = cfg.getOr<float>("editor", baseKey + "thumbnailSize", _thumbnailSize);
    _padding        = cfg.getOr<float>("editor", baseKey + "padding", _padding);

    const std::string currentDirectory = cfg.getOr<std::string>("editor", baseKey + "currentDirectory", "");
    if (!currentDirectory.empty()) {
        const std::filesystem::path dirPath = path_utils::pathFromUtf8String(currentDirectory);
        if (std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath)) {
            setSelectedPath(dirPath);
            _selectedPath.clear();
        }
    }

    const std::string selectedPath = cfg.getOr<std::string>("editor", baseKey + "selectedPath", "");
    if (!selectedPath.empty()) {
        const std::filesystem::path path = path_utils::pathFromUtf8String(selectedPath);
        if (std::filesystem::exists(path)) {
            setSelectedPath(path);
        }
    }
}

void FileExplorer::saveConfig() const
{
    if (_configScope.empty()) {
        return;
    }

    auto& cfg = ConfigManager::get();
    if (!cfg.hasDocument("editor")) {
        return;
    }

    const std::string baseKey = makeConfigKey("");
    ConfigManager::Editor("editor")
        .set(baseKey + "viewMode", static_cast<int>(_viewMode))
        .set(baseKey + "leftPanelWidth", _leftPanelWidth)
        .set(baseKey + "thumbnailSize", _thumbnailSize)
        .set(baseKey + "padding", _padding)
        .set(baseKey + "currentDirectory", path_utils::pathToUtf8String(_currentDirectory))
        .set(baseKey + "selectedPath", path_utils::pathToUtf8String(_selectedPath));
    _configDirty = true;
}

void FileExplorer::flushConfig() const
{
    if (!_configDirty || _configScope.empty()) {
        return;
    }

    auto& cfg = ConfigManager::get();
    if (!cfg.hasDocument("editor")) {
        return;
    }

    ConfigManager::Editor("editor").flush();
    _configDirty = false;
}

} // namespace ya
