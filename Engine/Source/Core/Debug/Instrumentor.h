//
// Created by nono on 10/14/23.
//

#ifndef HAZEL_INSTRUMENTOR_H
    #define HAZEL_INSTRUMENTOR_H

    #include <chrono>
    #include <fstream>
    #include <string>
    #include <thread>


    #include "Core/Base.h"



namespace ya
{



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

class Instrumentor
{
    InstrumentationSession *m_CurrentSession = nullptr;
    std::ofstream           m_OutputStream;

    size_t m_ProfileCount = 0;

  public:
    Instrumentor() = default;

    void BeginSession(const std::string &name, const std::string &filepath = "results.json");
    void EndSession();

    void WriteHeader();
    void WriteProfile(const ProfileResult &result);
    void WriteFooter();

    static Instrumentor &Get()
    {
        static Instrumentor instance;
        return instance;
    }
};

class InstrumentationTimer
{
    using clock_t = std::chrono::steady_clock;

    std::string                      _name;
    std::chrono::time_point<clock_t> _startTime;
    bool                             bStop = false;

  public:
    explicit InstrumentationTimer(const char *name, std::source_location loc = std::source_location::current())
    {
        // TODO: optimize the name performance
        _name      = std::format("{}:{} ({})",
                            std::filesystem::path(loc.file_name()).filename().string(),
                            loc.line(),
                            name);
        _startTime = std::chrono::high_resolution_clock::now();
    }

    ~InstrumentationTimer()
    {
        if (!bStop) {
            stop();
        }
    }

    void stop()
    {
        auto dur = clock_t::now() - _startTime;
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();


        uint32_t threadID = (uint32_t)std::hash<std::thread::id>{}(std::this_thread::get_id());
        // Instrumentor::Get().WriteProfile({.Name = _name, .Start = start, .End = end, .ThreadID = threadID});
        YA_CORE_DEBUG("Profile: [{0}] {1}ms,  on thread {2}", _name, ms, threadID);

        bStop = true;
    }
};

}; // namespace ya

#endif


// TODO: Stores data for drawing on overlay
#ifndef PROFILING_WITH_OVERLAY
#endif


#if _WIN32
    #define THE_PRETTY_FUNCTION __FUNCSIG__
#elif __linux__
    #define THE_PRETTY_FUNCTION __PRETTY_FUNCTION__
#else
    #error Need your implementation
#endif

#define ENABLE_PROFILING 1
#if ENABLE_PROFILING
    #define YA_PROFILE_SESSION_BEGIN(session_name, filepath) ::ya::Instrumentor::Get().BeginSession(session_name, filepath)
    #define YA_PROFILE_SESSION_END() ::ya::Instrumentor::Get().EndSession()
    #define YA_PROFILE_SCOPE(name) ::ya::InstrumentationTimer timer##__LINE__(name)
    #define YA_PROFILE_FUNCTION() YA_PROFILE_SCOPE(THE_PRETTY_FUNCTION)
#else
    #define YA_PROFILE_SESSION_BEGIN(session_name, filepath)
    #define YA_PROFILE_SESSION_END()
    #define YA_PROFILE_SCOPE(name)
    #define YA_PROFILE_FUNCTION() ;
#endif