#include "Core/Profiling/PerfBenchSources.h"

#include "Core/Os/OsMemory.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/RenderCounters.h"

#include <algorithm>
#include <string>
#include <unordered_map>

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

PassGpuTimeBenchSource::PassGpuTimeBenchSource(Provider provider)
    : _provider(std::move(provider))
{
}

void PassGpuTimeBenchSource::sampleFrame(BenchSeriesWriter& out)
{
    if (!_provider) {
        return;
    }
    std::unordered_map<std::string, float> passTimesMs;
    _provider(passTimesMs);
    for (const auto& [passName, timeMs] : passTimesMs) {
        out.record(passName, timeMs);
    }
}

void RenderCounterBenchSource::sampleFrame(BenchSeriesWriter& out)
{
    // Read only. The frame boundary owns the reset (see
    // `VulkanRender::beginRecordedFrame`), so the counters mean exactly the
    // frame that just ended whether or not anything is sampling them; resetting
    // here instead would let everything recorded before the first sample pile
    // into one frame's numbers.
    const RenderCounters snapshot = renderCountersSnapshot();

    if (snapshot.drawCount != 0) {
        out.record("drawCount", static_cast<float>(snapshot.drawCount));
    }
    if (snapshot.drawIndexedCount != 0) {
        out.record("drawIndexedCount", static_cast<float>(snapshot.drawIndexedCount));
    }
    if (snapshot.indirectDrawCount != 0) {
        out.record("indirectDrawCount", static_cast<float>(snapshot.indirectDrawCount));
    }
    if (snapshot.dispatchCount != 0) {
        out.record("dispatchCount", static_cast<float>(snapshot.dispatchCount));
    }
    if (snapshot.dispatchIndirectCount != 0) {
        out.record("dispatchIndirectCount", static_cast<float>(snapshot.dispatchIndirectCount));
    }
    if (snapshot.pipelineBindCount != 0) {
        out.record("pipelineBindCount", static_cast<float>(snapshot.pipelineBindCount));
    }
    if (snapshot.descriptorBindCount != 0) {
        out.record("descriptorBindCount", static_cast<float>(snapshot.descriptorBindCount));
    }
    if (snapshot.batch2DCount != 0) {
        out.record("batch2DCount", static_cast<float>(snapshot.batch2DCount));
    }
    if (snapshot.textureTableSwitch2DCount != 0) {
        out.record("textureTableSwitch2DCount", static_cast<float>(snapshot.textureTableSwitch2DCount));
    }
}

void ProcessMemoryBenchSource::sampleFrame(BenchSeriesWriter& out)
{
    const Os::FProcessMemoryStats stats = Os::sampleProcessMemory();
    if (!stats.bSupported) {
        // No counter to read. Recording zeros would turn "unknown" into a
        // measurement in the summary; the fact below says so instead.
        return;
    }
    constexpr double kBytesPerMiB = 1024.0 * 1024.0;
    out.record("rssMiB", static_cast<float>(static_cast<double>(stats.residentBytes) / kBytesPerMiB));
    out.record("peakRssMiB", static_cast<float>(static_cast<double>(stats.peakResidentBytes) / kBytesPerMiB));
}

void ProcessMemoryBenchSource::writeFacts(nlohmann::json& section) const
{
    section["supported"]     = Os::sampleProcessMemory().bSupported;
    section["unifiedMemory"] = Os::hasUnifiedMemory();
}

} // namespace ya::profiling
