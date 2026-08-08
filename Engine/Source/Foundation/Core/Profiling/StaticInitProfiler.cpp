#include "StaticInitProfiler.h"
#include "Foundation/Core/Log.h"
#include "Foundation/Core/Profiling/Profiling.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "Foundation/Core/Manager/Facade.h"

namespace ya::profiling
{
using std::localtime;


// ============================================================================
// 静态成员初始化
// ============================================================================

std::atomic<uint64_t>                           StaticInitProfiler::_startTimeNs(0);
std::atomic<uint64_t>                           StaticInitProfiler::_endTimeNs(0);
std::atomic<bool>                               StaticInitProfiler::_hasStarted(false);
std::atomic<bool>                               StaticInitProfiler::_hasEnded(false);
std::vector<StaticInitProfiler::VariableRecord> StaticInitProfiler::_records;
std::mutex                                      StaticInitProfiler::_recordsMutex;

// ============================================================================
// 总耗时统计
// ============================================================================

void StaticInitProfiler::reset()
{
    _startTimeNs.store(0, std::memory_order_relaxed);
    _endTimeNs.store(0, std::memory_order_relaxed);
    _hasStarted.store(false, std::memory_order_relaxed);
    _hasEnded.store(false, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(_recordsMutex);
    _records.clear();
}

void StaticInitProfiler::ensureStarted()
{
    if (_hasStarted.load(std::memory_order_acquire)) {
        return;
    }
    recordStart();
}

void StaticInitProfiler::recordStart()
{
    if (!profiling::isStaticInitEnabled()) {
        return;
    }

    bool expected = false;
    if (!_hasStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    _hasEnded.store(false, std::memory_order_release);
    _endTimeNs.store(0, std::memory_order_relaxed);

    YA_CORE_TRACE_LZ("Static initialization started  {}, {}ns",
                     StaticInitProfiler::nowTimeString(),
                     _startTimeNs.load(std::memory_order_relaxed));
    _startTimeNs.store(getNowNanoseconds(), std::memory_order_release);
}

void StaticInitProfiler::recordEnd()
{
    if (!profiling::isStaticInitEnabled()) {
        return;
    }

    ensureStarted();

    bool expected = false;
    if (!_hasEnded.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    _endTimeNs.store(getNowNanoseconds(), std::memory_order_release);
    YA_CORE_TRACE_LZ("Static initialization ended    {}, {}ns",
                     StaticInitProfiler::nowTimeString(),
                     _endTimeNs.load(std::memory_order_relaxed));

    // 自动打印报告
    printReport();
}

uint64_t StaticInitProfiler::getTotalNanoseconds()
{
    if (!profiling::isStaticInitEnabled()) {
        return 0;
    }

    uint64_t start = _startTimeNs.load(std::memory_order_acquire);
    uint64_t end   = _endTimeNs.load(std::memory_order_acquire);

    if (start == 0) return 0; // 未开始
    if (end == 0) {
        // 仍在初始化中，返回当前耗时
        return getNowNanoseconds() - start;
    }

    return end - start;
}

double StaticInitProfiler::getTotalMilliseconds()
{
    return getTotalNanoseconds() / 1000000.0;
}

void StaticInitProfiler::printReport()
{
    if (!profiling::isStaticInitEnabled()) {
        return;
    }

    uint64_t totalNs = getTotalNanoseconds();
    double   totalMs = totalNs / 1000000.0;

    // 打印总耗时
    YA_CORE_TRACE_LZ("+----------------------------------------------------------+");
    YA_CORE_TRACE_LZ("| Static Initialization Profiler Report                   |");
    YA_CORE_TRACE_LZ("+----------------------------------------------------------+");
    YA_CORE_TRACE_LZ("| Total Time: {:.3f} ms ({} ns)", totalMs, totalNs);

    // 打印单个变量记录（如果有）
    {
        std::lock_guard<std::mutex> lock(_recordsMutex);
        if (!_records.empty()) {
            YA_CORE_TRACE_LZ("+----------------------------------------------------------+");
            YA_CORE_TRACE_LZ("| Top Slow Variables:                                      |");

            // 按耗时排序（降序）
            auto sorted = _records;
            std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.nanoseconds > b.nanoseconds; });

            // 只显示前 10 个最慢的
            size_t count = std::min<size_t>(10, sorted.size());
            for (size_t i = 0; i < count; ++i) {
                const auto &record  = sorted[i];
                double      percent = totalNs > 0 ? (record.nanoseconds * 100.0) / totalNs : 0.0;
                YA_CORE_TRACE_LZ("| {:2}. {:40} {:6.3f} ms ({:5.1f}%)",
                             i + 1,
                             record.name.substr(0, 40), // 截断过长的名字
                             record.milliseconds,
                             percent);
            }

            if (sorted.size() > 10) {
                YA_CORE_TRACE_LZ("|     ... and {} more variables", sorted.size() - 10);
            }
        }
    }

    YA_CORE_TRACE_LZ("+----------------------------------------------------------+");
}

// ============================================================================
// 单个变量耗时统计
// ============================================================================

void StaticInitProfiler::recordVariable(const std::string &name, uint64_t nanoseconds)
{
    if (!profiling::isStaticInitEnabled()) {
        return;
    }

    std::lock_guard<std::mutex> lock(_recordsMutex);
    _records.push_back({
        .name         = name,
        .nanoseconds  = nanoseconds,
        .milliseconds = nanoseconds / 1000000.0,
    });
}

std::vector<StaticInitProfiler::VariableRecord> StaticInitProfiler::getVariableRecords()
{
    if (!profiling::isStaticInitEnabled()) {
        return {};
    }

    std::lock_guard<std::mutex> lock(_recordsMutex);
    return _records;
}

uint64_t StaticInitProfiler::getNowNanoseconds()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               now.time_since_epoch())
        .count();
}

std::string StaticInitProfiler::nowTimeString()
{
    return Facade.clockManager.nowString();
}

// ============================================================================
// StaticInitTimer 实现
// ============================================================================

StaticInitTimer::StaticInitTimer(const std::string &varName)
    : _varName(varName), _start(std::chrono::high_resolution_clock::now())
{
    if (!profiling::isStaticInitEnabled()) {
        _varName.clear();
    }
}

StaticInitTimer::~StaticInitTimer()
{
    if (_varName.empty() || !profiling::isStaticInitEnabled()) {
        return;
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - _start);

    StaticInitProfiler::recordVariable(_varName, duration.count());
}

} // namespace ya::profiling
