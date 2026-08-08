#include "Foundation/RHI/Core/OffscreenJob.h"
#include "Foundation/RHI/Core/CommandBuffer.h"
#include "Product/Host/Utility/OffscreenJobRunner.h"
#include "Product/Host/App.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <latch>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace ya
{

namespace
{

enum class EPlannedAsyncOutcome : uint8_t
{
    Recorded,
    Failed,
    Cancelled,
};

struct PlannedAsyncStep
{
    EPlannedAsyncOutcome outcome = EPlannedAsyncOutcome::Recorded;
    int                  delayMs = 0;
};

class TestCommandBuffer final : public ICommandBuffer
{
  public:
    CommandBufferHandle getHandle() const override { return {}; }
    CommandBufferHandle getTypedHandle() const override { return {}; }
    bool begin(bool = false) override { return true; }
    bool end() override { return true; }
    void reset() override { clearRetainedResources(); }
    void bindPipeline(IGraphicsPipeline*) override {}
    void bindComputePipeline(IComputePipeline*) override {}
    void bindVertexBuffer(uint32_t, const IBuffer*, uint64_t = 0) override {}
    void bindIndexBuffer(IBuffer*, uint64_t = 0, bool = false) override {}
    void draw(uint32_t, uint32_t = 1, uint32_t = 0, uint32_t = 0) override {}
    void drawIndexed(uint32_t, uint32_t = 1, uint32_t = 0, int32_t = 0, uint32_t = 0) override {}
    void setViewport(float, float, float, float, float = 0.0f, float = 1.0f) override {}
    void setScissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void setCullMode(ECullMode::T) override {}
    void setPolygonMode(EPolygonMode::T) override {}
    void setDepthBias(float, float, float) override {}
    void bindDescriptorSets(IPipelineLayout*, uint32_t, const std::vector<DescriptorSetHandle>&, const std::vector<uint32_t>& = {}) override {}
    void bindComputeDescriptorSets(IPipelineLayout*, uint32_t, const std::vector<DescriptorSetHandle>&, const std::vector<uint32_t>& = {}) override {}
    void pushConstants(IPipelineLayout*, EShaderStage::T, uint32_t, uint32_t, const void*) override {}
    void copyBuffer(IBuffer*, IBuffer*, uint64_t, uint64_t = 0, uint64_t = 0) override {}
    void dispatch(uint32_t, uint32_t, uint32_t) override {}
    void dispatchIndirect(IBuffer*, uint64_t = 0) override {}
    void drawIndirect(IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void drawIndexedIndirect(IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void drawIndexedIndirectCount(IBuffer*, uint64_t, IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void fillBuffer(IBuffer*, uint64_t, uint64_t, uint32_t) override {}
    void bufferMemoryBarrier(IBuffer*, EPipelineStage::T, EPipelineStage::T, EResourceAccess::T, EResourceAccess::T, uint64_t = 0, uint64_t = 0) override {}
    void copyBufferToImage(IBuffer*, IImage*, EImageLayout::T, const std::vector<BufferImageCopy>&) override {}
    void copyImageToBuffer(IImage*, EImageLayout::T, IBuffer*, const std::vector<BufferImageCopy>&) override {}
    void copyImage(IImage*, EImageLayout::T, IImage*, EImageLayout::T, const std::vector<ImageCopy>&) override {}
    void beginRendering(const RenderingInfo&) override {}
    void endRendering(const RenderingInfo& = {}) override {}
    void transitionImageLayout(IImage*, EImageLayout::T, EImageLayout::T, const ImageSubresourceRange* = nullptr) override {}
    void transitionImageLayoutAuto(IImage*, EImageLayout::T, const ImageSubresourceRange* = nullptr) override {}
    void debugBeginLabel(const char*, const float* = nullptr) override {}
    void debugEndLabel() override {}
};

std::shared_ptr<RenderImage> makeFakeRenderImage()
{
    return std::make_shared<RenderImage>();
}

std::shared_ptr<OffscreenJobState> makeJob(EOffscreenJobPhase phase = EOffscreenJobPhase::Pending)
{
    auto job   = std::make_shared<OffscreenJobState>();
    job->phase = phase;
    return job;
}

std::vector<PlannedAsyncStep> makePlannedAsyncSteps(uint32_t seed, int jobCount)
{
    std::mt19937 rng(seed);

    std::uniform_int_distribution<int> outcomeDist(0, 99);
    std::uniform_int_distribution<int> delayDist(0, 3);

    std::vector<PlannedAsyncStep> steps;
    steps.reserve(jobCount);
    for (int index = 0; index < jobCount; ++index) {
        const int outcomeValue = outcomeDist(rng);

        PlannedAsyncStep step;
        step.delayMs = delayDist(rng);
        if (outcomeValue < 20) {
            step.outcome = EPlannedAsyncOutcome::Cancelled;
        }
        else if (outcomeValue < 45) {
            step.outcome = EPlannedAsyncOutcome::Failed;
        }
        else {
            step.outcome = EPlannedAsyncOutcome::Recorded;
        }

        steps.push_back(step);
    }

    return steps;
}

void runSeededAsyncPlan(uint32_t seed, int jobCount)
{
    auto steps = makePlannedAsyncSteps(seed, jobCount);

    std::vector<std::shared_ptr<OffscreenJobState>> jobs;
    jobs.reserve(jobCount);
    for (int index = 0; index < jobCount; ++index) {
        jobs.push_back(makeJob(EOffscreenJobPhase::Queued));
    }

    std::latch ready(jobCount);
    std::latch start(1);

    std::vector<std::thread> workers;
    workers.reserve(jobCount);
    for (int index = 0; index < jobCount; ++index) {
        auto job  = jobs[index];
        auto step = steps[index];
        workers.emplace_back([job, step, &ready, &start]() {
            ready.count_down();
            start.wait();

            std::this_thread::sleep_for(std::chrono::milliseconds(step.delayMs));

            if (job->phase == EOffscreenJobPhase::Cancelled || job->bCancelled) {
                return;
            }

            switch (step.outcome) {
                case EPlannedAsyncOutcome::Cancelled: {
                    job->bCancelled = true;
                    job->phase      = EOffscreenJobPhase::Cancelled;
                    break;
                }
                case EPlannedAsyncOutcome::Failed: {
                    job->phase = EOffscreenJobPhase::Failed;
                    break;
                }
                case EPlannedAsyncOutcome::Recorded: {
                    job->phase = EOffscreenJobPhase::Recorded;
                    break;
                }
            }
        });
    }

    ready.wait();
    start.count_down();
    for (auto& worker : workers) {
        worker.join();
    }

    auto submittedJobs = jobs;
    finalizeSubmittedOffscreenJobs(submittedJobs);

    EXPECT_TRUE(submittedJobs.empty());
    for (int index = 0; index < jobCount; ++index) {
        switch (steps[index].outcome) {
            case EPlannedAsyncOutcome::Cancelled: {
                EXPECT_EQ(jobs[index]->phase, EOffscreenJobPhase::Cancelled);
                break;
            }
            case EPlannedAsyncOutcome::Failed: {
                EXPECT_EQ(jobs[index]->phase, EOffscreenJobPhase::Failed);
                break;
            }
            case EPlannedAsyncOutcome::Recorded: {
                EXPECT_EQ(jobs[index]->phase, EOffscreenJobPhase::GpuCompleted);
                break;
            }
        }
    }
}

} // namespace

TEST(OffscreenAsyncTest, PhaseHelpersReflectTerminalStates)
{
    auto pending = makeJob();
    pending->createOutputFn = [](IRender*) -> std::shared_ptr<RenderImage> { return nullptr; };
    pending->executeFn      = [](ICommandBuffer*, RenderImage*) -> bool { return true; };

    EXPECT_TRUE(pending->isReadyToQueue());
    EXPECT_FALSE(pending->isGpuCompleted());
    EXPECT_FALSE(pending->hasFailed());

    pending->phase = EOffscreenJobPhase::Queued;
    EXPECT_FALSE(pending->isReadyToQueue());

    pending->phase = EOffscreenJobPhase::GpuCompleted;
    EXPECT_TRUE(pending->isGpuCompleted());

    pending->phase = EOffscreenJobPhase::Failed;
    EXPECT_TRUE(pending->hasFailed());
}

TEST(OffscreenAsyncTest, FinalizeSubmittedJobsPromotesOnlyRecordedJobs)
{
    auto recorded  = makeJob(EOffscreenJobPhase::Recorded);
    auto queued    = makeJob(EOffscreenJobPhase::Queued);
    auto failed    = makeJob(EOffscreenJobPhase::Failed);
    auto cancelled = makeJob(EOffscreenJobPhase::Cancelled);

    std::vector<std::shared_ptr<OffscreenJobState>> submittedJobs = {
        recorded,
        nullptr,
        queued,
        failed,
        cancelled,
    };

    finalizeSubmittedOffscreenJobs(submittedJobs);

    EXPECT_EQ(recorded->phase, EOffscreenJobPhase::GpuCompleted);
    EXPECT_EQ(queued->phase, EOffscreenJobPhase::Queued);
    EXPECT_EQ(failed->phase, EOffscreenJobPhase::Failed);
    EXPECT_EQ(cancelled->phase, EOffscreenJobPhase::Cancelled);
    EXPECT_TRUE(submittedJobs.empty());
}

TEST(OffscreenAsyncTest, TaskManagerPreservesSubmissionOrderAndPromotesResults)
{
    TaskManager taskManager;
    std::mutex  orderMutex;

    std::vector<int> executionOrder;
    auto             first  = makeJob(EOffscreenJobPhase::Queued);
    auto             second = makeJob(EOffscreenJobPhase::Queued);
    auto             third  = makeJob(EOffscreenJobPhase::Queued);

    taskManager.enqueueOffscreenTask(first, [&executionOrder, &orderMutex, first](ICommandBuffer*) {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back(1);
        first->phase = EOffscreenJobPhase::Recorded;
    });
    taskManager.enqueueOffscreenTask(second, [&executionOrder, &orderMutex, second](ICommandBuffer*) {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back(2);
        second->phase = EOffscreenJobPhase::Recorded;
    });
    taskManager.enqueueOffscreenTask(third, [&executionOrder, &orderMutex, third](ICommandBuffer*) {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back(3);
        third->phase = EOffscreenJobPhase::Recorded;
    });

    std::vector<std::shared_ptr<OffscreenJobState>> submittedJobs;
    taskManager.updateOffscreenTasks(reinterpret_cast<ICommandBuffer*>(0x1), &submittedJobs);

    ASSERT_EQ(executionOrder.size(), 3u);
    EXPECT_EQ(executionOrder[0], 1);
    EXPECT_EQ(executionOrder[1], 2);
    EXPECT_EQ(executionOrder[2], 3);

    ASSERT_EQ(submittedJobs.size(), 3u);
    EXPECT_EQ(submittedJobs[0].get(), first.get());
    EXPECT_EQ(submittedJobs[1].get(), second.get());
    EXPECT_EQ(submittedJobs[2].get(), third.get());

    finalizeSubmittedOffscreenJobs(submittedJobs);

    EXPECT_EQ(first->phase, EOffscreenJobPhase::GpuCompleted);
    EXPECT_EQ(second->phase, EOffscreenJobPhase::GpuCompleted);
    EXPECT_EQ(third->phase, EOffscreenJobPhase::GpuCompleted);
    EXPECT_TRUE(submittedJobs.empty());
}

TEST(OffscreenAsyncTest, QueueOffscreenJobRejectsInvalidInputs)
{
    App app;
    auto job = makeJob();

    queueOffscreenJob(nullptr, reinterpret_cast<IRender*>(0x1), job);
    EXPECT_EQ(job->phase, EOffscreenJobPhase::Failed);

    job = makeJob();
    queueOffscreenJob(&app, nullptr, job);
    EXPECT_EQ(job->phase, EOffscreenJobPhase::Failed);

    job = makeJob();
    queueOffscreenJob(&app, reinterpret_cast<IRender*>(0x1), job);
    EXPECT_EQ(job->phase, EOffscreenJobPhase::Failed);
}

TEST(OffscreenAsyncTest, QueueOffscreenJobRecordsAndPublishesSuccessfulTask)
{
    App               app;
    TestCommandBuffer cmdBuf;
    auto              job = makeJob();

    job->createOutputFn = [](IRender*) -> std::shared_ptr<RenderImage> { return makeFakeRenderImage(); };
    job->executeFn      = [](ICommandBuffer*, RenderImage* output) -> bool { return output != nullptr; };

    queueOffscreenJob(&app, reinterpret_cast<IRender*>(0x1), job);

    ASSERT_EQ(job->phase, EOffscreenJobPhase::Queued);
    ASSERT_EQ(app.getTaskManager().offscreenTasks.size(), 1u);

    std::vector<std::shared_ptr<OffscreenJobState>> submittedJobs;
    app.getTaskManager().updateOffscreenTasks(&cmdBuf, &submittedJobs);

    ASSERT_EQ(job->phase, EOffscreenJobPhase::Recorded);
    ASSERT_NE(job->result, nullptr);
    ASSERT_NE(job->result->outputImage, nullptr);
    ASSERT_EQ(submittedJobs.size(), 1u);

    finalizeSubmittedOffscreenJobs(submittedJobs);
    EXPECT_EQ(job->phase, EOffscreenJobPhase::GpuCompleted);
}

TEST(OffscreenAsyncTest, QueueOffscreenJobPublishesRecordedKeepAliveResources)
{
    App               app;
    TestCommandBuffer cmdBuf;
    auto              job       = makeJob();
    auto              keepAlive = std::make_shared<int>(7);

    job->createOutputFn = [](IRender*) -> std::shared_ptr<RenderImage> { return makeFakeRenderImage(); };
    job->executeFn      = [keepAlive](ICommandBuffer* cmdBuf, RenderImage* output) -> bool {
        if (!cmdBuf || !output) {
            return false;
        }
        cmdBuf->retainResource(keepAlive);
        return true;
    };

    queueOffscreenJob(&app, reinterpret_cast<IRender*>(0x1), job);

    ASSERT_EQ(job->phase, EOffscreenJobPhase::Queued);

    std::vector<std::shared_ptr<OffscreenJobState>> submittedJobs;
    app.getTaskManager().updateOffscreenTasks(&cmdBuf, &submittedJobs);

    ASSERT_EQ(job->phase, EOffscreenJobPhase::Recorded);
    ASSERT_NE(job->result, nullptr);
    ASSERT_EQ(job->result->retainedResources.size(), 1u);
    EXPECT_EQ(job->result->retainedResources[0].get(), keepAlive.get());
}

TEST(OffscreenAsyncTest, QueueOffscreenJobKeepsRecordedResourcesAliveThroughGpuCompletion)
{
    App               app;
    TestCommandBuffer cmdBuf;
    auto              job       = makeJob();
    auto              keepAlive = std::make_shared<int>(11);
    std::weak_ptr<int> keepAliveWeak = keepAlive;

    job->createOutputFn = [](IRender*) -> std::shared_ptr<RenderImage> { return makeFakeRenderImage(); };
    job->executeFn      = [keepAlive](ICommandBuffer* cmdBuf, RenderImage* output) -> bool {
        if (!cmdBuf || !output) {
            return false;
        }
        cmdBuf->retainResource(keepAlive);
        return true;
    };

    queueOffscreenJob(&app, reinterpret_cast<IRender*>(0x1), job);

    std::vector<std::shared_ptr<OffscreenJobState>> submittedJobs;
    app.getTaskManager().updateOffscreenTasks(&cmdBuf, &submittedJobs);

    ASSERT_EQ(job->phase, EOffscreenJobPhase::Recorded);
    ASSERT_NE(job->result, nullptr);
    ASSERT_NE(job->result->outputImage, nullptr);
    ASSERT_EQ(job->result->retainedResources.size(), 1u);
    ASSERT_EQ(job->result->outputImage->retainedResources.size(), 1u);
    EXPECT_FALSE(keepAliveWeak.expired());

    keepAlive.reset();
    EXPECT_FALSE(keepAliveWeak.expired());

    finalizeSubmittedOffscreenJobs(submittedJobs);
    EXPECT_EQ(job->phase, EOffscreenJobPhase::GpuCompleted);
    EXPECT_FALSE(keepAliveWeak.expired());

    job->result->retainedResources.clear();
    EXPECT_FALSE(keepAliveWeak.expired());

    job->executeFn = {};
    cmdBuf.clearRetainedResources();
    job->result->outputImage.reset();
    EXPECT_TRUE(keepAliveWeak.expired());
}

TEST(OffscreenAsyncTest, QueueOffscreenJobMarksExecutionFailure)
{
    App               app;
    TestCommandBuffer cmdBuf;
    auto              job = makeJob();

    job->createOutputFn = [](IRender*) -> std::shared_ptr<RenderImage> { return makeFakeRenderImage(); };
    job->executeFn      = [](ICommandBuffer*, RenderImage*) -> bool { return false; };

    queueOffscreenJob(&app, reinterpret_cast<IRender*>(0x1), job);
    ASSERT_EQ(job->phase, EOffscreenJobPhase::Queued);

    app.getTaskManager().updateOffscreenTasks(&cmdBuf);

    EXPECT_EQ(job->phase, EOffscreenJobPhase::Failed);
    ASSERT_NE(job->result, nullptr);
    EXPECT_EQ(job->result->outputImage, nullptr);
}

TEST(OffscreenAsyncTest, CancelledQueuedJobRemainsCancelledWhenQueuedWorkerRuns)
{
    App               app;
    TestCommandBuffer cmdBuf;
    auto              job = makeJob();

    job->createOutputFn = [](IRender*) -> std::shared_ptr<RenderImage> { return makeFakeRenderImage(); };
    job->executeFn      = [](ICommandBuffer*, RenderImage*) -> bool { return true; };

    queueOffscreenJob(&app, reinterpret_cast<IRender*>(0x1), job);
    ASSERT_EQ(job->phase, EOffscreenJobPhase::Queued);

    auto queuedJob = job;
    cancelOffscreenJob(job);

    EXPECT_EQ(job, nullptr);
    ASSERT_NE(queuedJob, nullptr);
    EXPECT_TRUE(queuedJob->bCancelled);
    EXPECT_EQ(queuedJob->phase, EOffscreenJobPhase::Cancelled);

    app.getTaskManager().updateOffscreenTasks(&cmdBuf);

    EXPECT_EQ(queuedJob->phase, EOffscreenJobPhase::Cancelled);
    ASSERT_NE(queuedJob->result, nullptr);
    EXPECT_EQ(queuedJob->result->outputImage, nullptr);
}

TEST(OffscreenAsyncTest, MultithreadedWorkersSimulateAsyncSuccessFailureAndCancellation)
{
    static constexpr int JOB_COUNT = 24;

    std::vector<std::shared_ptr<OffscreenJobState>> jobs;
    jobs.reserve(JOB_COUNT);
    for (int index = 0; index < JOB_COUNT; ++index) {
        jobs.push_back(makeJob(EOffscreenJobPhase::Queued));
    }

    std::latch ready(JOB_COUNT);
    std::latch start(1);

    std::vector<std::thread> workers;
    workers.reserve(JOB_COUNT);
    for (int index = 0; index < JOB_COUNT; ++index) {
        auto job = jobs[index];
        workers.emplace_back([job, index, &ready, &start]() {
            ready.count_down();
            start.wait();

            if (job->phase == EOffscreenJobPhase::Cancelled || job->bCancelled) {
                return;
            }

            std::this_thread::yield();
            if (index % 5 == 0) {
                job->phase = EOffscreenJobPhase::Failed;
                return;
            }

            job->phase = EOffscreenJobPhase::Recorded;
        });
    }

    ready.wait();
    for (int index = 0; index < JOB_COUNT; ++index) {
        if (index % 4 == 0) {
            jobs[index]->bCancelled = true;
            jobs[index]->phase      = EOffscreenJobPhase::Cancelled;
        }
    }

    start.count_down();
    for (auto& worker : workers) {
        worker.join();
    }

    auto submittedJobs = jobs;
    finalizeSubmittedOffscreenJobs(submittedJobs);

    EXPECT_TRUE(submittedJobs.empty());
    for (int index = 0; index < JOB_COUNT; ++index) {
        if (index % 4 == 0) {
            EXPECT_EQ(jobs[index]->phase, EOffscreenJobPhase::Cancelled);
            continue;
        }
        if (index % 5 == 0) {
            EXPECT_EQ(jobs[index]->phase, EOffscreenJobPhase::Failed);
            continue;
        }
        EXPECT_EQ(jobs[index]->phase, EOffscreenJobPhase::GpuCompleted);
    }
}

TEST(OffscreenAsyncTest, SubprocessWorkerCompletesRecordedJobsInIsolation)
{
    EXPECT_EXIT(
        ([]() {
            auto recorded = makeJob(EOffscreenJobPhase::Recorded);
            auto queued   = makeJob(EOffscreenJobPhase::Queued);

            std::vector<std::shared_ptr<OffscreenJobState>> submittedJobs = {
                recorded,
                queued,
            };

            std::thread worker([&submittedJobs]() { finalizeSubmittedOffscreenJobs(submittedJobs); });
            worker.join();

            if (recorded->phase == EOffscreenJobPhase::GpuCompleted &&
                queued->phase == EOffscreenJobPhase::Queued && submittedJobs.empty()) {
                std::exit(0);
            }

            std::exit(1);
            })(),
        ::testing::ExitedWithCode(0),
        "");
}

TEST(OffscreenAsyncTest, SeededStressPlanProducesStableResults)
{
    const auto stepsA = makePlannedAsyncSteps(1337u, 32);
    const auto stepsB = makePlannedAsyncSteps(1337u, 32);

    ASSERT_EQ(stepsA.size(), stepsB.size());
    for (size_t index = 0; index < stepsA.size(); ++index) {
        EXPECT_EQ(stepsA[index].outcome, stepsB[index].outcome);
        EXPECT_EQ(stepsA[index].delayMs, stepsB[index].delayMs);
    }
}

TEST(OffscreenAsyncTest, SeededStressWorkersHandleMixedOutcomes)
{
    runSeededAsyncPlan(1337u, 32);
}

TEST(OffscreenAsyncTest, SeededStressWorkersHandleDifferentSchedule)
{
    runSeededAsyncPlan(20260418u, 48);
}

} // namespace ya
