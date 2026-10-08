#include "Core/Os/OsMemory.h"

// Platform headers stay in this TU: they are the heavyweight ones (mach,
// windows.h) and `OsMemory.h` is pulled in by the profiling and editor code
// that only wants a byte count.
#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <psapi.h>
#elif defined(__APPLE__)
    #include <mach/mach.h>
    #include <mach/task_info.h>
#else
    #include <unistd.h>

    #include <cstdio>
    #include <fstream>
    #include <string>
#endif

namespace ya::Os
{

FProcessMemoryStats sampleProcessMemory()
{
    FProcessMemoryStats stats;

#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        stats.bSupported        = true;
        stats.residentBytes     = counters.WorkingSetSize;
        stats.peakResidentBytes = counters.PeakWorkingSetSize;
    }
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t      count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(),
                  MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info),
                  &count) == KERN_SUCCESS) {
        stats.bSupported        = true;
        stats.residentBytes     = info.resident_size;
        stats.peakResidentBytes = info.resident_size_max;
    }
#else
    // Linux: RSS in pages from statm, the high-water mark from status. Neither
    // is a stable ABI, so a missing or unparsable file leaves `bSupported`
    // false instead of reporting zeros.
    std::ifstream statm("/proc/self/statm");
    if (statm) {
        unsigned long totalPages = 0, residentPages = 0;
        statm >> totalPages >> residentPages;
        if (statm && residentPages != 0) {
            stats.bSupported    = true;
            stats.residentBytes = static_cast<uint64_t>(residentPages) * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
        }
    }
    std::ifstream status("/proc/self/status");
    std::string   line;
    while (std::getline(status, line)) {
        unsigned long long peakKB = 0;
        if (std::sscanf(line.c_str(), "VmHWM: %llu kB", &peakKB) == 1) {
            stats.peakResidentBytes = peakKB * 1024ull;
            break;
        }
    }
#endif

    return stats;
}

bool hasUnifiedMemory()
{
#if defined(__APPLE__) && defined(__aarch64__)
    // Apple Silicon: CPU and GPU share the same physical memory, so the process
    // and GPU memory numbers overlap.
    return true;
#else
    return false;
#endif
}

} // namespace ya::Os
