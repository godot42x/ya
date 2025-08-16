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

#if ENABLE_PROFILING
    #define HZ_PROFILE_SESSION_BEGIN(session_name, filepath) ::hazel::Instrumentor::Get().BeginSession(session_name, filepath)
    #define HZ_PROFILE_SESSION_END() ::hazel::Instrumentor::Get().EndSession()
    #define HZ_PROFILE_SCOPE(name) ::hazel::InstrumentationTimer timer##__LINE__(name)
    #define HZ_PROFILE_FUNCTION() HZ_PROFILE_SCOPE(THE_PRETTY_FUNCTION)
#else
    #define HZ_PROFILE_SESSION_BEGIN(session_name, filepath)
    #define HZ_PROFILE_SESSION_END()
    #define HZ_PROFILE_SCOPE(name)
    #define HZ_PROFILE_FUNCTION() ;
#endif

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
    const char                                                 *m_Name;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTimePoint;
    bool                                                        m_Stopped = false;

  public:
    explicit InstrumentationTimer(const char *name)
        : m_Name(name), m_Stopped(false)
    {
        m_StartTimePoint = std::chrono::high_resolution_clock::now();
    }

    ~InstrumentationTimer()
    {
        if (!m_Stopped)
            Stop();
    }

    void Stop()
    {
        auto end_tp = std::chrono::high_resolution_clock::now();

        long long start = std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimePoint).time_since_epoch().count();
        long long end   = std::chrono::time_point_cast<std::chrono::microseconds>(end_tp).time_since_epoch().count();

        uint32_t threadID = std::hash<std::thread::id>{}(std::this_thread::get_id());
        Instrumentor::Get().WriteProfile({.Name = m_Name, .Start = start, .End = end, .ThreadID = threadID});

        m_Stopped = true;
    }
};

}; // namespace ya

#endif
