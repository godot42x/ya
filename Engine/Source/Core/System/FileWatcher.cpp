#include "FileWatcher.h"
#include "Core/Log.h"
#include <chrono>
#include <filesystem>

namespace ya
{

FileWatcher* FileWatcher::_instance = nullptr;

FileWatcher* FileWatcher::get()
{
    return _instance;
}

void FileWatcher::init()
{
    if (!_instance) {
        _instance = new FileWatcher();
        YA_CORE_INFO("FileWatcher initialized");
    }
}

void FileWatcher::shutdown()
{
    if (_instance) {
        delete _instance;
        _instance = nullptr;
        YA_CORE_INFO("FileWatcher shutdown");
    }
}

FileWatcher::FileWatcher()
{
    // 简化实现：使用轮询方式检测文件修改时间
    // 如果需要更高效的实现，可以集成 libfswatch
}

FileWatcher::~FileWatcher()
{
    clear();
}

void FileWatcher::watchFile(const std::string& filepath, Callback callback)
{
    namespace fs = std::filesystem;
    
    // 规范化路径
    std::string normalizedPath = filepath;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    
    auto it = _watchedFiles.find(normalizedPath);
    if (it != _watchedFiles.end()) {
        // 文件已被监视，添加回调
        it->second.callbacks.push_back(callback);
    } else {
        // 新建监视项
        WatchEntry entry;
        entry.path = normalizedPath;
        entry.callbacks.push_back(callback);
        entry.lastModified = getFileModTime(normalizedPath);
        
        _watchedFiles[normalizedPath] = std::move(entry);
        
        YA_CORE_TRACE("Now watching file: {}", normalizedPath);
    }
}

void FileWatcher::watchDirectory(const std::string& dirpath, const std::string& filter, Callback callback)
{
    namespace fs = std::filesystem;
    
    if (!fs::exists(dirpath) || !fs::is_directory(dirpath)) {
        YA_CORE_WARN("Directory does not exist: {}", dirpath);
        return;
    }
    
    // 递归扫描目录，为每个匹配的文件添加监视
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dirpath)) {
            if (entry.is_regular_file()) {
                std::string filepath = entry.path().string();
                std::replace(filepath.begin(), filepath.end(), '\\', '/');
                
                // 检查扩展名
                if (!filter.empty() && entry.path().extension() != filter) {
                    continue;
                }
                
                watchFile(filepath, callback);
            }
        }
        
        _watchedDirectories.insert(dirpath);
        YA_CORE_INFO("Now watching directory: {} (filter: {})", dirpath, filter);
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("Failed to watch directory {}: {}", dirpath, e.what());
    }
}

void FileWatcher::unwatchFile(const std::string& filepath)
{
    std::string normalizedPath = filepath;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    
    _watchedFiles.erase(normalizedPath);
    YA_CORE_TRACE("Stopped watching file: {}", normalizedPath);
}

void FileWatcher::unwatchDirectory(const std::string& dirpath)
{
    _watchedDirectories.erase(dirpath);
    
    // 移除该目录下的所有文件监视
    for (auto it = _watchedFiles.begin(); it != _watchedFiles.end();) {
        if (it->first.find(dirpath) == 0) {
            it = _watchedFiles.erase(it);
        } else {
            ++it;
        }
    }
    
    YA_CORE_INFO("Stopped watching directory: {}", dirpath);
}

void FileWatcher::poll()
{
    namespace fs = std::filesystem;
    
    // 轮询所有监视的文件
    for (auto& [filepath, entry] : _watchedFiles) {
        uint64_t currentModTime = getFileModTime(filepath);
        
        if (currentModTime == 0) {
            // 文件已删除
            FileEvent event{filepath, ChangeType::Deleted, currentModTime};
            for (auto& callback : entry.callbacks) {
                callback(event);
            }
            continue;
        }
        
        if (currentModTime > entry.lastModified) {
            // 文件已修改
            entry.lastModified = currentModTime;
            
            FileEvent event{filepath, ChangeType::Modified, currentModTime};
            for (auto& callback : entry.callbacks) {
                callback(event);
            }
            
            YA_CORE_TRACE("File modified: {}", filepath);
        }
    }
}

void FileWatcher::clear()
{
    _watchedFiles.clear();
    _watchedDirectories.clear();
    YA_CORE_INFO("FileWatcher cleared all watches");
}

uint64_t FileWatcher::getFileModTime(const std::string& filepath)
{
    namespace fs = std::filesystem;
    
    try {
        if (!fs::exists(filepath)) {
            return 0;
        }
        
        auto ftime = fs::last_write_time(filepath);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        return std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
    }
    catch (const std::exception& e) {
        YA_CORE_WARN("Failed to get file time for {}: {}", filepath, e.what());
        return 0;
    }
}

} // namespace ya
