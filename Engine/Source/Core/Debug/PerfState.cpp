#include "PerfState.h"

namespace ya
{

namespace
{

float toMilliseconds(std::chrono::steady_clock::duration duration)
{
    return static_cast<float>(std::chrono::duration<double, std::milli>(duration).count());
}

} // namespace

PerfState& PerfState::Get()
{
    static PerfState instance;
    return instance;
}

void PerfState::setEnabled(bool bEnabled)
{
    _bEnabled.store(bEnabled, std::memory_order_relaxed);
}

bool PerfState::isEnabled() const
{
    return _bEnabled.load(std::memory_order_relaxed);
}

void PerfState::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _samples.clear();
    ++_revision;
}

void PerfState::clearMetric(FName sampleKey, FName metricKey)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto sampleIt = _samples.find(sampleKey);
    if (sampleIt == _samples.end()) {
        return;
    }

    sampleIt->second.metrics.erase(metricKey);
    ++_revision;
}

void PerfState::setValue(FName sampleKey, FName metricKey, float value, FName domainKey)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto& sample   = _samples[sampleKey];
    sample.sampleKey = sampleKey;
    if (domainKey.isValid()) {
        sample.domainKey = domainKey;
    }

    auto& metric     = sample.metrics[metricKey];
    metric.lastValue = value;
    metric.revision  = ++_revision;
}

void PerfState::setDuration(FName sampleKey, FName metricKey, std::chrono::steady_clock::duration duration, FName domainKey)
{
    setValue(sampleKey, metricKey, toMilliseconds(duration), domainKey);
}

PerfSampleToken PerfState::beginSample(FName sampleKey, FName metricKey, FName domainKey)
{
    if (!isEnabled()) {
        return {};
    }

    return PerfSampleToken{
        .sampleKey = sampleKey,
        .metricKey = metricKey,
        .domainKey = domainKey,
        .startTime = PerfSampleToken::clock_t::now(),
        .bActive   = true,
    };
}

void PerfState::endSample(PerfSampleToken* token)
{
    if (!token || !token->bActive) {
        return;
    }

    setDuration(token->sampleKey, token->metricKey, PerfSampleToken::clock_t::now() - token->startTime, token->domainKey);
    token->bActive = false;
}

float PerfState::getLastValue(FName sampleKey, FName metricKey, float fallbackValue) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto sampleIt = _samples.find(sampleKey);
    if (sampleIt == _samples.end()) {
        return fallbackValue;
    }

    auto metricIt = sampleIt->second.metrics.find(metricKey);
    if (metricIt == sampleIt->second.metrics.end()) {
        return fallbackValue;
    }

    return metricIt->second.lastValue;
}

PerfScopeTimer::PerfScopeTimer(FName sampleKey, FName metricKey, FName domainKey)
    : _token(PerfState::Get().beginSample(sampleKey, metricKey, domainKey))
{
}

PerfScopeTimer::~PerfScopeTimer()
{
    PerfState::Get().endSample(&_token);
}

PerfScopeTimerConditional::PerfScopeTimerConditional(bool bEnabled, FName sampleKey, FName metricKey, FName domainKey)
    : _bEnabled(bEnabled)
{
    if (_bEnabled) {
        _token = PerfState::Get().beginSample(sampleKey, metricKey, domainKey);
    }
}

PerfScopeTimerConditional::~PerfScopeTimerConditional()
{
    if (_bEnabled) {
        PerfState::Get().endSample(&_token);
    }
}

} // namespace ya