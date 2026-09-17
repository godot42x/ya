//
// Created by nono on 10/14/23.
//

#include "Core/Profiling/Instrumentor.h"

#include "Core/Log.h"
#include <algorithm>
#include <filesystem>


namespace ya
{

/**
 * @brief Start a profiling session
 *
 * @param name Session name (displayed in speedscope)
 * @param filepath Output file path (should end with .json)
 */
void Instrumentor::beginSession(const std::string &name, const std::string &filepath)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_sessionActive) {
        YA_CORE_WARN("Instrumentor::beginSession - Session '{}' already active, ending it first", _sessionName);
        endSessionInternal();
    }

    _sessionName = name;
    _outputPath  = std::filesystem::path(filepath);
    if (_outputPath.extension() != ".json") {
        YA_CORE_WARN("Instrumentor::beginSession - Filepath '{}' does not end with .json, adding it", filepath);
        _outputPath.replace_extension(".json");
    }
    if (!std::filesystem::exists(_outputPath.parent_path())) {
        std::filesystem::create_directories(_outputPath.parent_path());
    }
    _outputStream.open(_outputPath.string());

    if (!_outputStream.is_open()) {
        YA_CORE_ERROR("Instrumentor::beginSession - Failed to open file: {}", filepath);
        return;
    }

    _sessionActive    = true;
    _sessionStartTime = std::chrono::steady_clock::now();
    _eventCount       = 0;
    _droppedEvents    = 0;
    _events.clear();
    _frames.clear();
    _frameIndexMap.clear();

    // Reserve capacity to reduce allocations
    _events.reserve(10000);
    _frames.reserve(1000);

    YA_CORE_INFO("Instrumentor: Session '{}' started, writing to '{}'", name, filepath);
}

/**
 * @brief End the current profiling session and write output file
 */
void Instrumentor::endSession()
{
    std::lock_guard<std::mutex> lock(_mutex);
    endSessionInternal();
}


/**
 * @brief End session (internal, assumes lock is held)
 */
void Instrumentor::endSessionInternal()
{
    if (!_sessionActive) {
        return;
    }

    const std::string finishedSessionName = _sessionName;

    // Write speedscope JSON format if file stream is open
    if (_outputStream.is_open()) {
        writeSpeedscopeJson();
        _outputStream.close();

        // 打印可点击的链接
        auto absPath = std::filesystem::absolute(_outputPath);

        // 重定向 latest profile
        auto latestPath = absPath.parent_path() / "profile-latest.speedscope.json";
        std::filesystem::copy_file(absPath,
            latestPath,
                                   std::filesystem::copy_options::overwrite_existing);

        auto pathStr = latestPath.string();

        YA_CORE_INFO("Instrumentor: Session '{}' ended, wrote to '{}'", _sessionName, pathStr);
        YA_CORE_INFO("========================================");
        YA_CORE_INFO("🔥 Profile Ready! Choose one option:");
        YA_CORE_INFO("");
        YA_CORE_INFO("  Option 1 (Recommended):");
        YA_CORE_INFO("    Open in VS Code and drag to: https://www.speedscope.app/");
        YA_CORE_INFO("    File: vscode://file/{}", pathStr);
        YA_CORE_INFO("");
        YA_CORE_INFO("  Option 2:");
        YA_CORE_INFO("    Visit: https://www.speedscope.app/");
        YA_CORE_INFO("    Drag & drop: {}", pathStr);
        YA_CORE_INFO("");
        YA_CORE_INFO("  Option 3 (CLI):");
        YA_CORE_INFO("    npm install -g speedscope");
        YA_CORE_INFO("    speedscope \"{}\"", pathStr);
        YA_CORE_INFO("========================================");

        _sessionName.clear();
    }

    _sessionActive = false;

    YA_CORE_INFO("Instrumentor: Session '{}' ended. {} events recorded, {} dropped",
                 finishedSessionName,
                 _eventCount.load(),
                 _droppedEvents.load());
}



} // namespace ya
