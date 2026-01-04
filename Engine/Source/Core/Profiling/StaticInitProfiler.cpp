#include "StaticInitProfiler.h"
#include "Core/Log.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "Core/Manager/Facade.h"

namespace ya::profiling
{
using std::localtime;


// ============================================================================
// 静态成员初始化
// ============================================================================

std::atomic<uint64_t>                           StaticInitProfiler::_startTimeNs(0);
std::atomic<uint64_t>                           StaticInitProfiler::_endTimeNs(0);
std::vector<StaticInitProfiler::VariableRecord> StaticInitProfiler::_records;
std::mutex                                      StaticInitProfiler::_recordsMutex;

// ============================================================================
// 总耗时统计
// ============================================================================

void StaticInitProfiler::recordStart()
{
    YA_CORE_TRACE_LZ("Static initialization started  {}, {}ns",
                     StaticInitProfiler::nowTimeString(),
                     _startTimeNs.load());
    _startTimeNs = getNowNanoseconds();
}

void StaticInitProfiler::recordEnd()
{
    _endTimeNs = getNowNanoseconds();
    YA_CORE_TRACE_LZ("Static initialization ended    {}, {}ns",
                     StaticInitProfiler::nowTimeString(),
                     _endTimeNs.load());

    // 自动打印报告
    printReport();
}

uint64_t StaticInitProfiler::getTotalNanoseconds()
{
    uint64_t start = _startTimeNs.load();
    uint64_t end   = _endTimeNs.load();

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
    uint64_t totalNs = getTotalNanoseconds();
    double   totalMs = totalNs / 1000000.0;

    // 打印总耗时
    YA_CORE_INFO("+----------------------------------------------------------+");
    YA_CORE_INFO("| Static Initialization Profiler Report                   |");
    YA_CORE_INFO("+----------------------------------------------------------+");
    YA_CORE_INFO("| Total Time: {:.3f} ms ({} ns)", totalMs, totalNs);

    // 打印单个变量记录（如果有）
    {
        std::lock_guard<std::mutex> lock(_recordsMutex);
        if (!_records.empty()) {
            YA_CORE_INFO("+----------------------------------------------------------+");
            YA_CORE_INFO("| Top Slow Variables:                                      |");

            // 按耗时排序（降序）
            auto sorted = _records;
            std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.nanoseconds > b.nanoseconds; });

            // 只显示前 10 个最慢的
            size_t count = std::min<size_t>(10, sorted.size());
            for (size_t i = 0; i < count; ++i) {
                const auto &record  = sorted[i];
                double      percent = totalNs > 0 ? (record.nanoseconds * 100.0) / totalNs : 0.0;
                YA_CORE_INFO("| {:2}. {:40} {:6.3f} ms ({:5.1f}%)",
                             i + 1,
                             record.name.substr(0, 40), // 截断过长的名字
                             record.milliseconds,
                             percent);
            }

            if (sorted.size() > 10) {
                YA_CORE_INFO("|     ... and {} more variables", sorted.size() - 10);
            }
        }
    }

    YA_CORE_INFO("+----------------------------------------------------------+");
}

// ============================================================================
// 单个变量耗时统计
// ============================================================================

void StaticInitProfiler::recordVariable(const std::string &name, uint64_t nanoseconds)
{
    std::lock_guard<std::mutex> lock(_recordsMutex);
    _records.push_back({
        .name         = name,
        .nanoseconds  = nanoseconds,
        .milliseconds = nanoseconds / 1000000.0,
    });
}

std::vector<StaticInitProfiler::VariableRecord> StaticInitProfiler::getVariableRecords()
{
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
}

StaticInitTimer::~StaticInitTimer()
{
    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - _start);

    StaticInitProfiler::recordVariable(_varName, duration.count());
}

} // namespace ya::profiling
