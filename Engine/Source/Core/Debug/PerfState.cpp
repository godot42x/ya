#include "PerfState.h"

#include <algorithm>
#include <iterator>

namespace ya
{

namespace
{

float toMilliseconds(std::chrono::steady_clock::duration duration)
{
    return static_cast<float>(std::chrono::duration<double, std::milli>(duration).count());
}

size_t normalizeAverageWindowSize(size_t windowSize)
{
    size_t bestWindow = PerfMetricState::AVERAGE_WINDOWS[0];
    size_t bestDelta  = windowSize > bestWindow ? windowSize - bestWindow : bestWindow - windowSize;
    for (size_t candidate : PerfMetricState::AVERAGE_WINDOWS) {
        const size_t delta = windowSize > candidate ? windowSize - candidate : candidate - windowSize;
        if (delta < bestDelta) {
            bestWindow = candidate;
            bestDelta  = delta;
        }
    }
    return bestWindow;
}

size_t averageWindowIndex(size_t windowSize)
{
    const size_t normalizedWindow = normalizeAverageWindowSize(windowSize);
    for (size_t i = 0; i < PerfMetricState::AVERAGE_WINDOWS.size(); ++i) {
        if (PerfMetricState::AVERAGE_WINDOWS[i] == normalizedWindow) {
            return i;
        }
    }
    return 0;
}

void recordAverageSample(PerfMetricState& metric, float value)
{
    for (size_t i = 0; i < PerfMetricState::AVERAGE_WINDOWS.size(); ++i) {
        const size_t windowSize = PerfMetricState::AVERAGE_WINDOWS[i];
        if (metric.averageSampleCount >= windowSize) {
            const size_t oldValueIndex = (metric.averageWriteIndex + PerfMetricState::AVERAGE_BUFFER_SIZE - windowSize) % PerfMetricState::AVERAGE_BUFFER_SIZE;
            metric.averageSums[i] -= metric.averageBuffer[oldValueIndex];
        }
        metric.averageSums[i] += value;
    }

    metric.averageBuffer[metric.averageWriteIndex] = value;
    metric.averageWriteIndex                       = (metric.averageWriteIndex + 1) % PerfMetricState::AVERAGE_BUFFER_SIZE;
    metric.averageSampleCount                      = std::min(metric.averageSampleCount + 1, PerfMetricState::AVERAGE_BUFFER_SIZE);
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
    recordAverageSample(metric, value);
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

float PerfState::getAverageValue(FName sampleKey, FName metricKey, size_t windowSize, float fallbackValue) const
{
    if (windowSize <= 1) {
        return getLastValue(sampleKey, metricKey, fallbackValue);
    }

    std::lock_guard<std::mutex> lock(_mutex);
    auto sampleIt = _samples.find(sampleKey);
    if (sampleIt == _samples.end()) {
        return fallbackValue;
    }

    auto metricIt = sampleIt->second.metrics.find(metricKey);
    if (metricIt == sampleIt->second.metrics.end()) {
        return fallbackValue;
    }

    const auto& metric      = metricIt->second;
    const size_t windowIdx  = averageWindowIndex(windowSize);
    const size_t sampleSize = std::min(metric.averageSampleCount, PerfMetricState::AVERAGE_WINDOWS[windowIdx]);
    if (sampleSize == 0) {
        return fallbackValue;
    }

    return metric.averageSums[windowIdx] / static_cast<float>(sampleSize);
}

float PerfState::getDisplayValue(FName sampleKey, FName metricKey, float fallbackValue) const
{
    return getAverageValue(sampleKey, metricKey, getAverageWindowSize(), fallbackValue);
}

void PerfState::setAverageWindowSize(size_t windowSize)
{
    _averageWindowSize.store(normalizeAverageWindowSize(windowSize), std::memory_order_relaxed);
}

size_t PerfState::getAverageWindowSize() const
{
    return _averageWindowSize.load(std::memory_order_relaxed);
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

PerfFrameScopeTimerConditional::PerfFrameScopeTimerConditional(bool bEnabled,
                                                               FName frameSampleKey,
                                                               FName metricKey,
                                                               FName domainKey,
                                                               FName unaccountedSampleKey,
                                                               std::initializer_list<FName> accountedSampleKeys)
    : _bEnabled(bEnabled),
      _metricKey(metricKey),
      _domainKey(domainKey),
      _unaccountedSampleKey(unaccountedSampleKey)
{
    _accountedSampleCount = std::min(accountedSampleKeys.size(), _accountedSampleKeys.size());
    std::copy_n(accountedSampleKeys.begin(), _accountedSampleCount, _accountedSampleKeys.begin());

    if (_bEnabled) {
        _token = PerfState::Get().beginSample(frameSampleKey, metricKey, domainKey);
    }
}

PerfFrameScopeTimerConditional::~PerfFrameScopeTimerConditional()
{
    if (!_bEnabled) {
        return;
    }

    auto& perf = PerfState::Get();
    perf.endSample(&_token);

    float accountedValue = 0.0f;
    for (size_t i = 0; i < _accountedSampleCount; ++i) {
        accountedValue += perf.getLastValue(_accountedSampleKeys[i], _metricKey);
    }

    const float frameValue = perf.getLastValue(_token.sampleKey, _metricKey);
    perf.setValue(_unaccountedSampleKey, _metricKey, std::max(0.0f, frameValue - accountedValue), _domainKey);
}

} // namespace ya