#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ya
{

/**
 * @brief 文件监视器 - 监视文件变化并触发回调
 * 
 * 使用 libfswatch 实现跨平台文件监视
 */
class FileWatcher
{
  public:
    enum class ChangeType
    {
        Created,
        Modified,
        Deleted,
        Renamed
    };

    struct FileEvent
    {
        std::string path;
        ChangeType  type;
        uint64_t    timestamp;
    };

    using Callback = std::function<void(const FileEvent&)>;

  private:
    static FileWatcher* _instance;

    struct WatchEntry
    {
        std::string path;
        std::vector<Callback> callbacks;
        uint64_t lastModified = 0;
    };

    std::unordered_map<std::string, WatchEntry> _watchedFiles;
    std::unordered_set<std::string> _watchedDirectories;
    
    bool _running = false;
    void* _fswatchSession = nullptr;  // fswatch_session*

  public:
    static FileWatcher* get();
    static void init();
    static void shutdown();

    FileWatcher();
    ~FileWatcher();

    /**
     * @brief 监视单个文件
     * @param filepath 文件路径（相对或绝对）
     * @param callback 文件变化时的回调
     */
    void watchFile(const std::string& filepath, Callback callback);

    /**
     * @brief 监视目录（递归）
     * @param dirpath 目录路径
     * @param filter 文件扩展名过滤（如 ".lua"）
     * @param callback 文件变化时的回调
     */
    void watchDirectory(const std::string& dirpath, const std::string& filter, Callback callback);

    /**
     * @brief 取消监视文件
     */
    void unwatchFile(const std::string& filepath);

    /**
     * @brief 取消监视目录
     */
    void unwatchDirectory(const std::string& dirpath);

    /**
     * @brief 轮询文件变化（每帧调用）
     */
    void poll();

    /**
     * @brief 清除所有监视
     */
    void clear();

  private:
    void startWatching();
    void stopWatching();
    uint64_t getFileModTime(const std::string& filepath);
};

} // namespace ya
