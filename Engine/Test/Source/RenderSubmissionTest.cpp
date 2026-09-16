#include "Render3D/Common/RenderSubmission.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/FrameUploadArena.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Core/Common/DeferredDeletionQueue.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace ya
{
namespace
{

class TestBuffer final : public IBuffer
{
    std::string  _name;
    EBufferUsage _usage = EBufferUsage::None;
    uint32_t     _size  = 0;

  public:
    explicit TestBuffer(const BufferCreateInfo& desc)
        : _name(desc.label), _usage(desc.usage), _size(desc.size)
    {}

    bool writeData(const void*, uint32_t = 0, uint32_t = 0) override { return true; }
    bool flush(uint32_t = 0, uint32_t = 0) override { return true; }
    void unmap() override {}
    BufferHandle getHandle() const override
    {
        return BufferHandle{reinterpret_cast<void*>(static_cast<uintptr_t>(_size + 1))};
    }
    uint32_t           getSize() const override { return _size; }
    EBufferUsage       getUsage() const override { return _usage; }
    bool               isHostVisible() const override { return true; }
    const std::string& getName() const override { return _name; }

  protected:
    void mapInternal(void** ptr) override { *ptr = nullptr; }
};

class TestResourceFactory final : public IRenderResourceFactory
{
  public:
    std::vector<std::shared_ptr<IBuffer>> ownedBuffers;

    std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) override
    {
        auto buffer = std::make_shared<TestBuffer>(desc);
        ownedBuffers.push_back(buffer);
        return buffer;
    }

    std::shared_ptr<Sampler>    createSampler(const SamplerDesc&) override { return nullptr; }
    std::shared_ptr<IImage>     createImage(const ImageCreateInfo&) override { return nullptr; }
    std::shared_ptr<IImage>     importImage(const ImportedImageDesc&) override { return nullptr; }
    std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage>, const ImageViewCreateInfo&) override
    {
        return nullptr;
    }
};

ICommandBuffer* dummyCmdBuf(uintptr_t token)
{
    return reinterpret_cast<ICommandBuffer*>(token);
}

} // namespace

TEST(RenderSubmissionTest, KeepalivesSurviveFinish)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    auto               owner = std::make_shared<int>(7);
    std::weak_ptr<int> weak  = owner;

    RenderSubmission* live = pool.acquire(0, 11u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);
    ASSERT_TRUE(live->retain(owner));
    owner.reset();

    ASSERT_TRUE(live->finish());
    EXPECT_TRUE(live->isFinished());
    EXPECT_EQ(live->frameToken(), 11u);
    EXPECT_FALSE(weak.expired());
    EXPECT_EQ(live->keepalives().size(), 1u);
}

TEST(RenderSubmissionTest, NewTokenOnSameFlightDropsPreviousKeepalives)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    auto               first     = std::make_shared<int>(1);
    auto               second    = std::make_shared<int>(2);
    std::weak_ptr<int> firstWeak = first;

    RenderSubmission* firstLive = pool.acquire(0, 1u, dummyCmdBuf(1));
    ASSERT_NE(firstLive, nullptr);
    ASSERT_TRUE(firstLive->retain(first));
    first.reset();
    ASSERT_TRUE(firstLive->finish());
    EXPECT_FALSE(firstWeak.expired());

    RenderSubmission* secondLive = pool.acquire(0, 2u, dummyCmdBuf(1));
    ASSERT_NE(secondLive, nullptr);
    EXPECT_TRUE(firstWeak.expired());
    ASSERT_TRUE(secondLive->retain(second));
    EXPECT_FALSE(secondLive->isFinished());
    EXPECT_EQ(secondLive->frameToken(), 2u);
    EXPECT_EQ(secondLive->keepalives().size(), 1u);
    EXPECT_EQ(secondLive->keepalives().front().as<int>(), second.get());
}

TEST(RenderSubmissionTest, OtherFlightAcquireDoesNotDropKeepalives)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    auto               flight0 = std::make_shared<int>(10);
    auto               flight1 = std::make_shared<int>(11);
    std::weak_ptr<int> weak0   = flight0;

    RenderSubmission* live0 = pool.acquire(0, 5u, dummyCmdBuf(1));
    ASSERT_NE(live0, nullptr);
    ASSERT_TRUE(live0->retain(flight0));
    flight0.reset();
    ASSERT_TRUE(live0->finish());

    RenderSubmission* live1 = pool.acquire(1, 5u, dummyCmdBuf(2));
    ASSERT_NE(live1, nullptr);
    ASSERT_TRUE(live1->retain(flight1));
    ASSERT_TRUE(live1->finish());

    EXPECT_FALSE(weak0.expired());
    ASSERT_NE(pool.get(0), nullptr);
    ASSERT_NE(pool.get(1), nullptr);
    EXPECT_TRUE(pool.get(0)->isFinished());
    EXPECT_EQ(pool.get(0)->keepalives().front().as<int>(), weak0.lock().get());
    EXPECT_EQ(pool.get(1)->keepalives().front().as<int>(), flight1.get());
}

TEST(RenderSubmissionTest, SameTokenAcquireKeepsKeepalives)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    auto owner = std::make_shared<int>(3);

    RenderSubmission* live = pool.acquire(0, 9u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);
    ASSERT_TRUE(live->retain(owner));
    ASSERT_EQ(pool.acquire(0, 9u, dummyCmdBuf(1)), live);
    EXPECT_EQ(live->keepalives().size(), 1u);
    EXPECT_TRUE(live->isRecording());
}

TEST(RenderSubmissionTest, AcquireRequiresCommandBuffer)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    EXPECT_EQ(pool.acquire(0, 1u, nullptr), nullptr);
    EXPECT_EQ(pool.acquire(MAX_FLIGHTS_IN_FLIGHT, 1u, dummyCmdBuf(1)), nullptr);
    EXPECT_EQ(pool.get(0), nullptr);
}

TEST(RenderSubmissionTest, TwoViewUploadSlicesStayIndependent)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    RenderSubmission* live = pool.acquire(0, 1u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);

    const uint32_t payloadA = 1;
    const uint32_t payloadB = 2;
    auto           sliceA   = live->allocateUpload(sizeof(payloadA), 16);
    auto           sliceB   = live->allocateUpload(sizeof(payloadB), 16);
    ASSERT_TRUE(sliceA.has_value());
    ASSERT_TRUE(sliceB.has_value());
    ASSERT_TRUE(sliceA->write(&payloadA, sizeof(payloadA)));
    ASSERT_TRUE(sliceB->write(&payloadB, sizeof(payloadB)));

    EXPECT_EQ(sliceA->buffer.get(), sliceB->buffer.get());
    EXPECT_EQ(sliceA->offset, 0u);
    EXPECT_GT(sliceB->offset, sliceA->offset);

    const uint64_t offsetA = sliceA->offset;
    sliceB->offset         = 4096;
    EXPECT_EQ(sliceA->offset, offsetA);
}

TEST(RenderSubmissionTest, FinishRejectsSecondFinishAndLaterAllocation)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    RenderSubmission* live = pool.acquire(0, 3u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);
    ASSERT_TRUE(live->allocateUpload(4, 16).has_value());
    EXPECT_FALSE(live->allocateDescriptorSet(nullptr, 1));
    ASSERT_TRUE(live->finish());
    EXPECT_FALSE(live->finish());
    EXPECT_FALSE(live->allocateUpload(4, 16).has_value());
    EXPECT_FALSE(live->allocateDescriptorSet(nullptr, 1));
    EXPECT_FALSE(live->retain(std::make_shared<int>(1)));
    EXPECT_EQ(pool.acquire(0, 3u, dummyCmdBuf(1)), nullptr);
}

} // namespace ya
