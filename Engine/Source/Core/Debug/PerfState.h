#pragma once

#include "Core/FName.h"
#include "Core/Macro/VariadicMacros.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace ya
{

struct PerfMetricState
{
    float    lastValue = 0.0f;
    uint64_t revision  = 0;
};

struct PerfSampleState
{
    FName                                     sampleKey;
    FName                                     domainKey;
    std::unordered_map<FName, PerfMetricState> metrics;
};

struct PerfSampleToken
{
    using clock_t = std::chrono::steady_clock;

    FName               sampleKey;
    FName               metricKey;
    FName               domainKey;
    clock_t::time_point startTime{};
    bool                bActive = false;
};

struct PerfState
{
  private:
    mutable std::mutex                          _mutex;
    std::unordered_map<FName, PerfSampleState> _samples;
    std::atomic<bool>                          _bEnabled{true};
    uint64_t                                   _revision = 0;

  public:
    static PerfState& Get();

    void setEnabled(bool bEnabled);
    bool isEnabled() const;

    void clear();
    void clearMetric(FName sampleKey, FName metricKey);

    void setValue(FName sampleKey, FName metricKey, float value, FName domainKey = {});
    void setDuration(FName sampleKey, FName metricKey, std::chrono::steady_clock::duration duration, FName domainKey = {});

    PerfSampleToken beginSample(FName sampleKey, FName metricKey, FName domainKey = {});
    void            endSample(PerfSampleToken* token);

    float getLastValue(FName sampleKey, FName metricKey, float fallbackValue = 0.0f) const;
};

struct PerfScopeTimer
{
  private:
    PerfSampleToken _token;

  public:
    PerfScopeTimer(FName sampleKey, FName metricKey, FName domainKey = {});
    ~PerfScopeTimer();

    PerfScopeTimer(const PerfScopeTimer&)            = delete;
    PerfScopeTimer& operator=(const PerfScopeTimer&) = delete;
    PerfScopeTimer(PerfScopeTimer&&)                 = delete;
    PerfScopeTimer& operator=(PerfScopeTimer&&)      = delete;
};

struct PerfScopeTimerConditional
{
  private:
    bool            _bEnabled = false;
    PerfSampleToken _token;

  public:
    PerfScopeTimerConditional(bool bEnabled, FName sampleKey, FName metricKey, FName domainKey = {});
    ~PerfScopeTimerConditional();

    PerfScopeTimerConditional(const PerfScopeTimerConditional&)            = delete;
    PerfScopeTimerConditional& operator=(const PerfScopeTimerConditional&) = delete;
    PerfScopeTimerConditional(PerfScopeTimerConditional&&)                 = delete;
    PerfScopeTimerConditional& operator=(PerfScopeTimerConditional&&)      = delete;
};

} // namespace ya

#if !defined(YA_PERF_DISABLED) && !defined(YA_PERF_CONDITIONAL) && !defined(YA_PERF_ENABLED)
    #define YA_PERF_CONDITIONAL
#endif

#if defined(YA_PERF_DISABLED)

    #define YA_PERF_SET_ENABLED(enabled) ((void)0)
    #define YA_PERF_IS_ENABLED() (false)
    #define YA_PERF_SCOPE(sampleKey, metricKey, domainKey)
    #define YA_PERF_FUNCTION(metricKey, domainKey)

#elif defined(YA_PERF_CONDITIONAL)

    #define YA_PERF_SET_ENABLED(enabled) (::ya::PerfState::Get().setEnabled(enabled))
    #define YA_PERF_IS_ENABLED() (::ya::PerfState::Get().isEnabled())
    #define YA_PERF_SCOPE(sampleKey, metricKey, domainKey) \
        ::ya::PerfScopeTimerConditional YA_CONCAT(ya_perf_timer_, __LINE__)(::ya::PerfState::Get().isEnabled(), sampleKey, metricKey, domainKey)
    #define YA_PERF_FUNCTION(metricKey, domainKey) YA_PERF_SCOPE(YA_PRETTY_FUNCTION, metricKey, domainKey)

#elif defined(YA_PERF_ENABLED)

    #define YA_PERF_SET_ENABLED(enabled) ((void)0)
    #define YA_PERF_IS_ENABLED() (true)
    #define YA_PERF_SCOPE(sampleKey, metricKey, domainKey) \
        ::ya::PerfScopeTimer YA_CONCAT(ya_perf_timer_, __LINE__)(sampleKey, metricKey, domainKey)
    #define YA_PERF_FUNCTION(metricKey, domainKey) YA_PERF_SCOPE(YA_PRETTY_FUNCTION, metricKey, domainKey)

#endif