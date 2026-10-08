// Benchmark statistics, the source registry, the PerfState sources and the
// summary JSON shape.

#include "Core/Profiling/BenchAggregator.h"
#include "Core/Profiling/BenchStats.h"
#include "Core/Profiling/BenchSummary.h"
#include "Core/Profiling/PerfBenchSources.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/RenderCounters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

namespace
{

using namespace ya;
using namespace ya::profiling;

std::vector<float> oneToHundred()
{
    std::vector<float> values(100);
    std::iota(values.begin(), values.end(), 1.0f);
    return values;
}

struct CountingSource final : IBenchSource
{
    std::string key;
    float       value         = 0.0f;
    int         windowBegins  = 0;
    int         sampledFrames = 0;
    bool        bWriteFact    = false;

    void onWindowBegin() override { ++windowBegins; }
    void sampleFrame(BenchSeriesWriter& out) override
    {
        ++sampledFrames;
        out.record(key, value);
        value += 1.0f;
    }
    void writeFacts(nlohmann::json& section) const override
    {
        if (bWriteFact) {
            section["peak"] = 42;
        }
    }
};

TEST(BenchStatsTest, PercentilesOfOneToHundredInterpolateBetweenRanks)
{
    const FBenchWindowStats stats = computeBenchWindowStats(oneToHundred());
    EXPECT_EQ(stats.count, 100u);
    EXPECT_DOUBLE_EQ(stats.mean, 50.5);
    EXPECT_DOUBLE_EQ(stats.min, 1.0);
    EXPECT_DOUBLE_EQ(stats.max, 100.0);
    EXPECT_NEAR(stats.p50, 50.5, 1e-9);
    EXPECT_NEAR(stats.p95, 95.05, 1e-9);
    EXPECT_NEAR(stats.p99, 99.01, 1e-9);
}

TEST(BenchStatsTest, OrderOfSamplesDoesNotMatter)
{
    std::vector<float> shuffled = oneToHundred();
    std::mt19937       rng(7);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    const FBenchWindowStats stats = computeBenchWindowStats(shuffled);
    EXPECT_NEAR(stats.p50, 50.5, 1e-9);
    EXPECT_NEAR(stats.p99, 99.01, 1e-9);
    EXPECT_DOUBLE_EQ(stats.max, 100.0);
}

TEST(BenchStatsTest, SingleAndEmptyWindows)
{
    const FBenchWindowStats single = computeBenchWindowStats({4.0f});
    EXPECT_EQ(single.count, 1u);
    EXPECT_DOUBLE_EQ(single.p50, 4.0);
    EXPECT_DOUBLE_EQ(single.p99, 4.0);
    EXPECT_DOUBLE_EQ(single.mean, 4.0);

    const FBenchWindowStats empty = computeBenchWindowStats({});
    EXPECT_EQ(empty.count, 0u);
    EXPECT_DOUBLE_EQ(empty.mean, 0.0);
    EXPECT_DOUBLE_EQ(empty.p95, 0.0);
}

TEST(BenchStatsTest, PercentileEndpointsAreMinAndMax)
{
    const std::vector<float> sorted{1.0f, 2.0f, 10.0f};
    EXPECT_DOUBLE_EQ(percentileOfSorted(sorted, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(percentileOfSorted(sorted, 1.0), 10.0);
    EXPECT_DOUBLE_EQ(percentileOfSorted(sorted, 0.75), 6.0);
}

TEST(BenchAggregatorTest, RegisterAndRemoveSources)
{
    BenchAggregator aggregator;
    CountingSource  a;
    CountingSource  b;
    a.key = "a";
    b.key = "b";

    const auto idA = aggregator.addSource("one", a);
    const auto idB = aggregator.addSource("two", b);
    EXPECT_NE(idA, idB);
    EXPECT_EQ(aggregator.getSourceCount(), 2u);

    EXPECT_TRUE(aggregator.removeSource(idA));
    EXPECT_FALSE(aggregator.removeSource(idA));
    EXPECT_EQ(aggregator.getSourceCount(), 1u);

    aggregator.beginWindow(4);
    aggregator.sampleFrame();
    aggregator.endWindow();
    EXPECT_EQ(a.sampledFrames, 0);
    EXPECT_EQ(b.sampledFrames, 1);
}

TEST(BenchAggregatorTest, RegistrationUnregistersWithItsLifetime)
{
    BenchAggregator aggregator;
    CountingSource  source;
    source.key = "v";
    {
        BenchSourceRegistration registration(aggregator, "section", source);
        EXPECT_TRUE(registration.isRegistered());
        EXPECT_EQ(aggregator.getSourceCount(), 1u);

        BenchSourceRegistration moved = std::move(registration);
        EXPECT_FALSE(registration.isRegistered());
        EXPECT_TRUE(moved.isRegistered());
        EXPECT_EQ(aggregator.getSourceCount(), 1u);
    }
    EXPECT_EQ(aggregator.getSourceCount(), 0u);
}

TEST(BenchAggregatorTest, CallbacksReenterWithoutDeadlock)
{
    struct ReenterSource final : IBenchSource
    {
        BenchAggregator* aggregator = nullptr;
        int              calls      = 0;

        void onWindowBegin() override { EXPECT_TRUE(aggregator->isWindowOpen()); }

        void sampleFrame(BenchSeriesWriter& out) override
        {
            out.record("v", static_cast<float>(calls));
            if (calls++ != 0) {
                return;
            }
            CountingSource extra;
            extra.key          = "e";
            const auto extraId = aggregator->addSource("extra", extra);
            aggregator->sampleFrame();
            (void)aggregator->buildSections();
            EXPECT_TRUE(aggregator->removeSource(extraId));
        }

        void writeFacts(nlohmann::json& section) const override
        {
            section["fact"] = aggregator->getSampledFrameCount();
        }
    };

    BenchAggregator aggregator;
    ReenterSource   source;
    source.aggregator = &aggregator;
    BenchSourceRegistration registration(aggregator, "section", source);

    aggregator.beginWindow(4);
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json sections = aggregator.buildSections();
    EXPECT_EQ(sections["section"]["v"]["count"].get<size_t>(), 2u);
    EXPECT_EQ(sections["section"]["fact"], 2);
    // The nested sampleFrame sees the source registered mid-callback.
    EXPECT_EQ(sections["extra"]["e"]["count"].get<size_t>(), 1u);
}

TEST(BenchAggregatorTest, SamplesOnlyInsideAWindow)
{
    BenchAggregator aggregator;
    CountingSource  source;
    source.key = "v";
    BenchSourceRegistration registration(aggregator, "section", source);

    aggregator.sampleFrame();
    EXPECT_EQ(source.sampledFrames, 0);

    aggregator.beginWindow(3);
    EXPECT_TRUE(aggregator.isWindowOpen());
    EXPECT_EQ(source.windowBegins, 1);
    for (int i = 0; i < 3; ++i) {
        aggregator.sampleFrame();
    }
    aggregator.endWindow();
    aggregator.sampleFrame();

    EXPECT_FALSE(aggregator.isWindowOpen());
    EXPECT_EQ(source.sampledFrames, 3);
    EXPECT_EQ(aggregator.getSampledFrameCount(), 3u);

    const nlohmann::json sections = aggregator.buildSections();
    ASSERT_TRUE(sections.contains("section"));
    const auto& series = sections["section"]["v"];
    EXPECT_EQ(series["count"].get<size_t>(), 3u);
    EXPECT_DOUBLE_EQ(series["min"].get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(series["max"].get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(series["p50"].get<double>(), 1.0);
}

TEST(BenchAggregatorTest, NewWindowDropsPreviousSamplesAndMergesFacts)
{
    BenchAggregator aggregator;
    CountingSource  source;
    source.key        = "v";
    source.bWriteFact = true;
    BenchSourceRegistration registration(aggregator, "section", source);

    aggregator.beginWindow(2);
    aggregator.sampleFrame();
    aggregator.sampleFrame();
    aggregator.endWindow();

    aggregator.beginWindow(1);
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json sections = aggregator.buildSections();
    EXPECT_EQ(sections["section"]["v"]["count"].get<size_t>(), 1u);
    EXPECT_DOUBLE_EQ(sections["section"]["v"]["p50"].get<double>(), 2.0);
    EXPECT_EQ(sections["section"]["peak"].get<int>(), 42);
}

TEST(PerfBenchSourcesTest, StageSourceRecordsOnlyRewrittenCpuSamples)
{
    PerfState            perf;
    PerfStageBenchSource source(perf);
    BenchAggregator      aggregator;
    BenchSourceRegistration registration(aggregator, "cpu_stages", source);

    perf.setValue(FName("Stage/Before"), perf::metric::cpuTimeMs(), 9.0f);
    aggregator.beginWindow(2);

    perf.setValue(FName("Stage/A"), perf::metric::cpuTimeMs(), 1.0f);
    perf.setValue(FName("Stage/B"), perf::metric::cpuTimeMs(), 2.0f);
    perf.setValue(FName("Stage/A"), perf::metric::gpuTimeMs(), 5.0f);
    aggregator.sampleFrame();

    perf.setValue(FName("Stage/A"), perf::metric::cpuTimeMs(), 3.0f);
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json stages = aggregator.buildSections()["cpu_stages"];
    EXPECT_FALSE(stages.contains("Stage/Before"));
    ASSERT_TRUE(stages.contains("Stage/A"));
    EXPECT_EQ(stages["Stage/A"]["count"].get<size_t>(), 2u);
    EXPECT_DOUBLE_EQ(stages["Stage/A"]["max"].get<double>(), 3.0);
    EXPECT_EQ(stages["Stage/B"]["count"].get<size_t>(), 1u);
}

TEST(PerfBenchSourcesTest, FrameSourceSkipsStaleHostTickValues)
{
    PerfState            perf;
    FrameBenchSource     source(perf);
    BenchAggregator      aggregator;
    BenchSourceRegistration registration(aggregator, "frame", source);

    perf.setValue(perf::sample::hostTick(), perf::metric::cpuTimeMs(), 8.0f);
    aggregator.beginWindow(2);

    perf.setValue(perf::sample::hostTick(), perf::metric::cpuTimeMs(), 4.0f);
    perf.setValue(perf::sample::hostTick(), perf::metric::gpuTimeMs(), 2.0f);
    aggregator.sampleFrame();
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json frame = aggregator.buildSections()["frame"];
    EXPECT_EQ(frame["frameMs"]["count"].get<size_t>(), 2u);
    EXPECT_EQ(frame["cpuMs"]["count"].get<size_t>(), 1u);
    EXPECT_DOUBLE_EQ(frame["cpuMs"]["p50"].get<double>(), 4.0);
    EXPECT_EQ(frame["gpuMs"]["count"].get<size_t>(), 1u);
    EXPECT_TRUE(frame.contains("fps"));
}

TEST(PerfBenchSourcesTest, PassGpuTimeSourceRecordsProviderKeys)
{
    PassGpuTimeBenchSource source([](std::unordered_map<std::string, float>& out) {
        out["Shadow"]  = 1.5f;
        out["GBuffer"] = 4.25f;
    });
    BenchAggregator           aggregator;
    BenchSourceRegistration registration(aggregator, "gpu_passes", source);

    aggregator.beginWindow(1);
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json passes = aggregator.buildSections()["gpu_passes"];
    ASSERT_TRUE(passes.contains("Shadow"));
    EXPECT_DOUBLE_EQ(passes["Shadow"]["p50"].get<double>(), 1.5);
    ASSERT_TRUE(passes.contains("GBuffer"));
    EXPECT_DOUBLE_EQ(passes["GBuffer"]["p50"].get<double>(), 4.25);
}

TEST(PerfBenchSourcesTest, PassGpuTimeSourceWithoutProviderRecordsNothing)
{
    PassGpuTimeBenchSource  source(PassGpuTimeBenchSource::Provider{});
    BenchAggregator         aggregator;
    BenchSourceRegistration registration(aggregator, "gpu_passes", source);

    aggregator.beginWindow(1);
    aggregator.sampleFrame();
    aggregator.endWindow();

    // The device is gone, so the frame has no pass times; the section exists
    // but must not claim a zero-valued series for any pass.
    const nlohmann::json sections = aggregator.buildSections();
    ASSERT_TRUE(sections.contains("gpu_passes"));
    EXPECT_TRUE(sections["gpu_passes"].empty());
}

TEST(PerfBenchSourcesTest, RenderCounterSourceReadsCountersAndLeavesTheResetToTheFrame)
{
    resetRenderCounters();
    RenderCounterBenchSource source;
    BenchAggregator          aggregator;
    BenchSourceRegistration registration(aggregator, "render_counters", source);

    incrementRenderCounterDraw();
    incrementRenderCounterDraw();
    incrementRenderCounterIndirectDraw(64);
    incrementRenderCounterBatch2D();

    aggregator.beginWindow(1);
    aggregator.sampleFrame();
    aggregator.endWindow();

    const nlohmann::json counters = aggregator.buildSections()["render_counters"];
    EXPECT_DOUBLE_EQ(counters["drawCount"]["p50"].get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(counters["indirectDrawCount"]["p50"].get<double>(), 64.0);
    EXPECT_DOUBLE_EQ(counters["batch2DCount"]["p50"].get<double>(), 1.0);
    // Untouched counters stay out of the section.
    EXPECT_FALSE(counters.contains("dispatchCount"));

    // Sampling only reads. The reset belongs to the frame boundary
    // (`VulkanRender::beginRecordedFrame`); if it lived here, everything
    // recorded before the first sample would land in one frame's numbers.
    const RenderCounters afterSampling = renderCountersSnapshot();
    EXPECT_EQ(afterSampling.drawCount, 2u);
    EXPECT_EQ(afterSampling.batch2DCount, 1u);

    resetRenderCounters();
    EXPECT_EQ(renderCountersSnapshot().drawCount, 0u);
}

TEST(BenchSummaryTest, SummaryCarriesMetaAndEverySection)
{
    BenchAggregator aggregator;
    CountingSource  source;
    source.key = "frameMs";
    BenchSourceRegistration registration(aggregator, "frame", source);
    aggregator.beginWindow(1);
    aggregator.sampleFrame();
    aggregator.endWindow();

    FBenchSummaryMeta meta;
    meta.name                  = "unit";
    meta.project               = "Example/HelloMaterial/HelloMaterial.yaproject";
    meta.host.buildMode        = "profile";
    meta.renderWidth           = 1920;
    meta.renderHeight          = 1080;
    meta.presentMode           = "immediate";
    meta.availablePresentModes = {"immediate", "fifo"};
    meta.warmupFrames          = 120;
    meta.sampleFrames          = 600;
    meta.sampledFrames         = aggregator.getSampledFrameCount();

    const nlohmann::json summary = buildBenchSummaryJson(meta, aggregator);
    EXPECT_EQ(summary["schemaVersion"].get<int>(), kBenchSummarySchemaVersion);
    EXPECT_EQ(summary["meta"]["name"], "unit");
    EXPECT_EQ(summary["meta"]["buildMode"], "profile");
    EXPECT_EQ(summary["meta"]["renderResolution"]["width"].get<uint32_t>(), 1920u);
    EXPECT_EQ(summary["meta"]["presentMode"], "immediate");
    EXPECT_EQ(summary["meta"]["availablePresentModes"].size(), 2u);
    EXPECT_EQ(summary["meta"]["sampledFrames"].get<uint64_t>(), 1u);
    ASSERT_TRUE(summary.contains("frame"));
    EXPECT_EQ(summary["frame"]["frameMs"]["count"].get<size_t>(), 1u);
    for (const char* key : {"count", "mean", "min", "p50", "p95", "p99", "max"}) {
        EXPECT_TRUE(summary["frame"]["frameMs"].contains(key)) << key;
    }
}

TEST(BenchSummaryTest, HostFactsAreFilled)
{
    const FBenchHostFacts facts = collectBenchHostFacts();
    EXPECT_FALSE(facts.gitCommit.empty());
    EXPECT_FALSE(facts.buildMode.empty());
    EXPECT_FALSE(facts.platform.empty());
    EXPECT_EQ(facts.timestampUtc.size(), 20u);
}

} // namespace
