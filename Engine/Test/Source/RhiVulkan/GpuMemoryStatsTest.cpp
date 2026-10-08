// GPU memory accounting: which bucket a resource lands in, what the running
// account does as allocations come and go, and what the `gpu_memory` bench
// source writes into a summary. No device here -- the classification and the
// account are pure, and the source takes its numbers from a provider.

#include "Core/Profiling/BenchAggregator.h"
#include "RHI/Core/GpuMemoryBenchSource.h"
#include "RHI/Core/GpuMemoryStats.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace
{

using namespace ya;
using namespace ya::profiling;

constexpr uint64_t kMiB = 1024u * 1024u;

FGpuMemoryStats statsWith(std::array<uint64_t, kGpuMemoryClassCount> classBytes)
{
    FGpuMemoryStats stats;
    stats.bSupported = true;
    for (uint32_t slot = 0; slot < kGpuMemoryClassCount; ++slot) {
        stats.byClass[slot].bytes           = classBytes[slot];
        stats.byClass[slot].allocationCount = classBytes[slot] == 0 ? 0u : 1u;
    }
    stats.heaps.push_back(FGpuMemoryHeapBudget{
        .heapIndex       = 0,
        .budgetBytes     = 512 * kMiB,
        .usageBytes      = 96 * kMiB,
        .allocationBytes = 64 * kMiB,
        .blockBytes      = 80 * kMiB,
    });
    return stats;
}

} // namespace

TEST(GpuMemoryClassTest, ImageUsageDecidesTheBucketWhenTheCreatorDidNot)
{
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Auto, EImageUsage::Sampled | EImageUsage::TransferDst),
              EGpuMemoryClass::Texture);
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Auto, EImageUsage::ColorAttachment | EImageUsage::Sampled),
              EGpuMemoryClass::RenderTarget);
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Auto, EImageUsage::DepthStencilAttachment),
              EGpuMemoryClass::RenderTarget);
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Auto, EImageUsage::TransientAttachment | EImageUsage::ColorAttachment),
              EGpuMemoryClass::Transient);
}

TEST(GpuMemoryClassTest, AnExplicitClassBeatsTheUsageHeuristic)
{
    // The graph knows an attachment is frame-local even where its usage flags
    // look like any other color target.
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Transient, EImageUsage::ColorAttachment),
              EGpuMemoryClass::Transient);
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Texture, EImageUsage::ColorAttachment),
              EGpuMemoryClass::Texture);
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::RenderTarget, EImageUsage::Sampled),
              EGpuMemoryClass::RenderTarget);
}

TEST(GpuMemoryClassTest, HostWritableBuffersAreTheUploadRegion)
{
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Auto, EMemoryUsage::CpuToGpu), EGpuMemoryClass::Upload);
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Auto, EMemoryUsage::GpuOnly), EGpuMemoryClass::Buffer);
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Auto, EMemoryUsage::GpuToCpu), EGpuMemoryClass::Buffer);
    EXPECT_EQ(resolveGpuMemoryClass(EGpuMemoryClass::Buffer, EMemoryUsage::CpuToGpu), EGpuMemoryClass::Buffer);
}

TEST(GpuMemoryAccountTest, TotalsFollowAllocationsAndClampAtZero)
{
    GpuMemoryAccount account;

    account.track(EGpuMemoryClass::Texture, 40 * kMiB);
    account.track(EGpuMemoryClass::Texture, 8 * kMiB);
    account.track(EGpuMemoryClass::RenderTarget, 128 * kMiB);

    std::array<FGpuMemoryClassUsage, kGpuMemoryClassCount> usage{};
    account.fillByClass(usage);

    EXPECT_EQ(usage[gpuMemoryClassIndex(EGpuMemoryClass::Texture)].bytes, 48 * kMiB);
    EXPECT_EQ(usage[gpuMemoryClassIndex(EGpuMemoryClass::Texture)].allocationCount, 2u);
    EXPECT_EQ(usage[gpuMemoryClassIndex(EGpuMemoryClass::RenderTarget)].bytes, 128 * kMiB);
    EXPECT_EQ(usage[gpuMemoryClassIndex(EGpuMemoryClass::Buffer)].bytes, 0u);

    account.release(EGpuMemoryClass::Texture, 48 * kMiB);
    // A second release of the same allocation must not wrap the counter into a
    // near-UINT64_MAX "measurement".
    account.release(EGpuMemoryClass::Texture, 48 * kMiB);
    account.fillByClass(usage);
    EXPECT_EQ(usage[gpuMemoryClassIndex(EGpuMemoryClass::Texture)].bytes, 0u);
    EXPECT_EQ(usage[gpuMemoryClassIndex(EGpuMemoryClass::Texture)].allocationCount, 0u);
}

TEST(GpuMemoryStatsTest, TotalSumsEveryClass)
{
    const FGpuMemoryStats stats = statsWith({4 * kMiB, 16 * kMiB, 32 * kMiB, 8 * kMiB, 2 * kMiB});
    EXPECT_EQ(stats.getTotalBytes(), 62 * kMiB);
    EXPECT_EQ(stats.getTotalAllocationCount(), 5u);
    EXPECT_EQ(stats.getClassBytes(EGpuMemoryClass::Transient), 32 * kMiB);
}

TEST(GpuMemoryBenchSourceTest, WindowRecordsPerClassSeriesAndPeakFacts)
{
    // A two-frame window: the second frame is the peak for every class.
    std::array<uint64_t, kGpuMemoryClassCount> frameOne{4 * kMiB, 16 * kMiB, 32 * kMiB, 8 * kMiB, 2 * kMiB};
    std::array<uint64_t, kGpuMemoryClassCount> frameTwo{6 * kMiB, 20 * kMiB, 24 * kMiB, 8 * kMiB, 1 * kMiB};
    int                                        call = 0;

    BenchAggregator aggregator;
    GpuMemoryBenchSource source([&] {
        ++call;
        return statsWith(call == 1 ? frameOne : frameTwo);
    });
    BenchSourceRegistration registration(aggregator, "gpu_memory", source);

    aggregator.beginWindow(2);
    aggregator.sampleFrame();
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json section = aggregator.buildSections().at("gpu_memory");

    EXPECT_EQ(section.at("totalMiB").at("count"), 2u);
    // 62 MiB then 59 MiB (the transient class shrank while the others grew).
    EXPECT_DOUBLE_EQ(section.at("totalMiB").at("mean").get<double>(), 60.5);
    // Class series: mean of 4 and 6 MiB, peak 6 MiB.
    EXPECT_DOUBLE_EQ(section.at("textureMiB").at("mean").get<double>(), 5.0);
    EXPECT_DOUBLE_EQ(section.at("peak.textureMiB").get<double>(), 6.0);
    EXPECT_DOUBLE_EQ(section.at("peak.transientMiB").get<double>(), 32.0);
    EXPECT_DOUBLE_EQ(section.at("peak.uploadMiB").get<double>(), 2.0);
    EXPECT_TRUE(section.at("supported").get<bool>());

    const nlohmann::json& heaps = section.at("heaps");
    ASSERT_EQ(heaps.size(), 1u);
    EXPECT_DOUBLE_EQ(heaps[0].at("budgetMiB").get<double>(), 512.0);
    EXPECT_DOUBLE_EQ(heaps[0].at("usageMiB").get<double>(), 96.0);
}

TEST(GpuMemoryBenchSourceTest, AnUnsupportedBackendSaysSoInsteadOfReportingZeros)
{
    BenchAggregator      aggregator;
    GpuMemoryBenchSource source([] { return FGpuMemoryStats{}; });
    BenchSourceRegistration registration(aggregator, "gpu_memory", source);

    aggregator.beginWindow(1);
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json section = aggregator.buildSections().at("gpu_memory");
    EXPECT_FALSE(section.at("supported").get<bool>());
}

TEST(GpuMemoryBenchSourceTest, PeaksResetWithTheWindow)
{
    std::array<uint64_t, kGpuMemoryClassCount> big{0, 64 * kMiB, 0, 0, 0};
    std::array<uint64_t, kGpuMemoryClassCount> small{0, 8 * kMiB, 0, 0, 0};
    int                                        frame = 0;

    BenchAggregator aggregator;
    GpuMemoryBenchSource source([&] {
        ++frame;
        return statsWith(frame == 1 ? big : small);
    });
    BenchSourceRegistration registration(aggregator, "gpu_memory", source);

    aggregator.beginWindow(1);
    aggregator.sampleFrame();
    aggregator.endWindow();
    EXPECT_DOUBLE_EQ(aggregator.buildSections().at("gpu_memory").at("peak.renderTargetMiB").get<double>(), 64.0);

    // A second window must not carry the first window's peak forward.
    aggregator.beginWindow(1);
    aggregator.sampleFrame();
    aggregator.endWindow();
    EXPECT_DOUBLE_EQ(aggregator.buildSections().at("gpu_memory").at("peak.renderTargetMiB").get<double>(), 8.0);
}
