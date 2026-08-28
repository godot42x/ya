//
// Created by nono on 10/14/23.
// Extended for speedscope JSON format support
//


#pragma once

#include "Core/Profiling/Profiling.h"

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
#include "Core/Macro/VariadicMacros.h"

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
 *   1. Call beginSession() at program start
 *   2. Use YA_PROFILE_SCOPE/YA_PROFILE_FUNCTION macros in code
 *   3. Call endSession() at program end
 *   4. Open the .json file in https://www.speedscope.app/
 *
 * The output JSON conforms to the speedscope file format specification:
 * https://github.com/jlfwong/speedscope/wiki/Importing-from-custom-sources
 */
struct YA_CORE_API Instrumentor
{
  private:
    // Session state
    bool          _sessionActive = false;
    std::string   _sessionName;
    std::ofstream _outputStream;

    // Thread safety
    mutable std::mutex _mutex;

    // Event storage (buffered for batch writing)
    std::vector<SpeedscopeEvent> _events;
    std::vector<SpeedscopeFrame> _frames;
    std::filesystem::path        _outputPath;

    // Frame deduplication: name -> frame index
    std::unordered_map<std::string, int> _frameIndexMap;

    // Timing
    std::chrono::steady_clock::time_point _sessionStartTime;

    // Configuration
    ProfilerConfig _config;

    // Statistics
    std::atomic<size_t> _eventCount{0};
    std::atomic<size_t> _droppedEvents{0};

  public:
    Instrumentor() = default;
    ~Instrumentor()
    {
        if (_sessionActive) {
            endSession();
        }
    }

    // Prevent copying
    Instrumentor(const Instrumentor &)            = delete;
    Instrumentor &operator=(const Instrumentor &) = delete;

    /**
     * @brief Get the singleton instance
     */
    static Instrumentor &get()
    {
        static Instrumentor instance;
        return instance;
    }

    void beginSession(const std::string &name, const std::string &filepath = "profile.speedscope.json");
    void endSession();

    /**
     * @brief Record a begin event for a scope/function
     *
     * @param name Scope/function name
     * @param file Source file (optional)
     * @param line Source line (optional)
     * @return Frame index for use with writeEndEvent
     */
    uint32_t writeBeginEvent(const std::string &name, const std::string &file = "", int line = 0)
    {
        if (!_sessionActive) {
            return static_cast<uint32_t>(-1);
        }

        std::lock_guard<std::mutex> lock(_mutex);

        uint32_t frameIndex = getOrCreateFrame(name, file, line);

        // Calculate time since session start in microseconds
        auto   now      = std::chrono::steady_clock::now();
        auto   duration = std::chrono::duration_cast<std::chrono::microseconds>(now - _sessionStartTime);
        double timeUs   = static_cast<double>(duration.count());

        // Get thread ID as string
        std::string tid = std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));

        // Record event
        _events.push_back({
            .type       = SpeedscopeEvent::Type::Open,
            .frameIndex = frameIndex,
            .at         = timeUs,
            .tid        = tid,
        });

        _eventCount++;

        return frameIndex;
    }

    /**
     * @brief Record an end event for a scope/function (no console output)
     *
     * @param frameIndex Frame index returned by writeBeginEvent
     */
    void writeEndEvent(uint32_t frameIndex)
    {
        writeEndEventInternal(frameIndex);
    }

    /**
     * @brief Record an end event and ALWAYS print to console
     *
     * @param frameIndex Frame index returned by writeBeginEvent
     * @param durationNs Duration in nanoseconds
     * @param name Name for console output
     */
    void writeEndEventLog(int frameIndex, long long durationNs, const std::string &name)
    {
        // Always print to console regardless of config
        if (!name.empty() && durationNs > 0) {
            printToConsole(name, durationNs);
        }

        writeEndEventInternal(frameIndex);
    }

  private:
    /**
     * @brief Print timing info to console
     */
    void printToConsole(const std::string &name, long long durationNs)
    {
        float ms = static_cast<float>(durationNs) / 1000000.0f;
        YA_CORE_DEBUG("[Profile] {}: {:.3f}ms ({} ns)", name, ms, durationNs);
    }

    /**
     * @brief Internal end event recording (without console output)
     */
    void writeEndEventInternal(uint32_t frameIndex)
    {
        if (!_sessionActive || frameIndex < 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);

        // Calculate time since session start in microseconds
        auto   now      = std::chrono::steady_clock::now();
        auto   duration = std::chrono::duration_cast<std::chrono::microseconds>(now - _sessionStartTime);
        double timeUs   = static_cast<double>(duration.count());

        // Get thread ID as string
        std::string tid = std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));

        // Record event
        _events.push_back({
            .type       = SpeedscopeEvent::Type::Close,
            .frameIndex = frameIndex,
            .at         = timeUs,
            .tid        = tid,
        });

        _eventCount++;
    }

  public:

    /**
     * @brief Configure profiler options
     */
    void setConfig(const ProfilerConfig &config)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _config = config;
    }

    /**
     * @brief Get current configuration
     */
    ProfilerConfig getConfig() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _config;
    }

    /**
     * @brief Get profiling statistics
     */
    void getStats(size_t &eventCount, size_t &droppedEvents) const
    {
        eventCount    = _eventCount.load();
        droppedEvents = _droppedEvents.load();
    }

    /**
     * @brief Check if a session is currently active
     */
    bool isSessionActive() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _sessionActive;
    }

  private:

    /**
     * @brief Get or create a frame index for a given name
     */
    int getOrCreateFrame(const std::string &name, const std::string &file, int line)
    {
        // Build full frame name
        std::string fullName = name;
        if (_config.bIncludeSourceInfo && !file.empty()) {
            fullName = std::format("{}:{} ({})",
                                   std::filesystem::path(file).filename().string(),
                                   line,
                                   name);
        }

        // Check if frame already exists
        auto it = _frameIndexMap.find(fullName);
        if (it != _frameIndexMap.end()) {
            return it->second;
        }

        // Create new frame
        int index                = static_cast<int>(_frames.size());
        _frameIndexMap[fullName] = index;
        _frames.push_back({
            .name = fullName,
            .file = file,
            .line = line,
        });

        return index;
    }

    /**
     * @brief End session (internal, assumes lock is held)
     */
    void endSessionInternal();

    /**
     * @brief Write events in speedscope JSON format
     *
     * Format specification:
     * https://github.com/jlfwong/speedscope/wiki/Importing-from-custom-sources
     */
    void writeSpeedscopeJson()
    {
        // Group events by thread
        std::unordered_map<std::string, std::vector<const SpeedscopeEvent *>> eventsByThread;
        for (const auto &event : _events) {
            eventsByThread[event.tid].push_back(&event);
        }

        _outputStream << "{\n";

        // Schema version
        _outputStream << "  \"$schema\": \"https://www.speedscope.app/file-format-schema.json\",\n";

        // Shared frames
        _outputStream << "  \"shared\": {\n";
        _outputStream << "    \"frames\": [\n";
        for (size_t i = 0; i < _frames.size(); i++) {
            const auto &frame = _frames[i];
            _outputStream << "      {\"name\": \"" << escapeJson(frame.name) << "\"}";
            if (i < _frames.size() - 1) {
                _outputStream << ",";
            }
            _outputStream << "\n";
        }
        _outputStream << "    ]\n";
        _outputStream << "  },\n";

        // Profiles (one per thread)
        _outputStream << "  \"profiles\": [\n";

        size_t threadIndex = 0;
        for (const auto &[tid, events] : eventsByThread) {
            _outputStream << "    {\n";
            _outputStream << "      \"type\": \"evented\",\n";
            _outputStream << "      \"name\": \"" << _sessionName << " (Thread " << tid << ")\",\n";
            _outputStream << "      \"unit\": \"microseconds\",\n";

            // Find start and end times for this thread
            double startTime = events.empty() ? 0.0 : events.front()->at;
            double endTime   = events.empty() ? 0.0 : events.back()->at;
            _outputStream << "      \"startValue\": " << std::fixed << startTime << ",\n";
            _outputStream << "      \"endValue\": " << std::fixed << endTime << ",\n";

            // Events
            _outputStream << "      \"events\": [\n";
            for (size_t i = 0; i < events.size(); i++) {
                const auto *event = events[i];
                _outputStream << "        {";
                _outputStream << "\"type\": \"" << static_cast<char>(event->type) << "\", ";
                _outputStream << "\"frame\": " << event->frameIndex << ", ";
                _outputStream << "\"at\": " << std::fixed << event->at;
                _outputStream << "}";
                if (i < events.size() - 1) {
                    _outputStream << ",";
                }
                _outputStream << "\n";
            }
            _outputStream << "      ]\n";
            _outputStream << "    }";

            threadIndex++;
            if (threadIndex < eventsByThread.size()) {
                _outputStream << ",";
            }
            _outputStream << "\n";
        }

        _outputStream << "  ],\n";

        // Metadata
        _outputStream << "  \"name\": \"" << escapeJson(_sessionName) << "\",\n";
        _outputStream << "  \"exporter\": \"Neon Engine Instrumentor\"\n";

        _outputStream << "}\n";
    }

    /**
     * @brief Escape special characters for JSON string
     */
    static std::string escapeJson(const std::string &str)
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
    uint32_t _frameIndex;
    bool     _stopped = false;

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
        _frameIndex = Instrumentor::get().writeBeginEvent(name, loc.file_name(), static_cast<int>(loc.line()));
    }

    ~InstrumentationTimer()
    {
        if (!_stopped) {
            stop();
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
    void stop()
    {
        if (_stopped) {
            return;
        }

        Instrumentor::get().writeEndEvent(_frameIndex);

        _stopped = true;
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
    std::string                      _name;
    std::string                      _file;
    int                              _line;
    std::chrono::time_point<clock_t> _startTime;
    int                              _frameIndex;
    bool                             _stopped = false;

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
        : _name(name), _file(loc.file_name()), _line(static_cast<int>(loc.line())), _startTime(clock_t::now())
    {
        _frameIndex = Instrumentor::get().writeBeginEvent(_name, _file, _line);
    }

    ~InstrumentationTimerLog()
    {
        if (!_stopped) {
            stop();
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
    void stop()
    {
        if (_stopped) {
            return;
        }

        auto      now        = clock_t::now();
        auto      duration   = std::chrono::duration_cast<std::chrono::nanoseconds>(now - _startTime);
        long long durationNs = duration.count();

        std::string displayName = std::format("{}:{} ({})",
                                              std::filesystem::path(_file).filename().string(),
                                              _line,
                                              _name);

        Instrumentor::get().writeEndEventLog(_frameIndex, durationNs, displayName);

        _stopped = true;
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
    int  _frameIndex = -1;
    bool _enabled    = false;
    bool _stopped    = false;

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
        : _enabled(enabled)
    {
        if (_enabled) {
            _frameIndex = Instrumentor::get().writeBeginEvent(name, loc.file_name(), static_cast<int>(loc.line()));
        }
    }

    ~InstrumentationTimerConditional()
    {
        if (!_stopped) {
            stop();
        }
    }

    // Prevent copying/moving
    InstrumentationTimerConditional(const InstrumentationTimerConditional &)            = delete;
    InstrumentationTimerConditional &operator=(const InstrumentationTimerConditional &) = delete;
    InstrumentationTimerConditional(InstrumentationTimerConditional &&)                 = delete;
    InstrumentationTimerConditional &operator=(InstrumentationTimerConditional &&)      = delete;

    void stop()
    {
        if (_stopped) {
            return;
        }

        if (_enabled && _frameIndex >= 0) {
            Instrumentor::get().writeEndEvent(_frameIndex);
        }

        _stopped = true;
    }
};

//=============================================================================
// InstrumentationTimerConsoleOnly - RAII timer that only prints to console
//=============================================================================

/**
 * @brief RAII timer that ONLY prints to console (no file output)
 *
 * This timer does not interact with the Instrumentor at all.
 * It simply measures time and prints to console on destruction.
 * Works in all profile modes including YA_PROFILE_DISABLED.
 */
struct InstrumentationTimerConsoleOnly
{
    using clock_t = std::chrono::steady_clock;

  private:
    std::string                      _name;
    std::string                      _file;
    int                              _line;
    std::chrono::time_point<clock_t> _startTime;
    bool                             _stopped = false;
    bool                             bEnable  = false;

  public:
    explicit InstrumentationTimerConsoleOnly(const std::string   &name,
                                             std::source_location loc     = std::source_location::current(),
                                             bool                 bEnable = true)
        : InstrumentationTimerConsoleOnly(name.c_str(), loc, bEnable)
    {
    }

    explicit InstrumentationTimerConsoleOnly(const char          *name,
                                             std::source_location loc     = std::source_location::current(),
                                             bool                 bEnable = true)
        : _name(name), _file(loc.file_name()), _line(static_cast<int>(loc.line())), bEnable(bEnable)
    {
        if (bEnable) {
            _startTime = clock_t::now();
        }
    }

    ~InstrumentationTimerConsoleOnly()
    {
        if (!_stopped) {
            stop();
        }
    }

    // Prevent copying/moving
    InstrumentationTimerConsoleOnly(const InstrumentationTimerConsoleOnly &)            = delete;
    InstrumentationTimerConsoleOnly &operator=(const InstrumentationTimerConsoleOnly &) = delete;
    InstrumentationTimerConsoleOnly(InstrumentationTimerConsoleOnly &&)                 = delete;
    InstrumentationTimerConsoleOnly &operator=(InstrumentationTimerConsoleOnly &&)      = delete;

    void stop()
    {
        if (_stopped || !bEnable) {
            return;
        }

        auto      now        = clock_t::now();
        auto      duration   = std::chrono::duration_cast<std::chrono::nanoseconds>(now - _startTime);
        long long durationNs = duration.count();

        // Build display name with source location
        std::string displayName = std::format("{}:{} ({})",
                                              std::filesystem::path(_file).filename().string(),
                                              _line,
                                              _name);

        // Print to console only (no file output)
        float ms = static_cast<float>(durationNs) / 1000000.0f;
        YA_CORE_DEBUG("[Profile] {}: {:.3f}ms ({} ns)", displayName, ms, durationNs);

        _stopped = true;
    }
};

//=============================================================================
// Legacy ProfileResult structure (for backward compatibility)
//=============================================================================

struct ProfileResult
{
    std::string name;
    long long   start;
    long long   end;
    uint32_t    threadId;
};

struct InstrumentationSession
{
    std::string name;
};

} // namespace ya

//=============================================================================
// Profiling Macros
//=============================================================================

//=============================================================================
// Profile Mode Configuration
//
// XMake selects the unified profiling mode. Legacy YA_PROFILE_* defines are
// normalized by Core/Profiling/Profiling.h.
//=============================================================================

//-----------------------------------------------------------------------------
// Mode 1: YA_PROFILE_DISABLED - Zero overhead, no code compiled
//-----------------------------------------------------------------------------
#if defined(YA_PROFILE_DISABLED)

    #define YA_PROFILE_BEGIN_SESSION_IMPL(session_name, filepath) do { (void)0; } while (0);
    #define YA_PROFILE_END_SESSION_IMPL() do { (void)0; } while (0);
    #define YA_PROFILE_SCOPE_IMPL(name) do { (void)0; } while (0);

    #define YA_PROFILE_SET_ENABLED(enabled) do { (void)sizeof(enabled); } while (0)
    #define YA_PROFILE_IS_ENABLED() (false)

//-----------------------------------------------------------------------------
// Mode 2: YA_PROFILE_CONDITIONAL - Runtime control via boolean
//-----------------------------------------------------------------------------
#elif defined(YA_PROFILE_CONDITIONAL)

    #define YA_PROFILE_SET_ENABLED(enabled) (::ya::profiling::setCpuTraceEnabled(enabled))
    #define YA_PROFILE_IS_ENABLED() (::ya::profiling::isCpuTraceEnabled())

    #define YA_PROFILE_BEGIN_SESSION_IMPL(session_name, filepath)                                        \
        do {                                                                                             \
            if (::ya::profiling::isCpuTraceEnabled()) ::ya::Instrumentor::get().beginSession(session_name, filepath); \
        } while (0)

    #define YA_PROFILE_END_SESSION_IMPL()                                        \
        do {                                                                     \
            if (::ya::profiling::isCpuTraceEnabled()) ::ya::Instrumentor::get().endSession();  \
        } while (0)

    #define YA_PROFILE_SCOPE_IMPL(name) \
        ::ya::InstrumentationTimerConditional YA_CONCAT(ya_timer_, __LINE__)(::ya::profiling::isCpuTraceEnabled(), name);

//-----------------------------------------------------------------------------
// Mode 3: YA_PROFILE_ENABLED - Always active
//-----------------------------------------------------------------------------
#elif defined(YA_PROFILE_ENABLED)

    #define YA_PROFILE_SET_ENABLED(enabled) ((void)0)
    #define YA_PROFILE_IS_ENABLED() (true)

    #define YA_PROFILE_BEGIN_SESSION_IMPL(session_name, filepath) \
        ::ya::Instrumentor::get().beginSession(session_name, filepath)

    #define YA_PROFILE_END_SESSION_IMPL() \
        ::ya::Instrumentor::get().endSession()

    #define YA_PROFILE_SCOPE_IMPL(name) \
        ::ya::InstrumentationTimer YA_CONCAT(ya_timer_, __LINE__)(name);

#endif

//=============================================================================
// Console-only logging macro (always available, independent of profile mode)
//=============================================================================
#define YA_PROFILE_SCOPE_LOG_IMPL(name) \
    ::ya::InstrumentationTimerConsoleOnly YA_CONCAT(ya_timer_log_, __LINE__)(name);
  // YA_CALL_MACRO_N(__YA_PROFILE_SCOPE_LOG_IMPL_, __VA_ARGS__)

#define __YA_PROFILE_SCOPE_LOG_IMPL_1(name) \
    ::ya::InstrumentationTimerConsoleOnly YA_CONCAT(ya_timer_log_, __LINE__)(name);

#define __YA_PROFILE_SCOPE_LOG_IMPL_2(name, bEnable) \
    ::ya::InstrumentationTimerConsoleOnly YA_CONCAT(ya_timer_log_, __LINE__)(name);

//=============================================================================
// Public API Macros (unified interface, only modify here)
//=============================================================================

#define YA_PROFILE_SESSION_BEGIN(session_name, filepath) YA_PROFILE_BEGIN_SESSION_IMPL(session_name, filepath)
#define YA_PROFILE_SESSION_END() YA_PROFILE_END_SESSION_IMPL()
#define YA_PROFILE_SCOPE(name) YA_PROFILE_SCOPE_IMPL(name)
#define YA_PROFILE_FUNCTION() YA_PROFILE_SCOPE_IMPL(YA_PRETTY_FUNCTION)
#define YA_PROFILE_SCOPE_LOG(name) YA_PROFILE_SCOPE_LOG_IMPL(name)
#define YA_PROFILE_FUNCTION_LOG() YA_PROFILE_SCOPE_LOG_IMPL(YA_PRETTY_FUNCTION)

namespace ya::profile
{
using CpuTrace = ::ya::Instrumentor;

inline CpuTrace& cpuTrace()
{
    return ::ya::profiling::cpuTrace();
}

inline bool isCpuTraceEnabled()
{
    return ::ya::profiling::isCpuTraceEnabled();
}

inline void setCpuTraceEnabled(bool enabled)
{
    ::ya::profiling::setCpuTraceEnabled(enabled);
}
} // namespace ya::profile
