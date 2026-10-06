#include "Core/Profiling/PerfBenchSources.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

#include <algorithm>

namespace ya::profiling
{

namespace
{

uint64_t currentRevision(const PerfState& perf, FName sampleKey, FName metricKey)
{
    PerfMetricReading reading;
    return perf.tryGetReading(sampleKey, metricKey, reading) ? reading.revision : 0;
}

void recordIfRewritten(const PerfState& perf, FName sampleKey, FName metricKey, std::string_view key, uint64_t& lastRevision, BenchSeriesWriter& out)
{
    PerfMetricReading reading;
    if (!perf.tryGetReading(sampleKey, metricKey, reading) || reading.revision <= lastRevision) {
        return;
    }
    lastRevision = reading.revision;
    out.record(key, reading.value);
}

} // namespace

FrameBenchSource::FrameBenchSource(const PerfState& perf)
    : _perf(perf)
{
}

void FrameBenchSource::onWindowBegin()
{
    _windowStart     = clock_t::now();
    _lastFrameEnd    = _windowStart;
    _frames          = 0;
    _lastCpuRevision = currentRevision(_perf, perf::sample::hostTick(), perf::metric::cpuTimeMs());
    _lastGpuRevision = currentRevision(_perf, perf::sample::hostTick(), perf::metric::gpuTimeMs());
}

void FrameBenchSource::sampleFrame(BenchSeriesWriter& out)
{
    const auto now = clock_t::now();
    out.record("frameMs", static_cast<float>(std::chrono::duration<double, std::milli>(now - _lastFrameEnd).count()));
    _lastFrameEnd = now;
    ++_frames;

    recordIfRewritten(_perf, perf::sample::hostTick(), perf::metric::cpuTimeMs(), "cpuMs", _lastCpuRevision, out);
    recordIfRewritten(_perf, perf::sample::hostTick(), perf::metric::gpuTimeMs(), "gpuMs", _lastGpuRevision, out);
}

void FrameBenchSource::writeFacts(nlohmann::json& section) const
{
    const double seconds = std::chrono::duration<double>(_lastFrameEnd - _windowStart).count();
    section["fps"]       = seconds > 0.0 ? static_cast<double>(_frames) / seconds : 0.0;
}

PerfStageBenchSource::PerfStageBenchSource(const PerfState& perf)
    : _perf(perf)
{
}

void PerfStageBenchSource::onWindowBegin()
{
    uint64_t latest = 0;
    _perf.visitMetrics([&latest](const PerfMetricReading& reading) {
        latest = std::max(latest, reading.revision);
    });
    _lastRevision = latest;
}

void PerfStageBenchSource::sampleFrame(BenchSeriesWriter& out)
{
    const FName& cpuTime = perf::metric::cpuTimeMs();
    uint64_t     latest  = _lastRevision;
    _perf.visitMetrics([&](const PerfMetricReading& reading) {
        if (reading.metricKey != cpuTime || reading.revision <= _lastRevision) {
            return;
        }
        out.record(reading.sampleKey.view(), reading.value);
        latest = std::max(latest, reading.revision);
    });
    _lastRevision = latest;
}

} // namespace ya::profiling
