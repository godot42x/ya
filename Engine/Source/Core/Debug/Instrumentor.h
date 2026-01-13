//
// Created by nono on 10/14/23.
// Extended for speedscope JSON format support
//

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string>
#include <thread>
#include <vector>

#include "Core/Log.h"

namespace ya
{

//=============================================================================
// Speedscope JSON Format Support
// Reference: https://github.com/jlfwong/speedscope
//
// Speedscope uses a JSON format that supports multiple profiles. We use the
// "evented" format which records individual begin/end events.
//=============================================================================

/**
 * @brief Represents a single profile event for speedscope format
 *
 * Events can be either "O" (open/begin) or "C" (close/end) type.
 * Time is recorded in microseconds relative to session start.
 */
struct SpeedscopeEvent
{
    enum class Type : char
    {
        Open  = 'O', // Begin event (function entry)
        Close = 'C'  // End event (function exit)
    };

    Type        type;       // Event type: Open or Close
    uint32_t    frameIndex; // Index into the shared frames array
    double      at;         // Time in microseconds since session start
    std::string tid;        // Thread ID as string
};

/**
 * @brief Represents a frame (function/scope) in speedscope format
 *
 * Frames are deduplicated and stored in a shared array.
 * Events reference frames by index.
 */
struct SpeedscopeFrame
{
    std::string name; // Function/scope name
    std::string file; // Source file (optional)
    int         line; // Source line (optional)
};

/**
 * @brief Configuration options for the profiler
 */
struct ProfilerConfig
{
    bool bIncludeSourceInfo = true; // Include file:line in frame names
};

//=============================================================================
// Instrumentor - Main profiler class
//=============================================================================

/**
 * @brief Thread-safe profiler that outputs speedscope-compatible JSON
 *
 * Usage:
 *   1. Call BeginSession() at program start
 *   2. Use YA_PROFILE_SCOPE/YA_PROFILE_FUNCTION macros in code
 *   3. Call EndSession() at program end
 *   4. Open the .json file in https://www.speedscope.app/
 *
 * The output JSON conforms to the speedscope file format specification:
 * https://github.com/jlfwong/speedscope/wiki/Importing-from-custom-sources
 */
struct Instrumentor
{
  private:
    // Session state
    bool          m_SessionActive = false;
    std::string   m_SessionName;
    std::ofstream m_OutputStream;

    // Thread safety
    mutable std::mutex m_Mutex;

    // Event storage (buffered for batch writing)
    std::vector<SpeedscopeEvent> m_Events;
    std::vector<SpeedscopeFrame> m_Frames;
    std::filesystem::path        _outputPath;

    // Frame deduplication: name -> frame index
    std::unordered_map<std::string, int> m_FrameIndexMap;

    // Timing
    std::chrono::steady_clock::time_point m_SessionStartTime;

    // Configuration
    ProfilerConfig m_Config;

    // Statistics
    std::atomic<size_t> m_EventCount{0};
    std::atomic<size_t> m_DroppedEvents{0};

  public:
    Instrumentor() = default;
    ~Instrumentor()
    {
        if (m_SessionActive) {
            EndSession();
        }
    }

    // Prevent copying
    Instrumentor(const Instrumentor &)            = delete;
    Instrumentor &operator=(const Instrumentor &) = delete;

    /**
     * @brief Get the singleton instance
     */
    static Instrumentor &Get()
    {
        static Instrumentor instance;
        return instance;
    }

    void BeginSession(const std::string &name, const std::string &filepath = "profile.speedscope.json");
    void EndSession();

    /**
     * @brief Record a begin event for a scope/function
     *
     * @param name Scope/function name
     * @param file Source file (optional)
     * @param line Source line (optional)
     * @return Frame index for use with WriteEndEvent
     */
    uint32_t WriteBeginEvent(const std::string &name, const std::string &file = "", int line = 0)
    {
        if (!m_SessionActive) {
            return static_cast<uint32_t>(-1);
        }

        std::lock_guard<std::mutex> lock(m_Mutex);

        uint32_t frameIndex = GetOrCreateFrame(name, file, line);

        // Calculate time since session start in microseconds
        auto   now      = std::chrono::steady_clock::now();
        auto   duration = std::chrono::duration_cast<std::chrono::microseconds>(now - m_SessionStartTime);
        double timeUs   = static_cast<double>(duration.count());

        // Get thread ID as string
        std::string tid = std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));

        // Record event
        m_Events.push_back({
            .type       = SpeedscopeEvent::Type::Open,
            .frameIndex = frameIndex,
            .at         = timeUs,
            .tid        = tid,
        });

        m_EventCount++;

        return frameIndex;
    }

    /**
     * @brief Record an end event for a scope/function (no console output)
     *
     * @param frameIndex Frame index returned by WriteBeginEvent
     */
    void WriteEndEvent(uint32_t frameIndex)
    {
        WriteEndEventInternal(frameIndex);
    }

    /**
     * @brief Record an end event and ALWAYS print to console
     *
     * @param frameIndex Frame index returned by WriteBeginEvent
     * @param durationNs Duration in nanoseconds
     * @param name Name for console output
     */
    void WriteEndEventLog(int frameIndex, long long durationNs, const std::string &name)
    {
        // Always print to console regardless of config
        if (!name.empty() && durationNs > 0) {
            PrintToConsole(name, durationNs);
        }

        WriteEndEventInternal(frameIndex);
    }

  private:
    /**
     * @brief Print timing info to console
     */
    void PrintToConsole(const std::string &name, long long durationNs)
    {
        float ms = static_cast<float>(durationNs) / 1000000.0f;
        YA_CORE_DEBUG("[Profile] {}: {:.3f}ms ({} ns)", name, ms, durationNs);
    }

    /**
     * @brief Internal end event recording (without console output)
     */
    void WriteEndEventInternal(uint32_t frameIndex)
    {
        if (!m_SessionActive || frameIndex < 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);

        // Calculate time since session start in microseconds
        auto   now      = std::chrono::steady_clock::now();
        auto   duration = std::chrono::duration_cast<std::chrono::microseconds>(now - m_SessionStartTime);
        double timeUs   = static_cast<double>(duration.count());

        // Get thread ID as string
        std::string tid = std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));

        // Record event
        m_Events.push_back({
            .type       = SpeedscopeEvent::Type::Close,
            .frameIndex = frameIndex,
            .at         = timeUs,
            .tid        = tid,
        });

        m_EventCount++;
    }

  public:

    /**
     * @brief Configure profiler options
     */
    void SetConfig(const ProfilerConfig &config)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Config = config;
    }

    /**
     * @brief Get current configuration
     */
    ProfilerConfig GetConfig() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Config;
    }

    /**
     * @brief Get profiling statistics
     */
    void GetStats(size_t &eventCount, size_t &droppedEvents) const
    {
        eventCount    = m_EventCount.load();
        droppedEvents = m_DroppedEvents.load();
    }

    /**
     * @brief Check if a session is currently active
     */
    bool IsSessionActive() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_SessionActive;
    }

  private:

    /**
     * @brief Get or create a frame index for a given name
     */
    int GetOrCreateFrame(const std::string &name, const std::string &file, int line)
    {
        // Build full frame name
        std::string fullName = name;
        if (m_Config.bIncludeSourceInfo && !file.empty()) {
            fullName = std::format("{}:{} ({})",
                                   std::filesystem::path(file).filename().string(),
                                   line,
                                   name);
        }

        // Check if frame already exists
        auto it = m_FrameIndexMap.find(fullName);
        if (it != m_FrameIndexMap.end()) {
            return it->second;
        }

        // Create new frame
        int index                 = static_cast<int>(m_Frames.size());
        m_FrameIndexMap[fullName] = index;
        m_Frames.push_back({
            .name = fullName,
            .file = file,
            .line = line,
        });

        return index;
    }

    /**
     * @brief End session (internal, assumes lock is held)
     */
    void EndSessionInternal();

    /**
     * @brief Write events in speedscope JSON format
     *
     * Format specification:
     * https://github.com/jlfwong/speedscope/wiki/Importing-from-custom-sources
     */
    void WriteSpeedscopeJson()
    {
        // Group events by thread
        std::unordered_map<std::string, std::vector<const SpeedscopeEvent *>> eventsByThread;
        for (const auto &event : m_Events) {
            eventsByThread[event.tid].push_back(&event);
        }

        m_OutputStream << "{\n";

        // Schema version
        m_OutputStream << "  \"$schema\": \"https://www.speedscope.app/file-format-schema.json\",\n";

        // Shared frames
        m_OutputStream << "  \"shared\": {\n";
        m_OutputStream << "    \"frames\": [\n";
        for (size_t i = 0; i < m_Frames.size(); i++) {
            const auto &frame = m_Frames[i];
            m_OutputStream << "      {\"name\": \"" << EscapeJson(frame.name) << "\"}";
            if (i < m_Frames.size() - 1) {
                m_OutputStream << ",";
            }
            m_OutputStream << "\n";
        }
        m_OutputStream << "    ]\n";
        m_OutputStream << "  },\n";

        // Profiles (one per thread)
        m_OutputStream << "  \"profiles\": [\n";

        size_t threadIndex = 0;
        for (const auto &[tid, events] : eventsByThread) {
            m_OutputStream << "    {\n";
            m_OutputStream << "      \"type\": \"evented\",\n";
            m_OutputStream << "      \"name\": \"" << m_SessionName << " (Thread " << tid << ")\",\n";
            m_OutputStream << "      \"unit\": \"microseconds\",\n";

            // Find start and end times for this thread
            double startTime = events.empty() ? 0.0 : events.front()->at;
            double endTime   = events.empty() ? 0.0 : events.back()->at;
            m_OutputStream << "      \"startValue\": " << std::fixed << startTime << ",\n";
            m_OutputStream << "      \"endValue\": " << std::fixed << endTime << ",\n";

            // Events
            m_OutputStream << "      \"events\": [\n";
            for (size_t i = 0; i < events.size(); i++) {
                const auto *event = events[i];
                m_OutputStream << "        {";
                m_OutputStream << "\"type\": \"" << static_cast<char>(event->type) << "\", ";
                m_OutputStream << "\"frame\": " << event->frameIndex << ", ";
                m_OutputStream << "\"at\": " << std::fixed << event->at;
                m_OutputStream << "}";
                if (i < events.size() - 1) {
                    m_OutputStream << ",";
                }
                m_OutputStream << "\n";
            }
            m_OutputStream << "      ]\n";
            m_OutputStream << "    }";

            threadIndex++;
            if (threadIndex < eventsByThread.size()) {
                m_OutputStream << ",";
            }
            m_OutputStream << "\n";
        }

        m_OutputStream << "  ],\n";

        // Metadata
        m_OutputStream << "  \"name\": \"" << EscapeJson(m_SessionName) << "\",\n";
        m_OutputStream << "  \"exporter\": \"Neon Engine Instrumentor\"\n";

        m_OutputStream << "}\n";
    }

    /**
     * @brief Escape special characters for JSON string
     */
    static std::string EscapeJson(const std::string &str)
    {
        std::string result;
        result.reserve(str.size() + 10);
        for (char c : str) {
            switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
            }
        }
        return result;
    }
};

//=============================================================================
// InstrumentationTimer - RAII timer for automatic scope measurement
//=============================================================================

/**
 * @brief RAII timer that automatically records begin/end events
 *
 * Usage:
 *   {
 *       InstrumentationTimer timer("MyFunction");
 *       // ... code to profile ...
 *   } // timer automatically records end event when destroyed
 */
struct InstrumentationTimer
{
    using clock_t = std::chrono::steady_clock;

  private:
    uint32_t m_FrameIndex;
    bool     m_Stopped = false;

  public:
    /**
     * @brief Construct timer with name and optional source location
     */
    explicit InstrumentationTimer(const std::string   &name,
                                  std::source_location loc = std::source_location::current())
        : InstrumentationTimer(name.c_str(), loc)
    {
    }

    /**
     * @brief Construct timer with C-string name and optional source location
     */
    explicit InstrumentationTimer(const char          *name,
                                  std::source_location loc = std::source_location::current())
    {
        // Record begin event
        m_FrameIndex = Instrumentor::Get().WriteBeginEvent(name, loc.file_name(), static_cast<int>(loc.line()));
    }

    ~InstrumentationTimer()
    {
        if (!m_Stopped) {
            Stop();
        }
    }

    // Prevent copying/moving
    InstrumentationTimer(const InstrumentationTimer &)            = delete;
    InstrumentationTimer &operator=(const InstrumentationTimer &) = delete;
    InstrumentationTimer(InstrumentationTimer &&)                 = delete;
    InstrumentationTimer &operator=(InstrumentationTimer &&)      = delete;

    /**
     * @brief Manually stop the timer (useful for early exit)
     */
    void Stop()
    {
        if (m_Stopped) {
            return;
        }

        // Record end event (no console output)
        Instrumentor::Get().WriteEndEvent(m_FrameIndex);

        m_Stopped = true;
    }
};

//=============================================================================
// InstrumentationTimerLog - RAII timer that always prints to console
//=============================================================================

/**
 * @brief RAII timer that always prints timing to console (even if global console is disabled)
 *
 * Usage:
 *   {
 *       InstrumentationTimerLog timer("MyFunction");
 *       // ... code to profile ...
 *   } // timer automatically prints timing when destroyed
 */
struct InstrumentationTimerLog
{
    using clock_t = std::chrono::steady_clock;

  private:
    std::string                      m_Name;
    std::string                      m_File;
    int                              m_Line;
    std::chrono::time_point<clock_t> m_StartTime;
    int                              m_FrameIndex;
    bool                             m_Stopped = false;

  public:
    /**
     * @brief Construct timer with name and optional source location (always logs)
     */
    explicit InstrumentationTimerLog(const std::string   &name,
                                     std::source_location loc = std::source_location::current())
        : InstrumentationTimerLog(name.c_str(), loc)
    {
    }

    /**
     * @brief Construct timer with C-string name and optional source location (always logs)
     */
    explicit InstrumentationTimerLog(const char          *name,
                                     std::source_location loc = std::source_location::current())
        : m_Name(name), m_File(loc.file_name()), m_Line(static_cast<int>(loc.line())), m_StartTime(clock_t::now())
    {
        // Record begin event
        m_FrameIndex = Instrumentor::Get().WriteBeginEvent(m_Name, m_File, m_Line);
    }

    ~InstrumentationTimerLog()
    {
        if (!m_Stopped) {
            Stop();
        }
    }

    // Prevent copying/moving
    InstrumentationTimerLog(const InstrumentationTimerLog &)            = delete;
    InstrumentationTimerLog &operator=(const InstrumentationTimerLog &) = delete;
    InstrumentationTimerLog(InstrumentationTimerLog &&)                 = delete;
    InstrumentationTimerLog &operator=(InstrumentationTimerLog &&)      = delete;

    /**
     * @brief Manually stop the timer (useful for early exit)
     */
    void Stop()
    {
        if (m_Stopped) {
            return;
        }

        auto      now        = clock_t::now();
        auto      duration   = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_StartTime);
        long long durationNs = duration.count();

        // Build display name
        std::string displayName = std::format("{}:{} ({})",
                                              std::filesystem::path(m_File).filename().string(),
                                              m_Line,
                                              m_Name);

        // Record end event and always print to console
        Instrumentor::Get().WriteEndEventLog(m_FrameIndex, durationNs, displayName);

        m_Stopped = true;
    }
};

//=============================================================================
// InstrumentationTimerConditional - RAII timer controlled by runtime flag
//=============================================================================

/**
 * @brief RAII timer that only profiles when enabled flag is true
 *
 * Used by YA_PROFILE_CONDITIONAL mode to allow runtime enable/disable.
 * When disabled, constructor/destructor are nearly zero-cost (just a bool check).
 */
struct InstrumentationTimerConditional
{
    using clock_t = std::chrono::steady_clock;

  private:
    int  m_FrameIndex = -1;
    bool m_Enabled    = false;
    bool m_Stopped    = false;

  public:
    /**
     * @brief Construct conditional timer
     * @param enabled If false, no profiling occurs
     * @param name Scope/function name
     * @param loc Source location
     */
    explicit InstrumentationTimerConditional(bool                 enabled,
                                             const std::string   &name,
                                             std::source_location loc = std::source_location::current())
        : InstrumentationTimerConditional(enabled, name.c_str(), loc)
    {
    }

    explicit InstrumentationTimerConditional(bool                 enabled,
                                             const char          *name,
                                             std::source_location loc = std::source_location::current())
        : m_Enabled(enabled)
    {
        if (m_Enabled) {
            m_FrameIndex = Instrumentor::Get().WriteBeginEvent(name, loc.file_name(), static_cast<int>(loc.line()));
        }
    }

    ~InstrumentationTimerConditional()
    {
        if (!m_Stopped) {
            Stop();
        }
    }

    // Prevent copying/moving
    InstrumentationTimerConditional(const InstrumentationTimerConditional &)            = delete;
    InstrumentationTimerConditional &operator=(const InstrumentationTimerConditional &) = delete;
    InstrumentationTimerConditional(InstrumentationTimerConditional &&)                 = delete;
    InstrumentationTimerConditional &operator=(InstrumentationTimerConditional &&)      = delete;

    void Stop()
    {
        if (m_Stopped) {
            return;
        }

        if (m_Enabled && m_FrameIndex >= 0) {
            Instrumentor::Get().WriteEndEvent(m_FrameIndex);
        }

        m_Stopped = true;
    }
};

//=============================================================================
// Legacy ProfileResult structure (for backward compatibility)
//=============================================================================

struct ProfileResult
{
    std::string Name;
    long long   Start, End;
    uint32_t    ThreadID;
};

struct InstrumentationSession
{
    std::string Name;
};

} // namespace ya

//=============================================================================
// Profiling Macros
//=============================================================================

// Platform-specific pretty function name
#if _WIN32
    #define YA_PRETTY_FUNCTION __FUNCSIG__
#elif __linux__
    #define YA_PRETTY_FUNCTION __PRETTY_FUNCTION__
#else
    #define YA_PRETTY_FUNCTION __func__
#endif

// Helper macro for concatenation
#define YA_CONCAT_IMPL(a, b) a##b
#define YA_CONCAT(a, b) YA_CONCAT_IMPL(a, b)

//=============================================================================
// Profile Mode Configuration
//
// Define ONE of the following before including this header (or in project settings):
//   YA_PROFILE_DISABLED    - No profiling code compiled (zero overhead)
//   YA_PROFILE_CONDITIONAL - Profiling controlled by runtime boolean
//   YA_PROFILE_ENABLED     - Profiling always active (default if none specified)
//
// Example:
//   #define YA_PROFILE_DISABLED
//   #include "Instrumentor.h"
//=============================================================================

// Default to ENABLED if no mode is specified
#if !defined(YA_PROFILE_DISABLED) && !defined(YA_PROFILE_CONDITIONAL) && !defined(YA_PROFILE_ENABLED)
    #define YA_PROFILE_DISABLED
#endif

//-----------------------------------------------------------------------------
// Mode 1: YA_PROFILE_DISABLED - Zero overhead, no code compiled
//-----------------------------------------------------------------------------
#if defined(YA_PROFILE_DISABLED)

    #define YA_PROFILE_BEGIN_SESSION_IMPL(session_name, filepath)
    #define YA_PROFILE_END_SESSION_IMPL()
    #define YA_PROFILE_SCOPE_IMPL(name)
    #define YA_PROFILE_SCOPE_LOG_IMPL(name)
    #define YA_PROFILE_SET_ENABLED(enabled)
    #define YA_PROFILE_IS_ENABLED() (false)

//-----------------------------------------------------------------------------
// Mode 2: YA_PROFILE_CONDITIONAL - Runtime control via boolean
//-----------------------------------------------------------------------------
#elif defined(YA_PROFILE_CONDITIONAL)

// Global runtime flag to enable/disable profiling
namespace ya
{
inline bool g_ProfileEnabled = false;
}

    #define YA_PROFILE_SET_ENABLED(enabled) (::ya::g_ProfileEnabled = (enabled))
    #define YA_PROFILE_IS_ENABLED() (::ya::g_ProfileEnabled)

    #define YA_PROFILE_BEGIN_SESSION_IMPL(session_name, filepath)                                       \
        do {                                                                                            \
            if (::ya::g_ProfileEnabled) ::ya::Instrumentor::Get().BeginSession(session_name, filepath); \
        } while (0)

    #define YA_PROFILE_END_SESSION_IMPL()                                       \
        do {                                                                    \
            if (::ya::g_ProfileEnabled) ::ya::Instrumentor::Get().EndSession(); \
        } while (0)

    #define YA_PROFILE_SCOPE_IMPL(name) \
        ::ya::InstrumentationTimerConditional YA_CONCAT(ya_timer_, __LINE__)(::ya::g_ProfileEnabled, name)

    #define YA_PROFILE_SCOPE_LOG_IMPL(name) \
        ::ya::InstrumentationTimerLog YA_CONCAT(ya_timer_log_, __LINE__)(name)

//-----------------------------------------------------------------------------
// Mode 3: YA_PROFILE_ENABLED - Always active (default)
//-----------------------------------------------------------------------------
#elif defined(YA_PROFILE_ENABLED)

    #define YA_PROFILE_SET_ENABLED(enabled) ((void)0)
    #define YA_PROFILE_IS_ENABLED() (true)

    #define YA_PROFILE_BEGIN_SESSION_IMPL(session_name, filepath) \
        ::ya::Instrumentor::Get().BeginSession(session_name, filepath);

    #define YA_PROFILE_END_SESSION_IMPL() \
        ::ya::Instrumentor::Get().EndSession();

    #define YA_PROFILE_SCOPE_IMPL(name) \
        ::ya::InstrumentationTimer YA_CONCAT(ya_timer_, __LINE__)(name);

    #define YA_PROFILE_SCOPE_LOG_IMPL(name) \
        ::ya::InstrumentationTimerLog YA_CONCAT(ya_timer_log_, __LINE__)(name);

#endif

//=============================================================================
// Public API Macros (unified interface, only modify here)
//=============================================================================

#define YA_PROFILE_SESSION_BEGIN(session_name, filepath) YA_PROFILE_BEGIN_SESSION_IMPL(session_name, filepath)
#define YA_PROFILE_SESSION_END() YA_PROFILE_END_SESSION_IMPL()
#define YA_PROFILE_SCOPE(name) YA_PROFILE_SCOPE_IMPL(name)
#define YA_PROFILE_FUNCTION() YA_PROFILE_SCOPE_IMPL(YA_PRETTY_FUNCTION)
#define YA_PROFILE_SCOPE_LOG(name) YA_PROFILE_SCOPE_LOG_IMPL(name)
#define YA_PROFILE_FUNCTION_LOG() YA_PROFILE_SCOPE_LOG_IMPL(YA_PRETTY_FUNCTION)