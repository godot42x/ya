#include "GameEditor/FileExplorerInternal.h"

namespace ya
{

void FileExplorer::switchToMountPoint(MountPoint* mp)
{
    if (!mp) return;

    if (_activeMountPoint) {
        _activeMountPoint->isActive = false;
    }

    _activeMountPoint           = mp;
    _activeMountPoint->isActive = true;
    _currentDirectory           = _activeMountPoint->path;
    _selectedPath.clear();
}

bool FileExplorer::isPathWithinActiveMountPoint(const std::filesystem::path& path) const
{
    if (!_activeMountPoint) return false;

    auto relativePath = std::filesystem::relative(path, _activeMountPoint->path);
    return !relativePath.empty() && !relativePath.string().starts_with("..");
}

void FileExplorer::setSelectedPath(const std::filesystem::path& path)
{
    _selectedPath = path;

    for (auto& mp : _mountPoints) {
        auto relativePath = std::filesystem::relative(path, mp.path);
        if (!relativePath.empty() && !relativePath.string().starts_with("..")) {
            switchToMountPoint(&mp);
            if (std::filesystem::is_directory(path)) {
                _currentDirectory = path;
            }
            else {
                _currentDirectory = path.parent_path();
            }
            break;
        }
    }
}

bool FileExplorer::matchesExtension(const std::filesystem::path& path) const
{
    if (_extensions.empty()) return true;

    for (const auto& allowedExt : _extensions) {
        if (std::string_view(path_utils::pathToUtf8String(path)).ends_with(allowedExt)) {
            return true;
        }
    }
    return false;
}

bool FileExplorer::matchesSearch(const std::string& name) const
{
    if (_searchBuffer[0] == '\0') return true;

    std::string searchLower = _searchBuffer;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    std::string nameLower = name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    return nameLower.find(searchLower) != std::string::npos;
}

} // namespace ya
