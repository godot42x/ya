//
// Created by nono on 10/14/23.
//

#include "Instrumentor.h"
#include "Core/App/App.h"
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
void Instrumentor::BeginSession(const std::string &name, const std::string &filepath)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (m_SessionActive) {
        YA_CORE_WARN("Instrumentor::BeginSession - Session '{}' already active, ending it first", m_SessionName);
        EndSessionInternal();
    }

    m_SessionName = name;
    _outputPath   = std::filesystem::path(filepath);
    if (_outputPath.extension() != ".json") {
        YA_CORE_WARN("Instrumentor::BeginSession - Filepath '{}' does not end with .json, adding it", filepath);
        _outputPath.replace_extension(".json");
    }
    if (!std::filesystem::exists(_outputPath.parent_path())) {
        std::filesystem::create_directories(_outputPath.parent_path());
    }
    m_OutputStream.open(_outputPath.string());

    if (!m_OutputStream.is_open()) {
        YA_CORE_ERROR("Instrumentor::BeginSession - Failed to open file: {}", filepath);
        return;
    }

    m_SessionActive    = true;
    m_SessionStartTime = std::chrono::steady_clock::now();
    m_EventCount       = 0;
    m_DroppedEvents    = 0;
    m_Events.clear();
    m_Frames.clear();
    // m_FrameIndexMap.clear();

    // Reserve capacity to reduce allocations
    m_Events.reserve(10000);
    m_Frames.reserve(1000);

    YA_CORE_INFO("Instrumentor: Session '{}' started, writing to '{}'", name, filepath);
}

/**
 * @brief End the current profiling session and write output file
 */
void Instrumentor::EndSession()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    EndSessionInternal();
}


/**
 * @brief End session (internal, assumes lock is held)
 */
void Instrumentor::EndSessionInternal()
{
    if (!m_SessionActive) {
        return;
    }

    // Write speedscope JSON format if file stream is open
    if (m_OutputStream.is_open()) {
        WriteSpeedscopeJson();
        m_OutputStream.close();

        // 打印可点击的链接
        auto absPath = std::filesystem::absolute(_outputPath);
        auto pathStr = absPath.string();

        // 转换为 URL 编码的路径 (替换反斜杠)
        std::string urlPath = pathStr;
        std::replace(urlPath.begin(), urlPath.end(), '\\', '/');

        YA_CORE_INFO("Instrumentor: Session '{}' ended, wrote to '{}'", m_SessionName, pathStr);
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

        m_SessionName.clear();
    }

    m_SessionActive = false;

    YA_CORE_INFO("Instrumentor: Session '{}' ended. {} events recorded, {} dropped",
                 m_SessionName,
                 m_EventCount.load(),
                 m_DroppedEvents.load());
}



} // namespace ya