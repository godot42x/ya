// Process memory: the platform probe and the `process_memory` bench source.
// The probe asserts only what every platform must answer -- RSS is nonzero and
// the peak is at least the current -- so a port that cannot read a counter
// fails here rather than silently reporting zeros.

#include "Core/Os/OsMemory.h"
#include "Core/Profiling/BenchAggregator.h"
#include "Core/Profiling/PerfBenchSources.h"

#include <gtest/gtest.h>

namespace
{

using namespace ya;
using namespace ya::profiling;

} // namespace

TEST(OsMemoryTest, PlatformReportsAResidentSetAndAPeakAtLeastAsLarge)
{
    const Os::FProcessMemoryStats stats = Os::sampleProcessMemory();
    ASSERT_TRUE(stats.bSupported) << "no process memory counter on this platform";
    EXPECT_GT(stats.residentBytes, 0u);
    EXPECT_GE(stats.peakResidentBytes, stats.residentBytes);
    // A process that is running holds something, and nothing sane holds 1 TiB.
    EXPECT_LT(stats.residentBytes, 1ull << 40);
}

TEST(OsMemoryTest, RepeatedSamplesStayConsistent)
{
    const Os::FProcessMemoryStats first  = Os::sampleProcessMemory();
    const Os::FProcessMemoryStats second = Os::sampleProcessMemory();
    ASSERT_TRUE(first.bSupported);
    ASSERT_TRUE(second.bSupported);
    // The peak is a high-water mark: it never goes back down.
    EXPECT_GE(second.peakResidentBytes, first.peakResidentBytes);
    EXPECT_GT(second.residentBytes, 0u);
}

TEST(ProcessMemoryBenchSourceTest, WindowRecordsRssAndFacts)
{
    BenchAggregator            aggregator;
    ProcessMemoryBenchSource   source;
    BenchSourceRegistration    registration(aggregator, "process_memory", source);

    aggregator.beginWindow(2);
    aggregator.sampleFrame();
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json section = aggregator.buildSections().at("process_memory");

    // Two samples of a live process, so both series are present and nonzero.
    EXPECT_EQ(section.at("rssMiB").at("count"), 2u);
    EXPECT_EQ(section.at("peakRssMiB").at("count"), 2u);
    EXPECT_GT(section.at("rssMiB").at("mean").get<double>(), 0.0);
    EXPECT_GE(section.at("peakRssMiB").at("min").get<double>(), section.at("rssMiB").at("min").get<double>());

    EXPECT_TRUE(section.at("supported").get<bool>());
    // The fact that keeps a reader from adding this section to `gpu_memory`.
    EXPECT_EQ(section.at("unifiedMemory").get<bool>(), Os::hasUnifiedMemory());
}
