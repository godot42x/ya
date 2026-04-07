#include "Log.h"

#include "Core/System/VirtualFileSystem.h"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <vector>

namespace
{
constexpr std::string_view LOG_FILE_PREFIX       = "YA-";
constexpr std::string_view LOG_FILE_EXTENSION    = ".log";
constexpr std::size_t      MAX_HISTORY_LOG_FILES = 10;

std::filesystem::path getLogsDirectory()
{
    if (auto* vfs = VirtualFileSystem::get()) {
        return vfs->getEngineRoot() / "Saved" / "Logs";
    }

    return std::filesystem::current_path() / "Engine" / "Saved" / "Logs";
}

std::string makeLaunchLogFileName()
{
    const auto currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &currentTime);
#else
    localtime_r(&currentTime, &localTime);
#endif

    return std::format("{}{:04}-{:02}-{:02}_{:02}-{:02}-{:02}{}",
                       LOG_FILE_PREFIX,
                       localTime.tm_year + 1900,
                       localTime.tm_mon + 1,
                       localTime.tm_mday,
                       localTime.tm_hour,
                       localTime.tm_min,
                       localTime.tm_sec,
                       LOG_FILE_EXTENSION);
}

bool isManagedLogFile(const std::filesystem::path& path)
{
    if (!path.has_filename() || path.extension() != LOG_FILE_EXTENSION) {
        return false;
    }

    const auto fileName = path.filename().string();
    return fileName.starts_with(LOG_FILE_PREFIX);
}

void pruneHistoricalLogs(const std::filesystem::path& logsDir)
{
    std::error_code                               errorCode;
    std::vector<std::filesystem::directory_entry> historicalFiles;

    if (!std::filesystem::exists(logsDir, errorCode)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(logsDir, errorCode)) {
        if (errorCode) {
            YA_CORE_WARN("Logger::init - Failed to enumerate log directory: {}", errorCode.message());
            return;
        }

        if (!entry.is_regular_file(errorCode)) {
            errorCode.clear();
            continue;
        }

        if (!isManagedLogFile(entry.path())) {
            continue;
        }

        historicalFiles.push_back(entry);
    }

    std::sort(historicalFiles.begin(),
              historicalFiles.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.path().filename().string() > rhs.path().filename().string();
              });

    if (historicalFiles.size() <= MAX_HISTORY_LOG_FILES) {
        return;
    }

    for (std::size_t i = MAX_HISTORY_LOG_FILES; i < historicalFiles.size(); ++i) {
        const auto& file = historicalFiles[i].path();
        std::filesystem::remove(file, errorCode);
        if (errorCode) {
            YA_CORE_WARN("Logger::init - Failed to remove old log file {}: {}", file.string(), errorCode.message());
            errorCode.clear();
        }
    }
}

bool addFileAppender(logcc::SyncLogger& logger, const std::string& logFilePath)
{
    logger.fileAppenders.emplace_back(logFilePath);
    if (logger.fileAppenders.back().fileStream.is_open()) {
        return true;
    }

    logger.fileAppenders.pop_back();
    return false;
}

void clearFileAppenders(logcc::SyncLogger& logger)
{
    logger.fileAppenders.clear();
}
} // namespace

logcc::SyncLogger Logger::CoreLogger;
logcc::SyncLogger Logger::AppLogger;

void Logger::init()
{
    CoreLogger.setFormatter(YaFormatterV2("Core"));
    AppLogger.setFormatter(YaFormatterV2("App"));

    auto& lazyLogger = getLazyLog();
    lazyLogger.setFormatter(YaFormatterV2("Lazy"));

    clearFileAppenders(CoreLogger);
    clearFileAppenders(AppLogger);
    clearFileAppenders(lazyLogger);

    const auto logsDir = getLogsDirectory();

    std::error_code errorCode;
    std::filesystem::create_directories(logsDir, errorCode);
    if (errorCode) {
        YA_CORE_WARN("Logger::init - Failed to create log directory {}: {}", logsDir.string(), errorCode.message());
        return;
    }

    pruneHistoricalLogs(logsDir);

    const auto        logFilePath     = logsDir / makeLaunchLogFileName();
    const std::string logFilePathText = logFilePath.string();

    if (!addFileAppender(CoreLogger, logFilePathText) ||
        !addFileAppender(AppLogger, logFilePathText) ||
        !addFileAppender(lazyLogger, logFilePathText))
    {
        clearFileAppenders(CoreLogger);
        clearFileAppenders(AppLogger);
        clearFileAppenders(lazyLogger);
        YA_CORE_WARN("Logger::init - Failed to open session log file {}", logFilePathText);
        return;
    }

    YA_CORE_INFO("Logger::init - Session log file: {}", logFilePathText);
}

logcc::SyncLogger& Logger::getLazyLog()
{
    static logcc::SyncLogger* LazyLogger = []() {
        static logcc::SyncLogger logger;
        logger.setFormatter(YaFormatterV2("Lazy"));
        return &logger;
    }();
    return *LazyLogger;
}
