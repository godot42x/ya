#include "GameEditor/FileExplorerInternal.h"

#include <cstring>

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

void FileExplorer::selectMountPoint(const MountPoint& mp)
{
    for (auto& candidate : _mountPoints) {
        if (candidate.name == mp.name && candidate.path == mp.path) {
            const MountPoint* previousMount = _activeMountPoint;
            const std::filesystem::path previousDir = _currentDirectory;
            switchToMountPoint(&candidate);
            if (previousMount != _activeMountPoint || previousDir != _currentDirectory) {
                bumpContentGeneration();
            }
            saveConfig();
            return;
        }
    }
}

void FileExplorer::collectEntries(std::vector<FEntry>& outEntries) const
{
    outEntries.clear();
    if (!std::filesystem::exists(_currentDirectory)) {
        return;
    }
    try {
        for (const auto& entry : std::filesystem::directory_iterator(_currentDirectory)) {
            const auto& path     = entry.path();
            std::string filename = path_utils::pathToUtf8String(path.filename());
            if (!filename.empty() && filename[0] == '.') {
                continue;
            }
            if (!matchesSearch(filename)) {
                continue;
            }
            if (entry.is_directory()) {
                if (_filterMode != FilterMode::Files) {
                    outEntries.push_back(FEntry{.path = path, .name = filename, .bIsDirectory = true});
                }
            }
            else if (entry.is_regular_file()) {
                if (_filterMode != FilterMode::Directories && matchesExtension(path)) {
                    outEntries.push_back(FEntry{.path = path, .name = filename, .bIsDirectory = false});
                }
            }
        }
        std::sort(outEntries.begin(), outEntries.end(),
                  [](const FEntry& a, const FEntry& b) {
                      if (a.bIsDirectory != b.bIsDirectory) {
                          return a.bIsDirectory;
                      }
                      return a.name < b.name;
                  });
    }
    catch (const std::exception&) {
        outEntries.clear();
    }
}

bool FileExplorer::navigateBack()
{
    if (!_activeMountPoint) {
        return false;
    }
    const std::filesystem::path parent = _currentDirectory.parent_path();
    if (!isPathWithinActiveMountPoint(parent) && parent != _activeMountPoint->path) {
        return false;
    }
    if (parent.empty() || parent == _currentDirectory) {
        return false;
    }
    _currentDirectory = parent;
    _selectedPath.clear();
    bumpContentGeneration();
    saveConfig();
    return true;
}

bool FileExplorer::navigateInto(const std::filesystem::path& directory)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return false;
    }
    _currentDirectory = directory;
    _selectedPath.clear();
    bumpContentGeneration();
    saveConfig();
    return true;
}

void FileExplorer::setSearchText(std::string_view text)
{
    const size_t length = std::min(text.size(), sizeof(_searchBuffer) - 1);
    if (length == std::strlen(_searchBuffer) && std::string_view(_searchBuffer, length) == text.substr(0, length)) {
        return;
    }
    std::memcpy(_searchBuffer, text.data(), length);
    _searchBuffer[length] = '\0';
    bumpContentGeneration();
}

std::string FileExplorer::getSearchText() const
{
    return _searchBuffer;
}

bool FileExplorer::isPathWithinActiveMountPoint(const std::filesystem::path& path) const
{
    if (!_activeMountPoint) return false;

    auto relativePath = std::filesystem::relative(path, _activeMountPoint->path);
    return !relativePath.empty() && !relativePath.string().starts_with("..");
}

void FileExplorer::setSelectedPath(const std::filesystem::path& path)
{
    _selectedPaths.clear();
    if (!path.empty()) {
        _selectedPaths.push_back(path);
    }
    _rangeAnchorPath = path;
    const MountPoint* previousMount = _activeMountPoint;
    const std::filesystem::path previousDir = _currentDirectory;
    for (auto& mp : _mountPoints) {
        auto relativePath = std::filesystem::relative(path, mp.path);
        if (!relativePath.empty() && !relativePath.string().starts_with("..")) {
            switchToMountPoint(&mp);
            _selectedPath = path;
            if (std::filesystem::is_directory(path)) {
                _currentDirectory = path;
            }
            else {
                _currentDirectory = path.parent_path();
            }
            break;
        }
    }
    if (previousMount != _activeMountPoint || previousDir != _currentDirectory) {
        bumpContentGeneration();
    }
}

void FileExplorer::applySelectionGesture(const std::filesystem::path& path, bool bMulti, bool bRange)
{
    if (path.empty()) {
        return;
    }

    if (bRange && !_rangeAnchorPath.empty() && path != _rangeAnchorPath) {
        // Range follows the current directory's listed order -- what the user
        // sees, not the filesystem's.
        std::vector<FEntry> entries;
        collectEntries(entries);
        const auto locate = [&entries](const std::filesystem::path& candidate) {
            return std::find_if(entries.begin(), entries.end(), [&candidate](const FEntry& entry) {
                return entry.path == candidate;
            });
        };
        const auto anchorIt = locate(_rangeAnchorPath);
        const auto clickIt  = locate(path);
        if (anchorIt != entries.end() && clickIt != entries.end()) {
            std::vector<std::filesystem::path> range;
            for (auto it = std::min(anchorIt, clickIt); it != std::next(std::max(anchorIt, clickIt)); ++it) {
                range.push_back(it->path);
            }
            _selectedPaths = std::move(range);
            _selectedPath  = path;
            return;
        }
    }

    if (bMulti) {
        const auto it = std::find(_selectedPaths.begin(), _selectedPaths.end(), path);
        if (it != _selectedPaths.end()) {
            _selectedPaths.erase(it);
        }
        else {
            _selectedPaths.push_back(path);
        }
        _selectedPath    = path;
        _rangeAnchorPath = path;
        return;
    }

    setSelectedPath(path);
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
