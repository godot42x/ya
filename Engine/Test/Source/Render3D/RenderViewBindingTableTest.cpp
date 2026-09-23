#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/RenderViewBindingTable.h"
#include "Render3D/Common/Shadow/ShadowFrameResources.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/FrameUploadArena.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Core/Common/DeferredDeletionQueue.h"

#include <gtest/gtest.h>

#include <type_traits>

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
    BufferHandle getHandle() const override { return BufferHandle{reinterpret_cast<void*>(static_cast<uintptr_t>(_size + 1))}; }
    uint32_t getSize() const override { return _size; }
    EBufferUsage getUsage() const override { return _usage; }
    bool isHostVisible() const override { return true; }
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

    std::shared_ptr<Sampler> createSampler(const SamplerDesc&) override { return nullptr; }
    std::shared_ptr<IImage> createImage(const ImageCreateInfo&) override { return nullptr; }
    std::shared_ptr<IImage> importImage(const ImportedImageDesc&) override { return nullptr; }
    std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage>, const ImageViewCreateInfo&) override
    {
        return nullptr;
    }
};

struct TestViewSlot
{
    uint32_t id     = 0;
    uint64_t offset = 0;
    void*    descriptor = nullptr;
};

} // namespace

TEST(RenderViewBindingTableTest, SameSubmissionViewsKeepIndependentSlots)
{
    RenderViewBindingTable<TestViewSlot> table;
    ASSERT_TRUE(table.beginSubmission(0, 1));

    TestViewSlot* viewA = table.mutableNextView(0);
    ASSERT_NE(viewA, nullptr);
    viewA->id          = 11;
    viewA->offset      = 0;
    viewA->descriptor  = reinterpret_cast<void*>(0xA);
    ASSERT_TRUE(table.commitNextView(0));

    TestViewSlot* viewB = table.mutableNextView(0);
    ASSERT_NE(viewB, nullptr);
    EXPECT_NE(viewA, viewB);
    viewB->id         = 22;
    viewB->offset     = 16;
    viewB->descriptor = reinterpret_cast<void*>(0xB);
    ASSERT_TRUE(table.commitNextView(0));

    const TestViewSlot* committedA = table.getView(0, 0);
    const TestViewSlot* committedB = table.getView(0, 1);
    ASSERT_NE(committedA, nullptr);
    ASSERT_NE(committedB, nullptr);
    EXPECT_EQ(committedA->id, 11u);
    EXPECT_EQ(committedB->id, 22u);
    EXPECT_NE(committedA->descriptor, committedB->descriptor);

    viewB->offset     = 99;
    viewB->descriptor = reinterpret_cast<void*>(0xC);
    EXPECT_EQ(table.getView(0, 0)->offset, 0u);
    EXPECT_EQ(table.getView(0, 0)->descriptor, reinterpret_cast<void*>(0xA));
    EXPECT_EQ(table.liveViewCount(0), 2u);
}

TEST(RenderViewBindingTableTest, SameTokenBeginDoesNotDropLiveViews)
{
    RenderViewBindingTable<TestViewSlot> table;
    ASSERT_TRUE(table.beginSubmission(0, 7));
    table.mutableNextView(0)->id = 1;
    ASSERT_TRUE(table.commitNextView(0));
    table.mutableNextView(0)->id = 2;
    ASSERT_TRUE(table.commitNextView(0));

    ASSERT_TRUE(table.beginSubmission(0, 7));
    EXPECT_EQ(table.liveViewCount(0), 2u);
    EXPECT_EQ(table.getView(0, 0)->id, 1u);
    EXPECT_EQ(table.getView(0, 1)->id, 2u);

    table.mutableNextView(0)->id = 3;
    ASSERT_TRUE(table.commitNextView(0));
    EXPECT_EQ(table.liveViewCount(0), 3u);
    EXPECT_EQ(table.getView(0, 0)->id, 1u);
}

TEST(RenderViewBindingTableTest, NewTokenReusesSlotsFromZero)
{
    RenderViewBindingTable<TestViewSlot> table;
    ASSERT_TRUE(table.beginSubmission(0, 1));
    table.mutableNextView(0)->id = 1;
    ASSERT_TRUE(table.commitNextView(0));
    table.mutableNextView(0)->id = 2;
    ASSERT_TRUE(table.commitNextView(0));
    EXPECT_EQ(table.slotCapacity(0), 2u);

    ASSERT_TRUE(table.beginSubmission(0, 2));
    EXPECT_EQ(table.liveViewCount(0), 0u);
    EXPECT_EQ(table.slotCapacity(0), 2u);
    EXPECT_EQ(table.getView(0, 0), nullptr);

    TestViewSlot* reused = table.mutableNextView(0);
    ASSERT_NE(reused, nullptr);
    reused->id = 8;
    ASSERT_TRUE(table.commitNextView(0));
    EXPECT_EQ(table.liveViewCount(0), 1u);
    EXPECT_EQ(table.getView(0, 0)->id, 8u);
}

TEST(RenderViewBindingTableTest, BeginViewRequiresSubmission)
{
    RenderViewBindingTable<TestViewSlot> table;
    EXPECT_EQ(table.mutableNextView(0), nullptr);
    EXPECT_FALSE(table.commitNextView(0));
    EXPECT_FALSE(table.beginSubmission(MAX_FLIGHTS_IN_FLIGHT, 1));
}

TEST(RenderViewBindingTableTest, ForwardViewSlicesStayIndependent)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    FrameUploadArena    arena(factory, /*flightCount=*/1, /*initialCapacity=*/64u * 1024u);
    ASSERT_TRUE(arena.beginFlight(0, 1u));

    ForwardFrameResourceSet::FramePayloads payloadsA{};
    ForwardFrameResourceSet::FramePayloads payloadsB{};

    ForwardFrameResourceSet::Binding viewA{};
    ForwardFrameResourceSet::Binding viewB{};
    ASSERT_TRUE(ForwardFrameResourceSet::writeViewPayloads(arena, 0, 16, payloadsA, viewA));
    ASSERT_TRUE(ForwardFrameResourceSet::writeViewPayloads(arena, 0, 16, payloadsB, viewB));

    EXPECT_TRUE(viewA.pbrFrame.valid());
    EXPECT_TRUE(viewB.pbrFrame.valid());
    EXPECT_EQ(viewA.pbrFrame.buffer.get(), viewB.pbrFrame.buffer.get());
    EXPECT_EQ(viewA.pbrFrame.offset, 0u);
    EXPECT_GT(viewB.pbrFrame.offset, viewA.pbrFrame.offset);
    EXPECT_NE(viewA.pbrLight.offset, viewB.pbrLight.offset);

    const uint64_t viewAOffset = viewA.pbrFrame.offset;
    const uint64_t viewALight  = viewA.pbrLight.offset;
    viewB.pbrFrame.offset = 4096;
    viewB.pbrLight.offset = 8192;
    EXPECT_EQ(viewA.pbrFrame.offset, viewAOffset);
    EXPECT_EQ(viewA.pbrLight.offset, viewALight);
}

TEST(RenderViewBindingTableTest, DeferredViewSlicesStayIndependent)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    FrameUploadArena    arena(factory, /*flightCount=*/1, /*initialCapacity=*/64u * 1024u);
    ASSERT_TRUE(arena.beginFlight(0, 1u));

    DeferredFrameResourceSet::SSAOFrameData ssaoA{};
    DeferredFrameResourceSet::SSAOFrameData ssaoB{};
    DeferredFrameResourceSet::ViewPayloads  payloadsA{};
    DeferredFrameResourceSet::ViewPayloads  payloadsB{};
    payloadsA.ssao = &ssaoA;
    payloadsB.ssao = &ssaoB;

    DeferredFrameResourceSet::Binding viewA{};
    DeferredFrameResourceSet::Binding viewB{};
    ASSERT_TRUE(DeferredFrameResourceSet::writeViewPayloads(arena, 0, 16, payloadsA, viewA));
    ASSERT_TRUE(DeferredFrameResourceSet::writeViewPayloads(arena, 0, 16, payloadsB, viewB));

    EXPECT_TRUE(viewA.frame.valid());
    EXPECT_TRUE(viewB.frame.valid());
    EXPECT_TRUE(viewA.ssaoFrame.valid());
    EXPECT_TRUE(viewB.ssaoFrame.valid());
    EXPECT_EQ(viewA.frame.buffer.get(), viewB.frame.buffer.get());
    EXPECT_EQ(viewA.frame.offset, 0u);
    EXPECT_GT(viewB.frame.offset, viewA.frame.offset);
    EXPECT_NE(viewA.light.offset, viewB.light.offset);
    EXPECT_NE(viewA.ssaoFrame.offset, viewB.ssaoFrame.offset);

    const uint64_t viewAOffset = viewA.frame.offset;
    const uint64_t viewALight  = viewA.light.offset;
    const uint64_t viewASsao   = viewA.ssaoFrame.offset;
    viewB.frame.offset     = 4096;
    viewB.light.offset     = 8192;
    viewB.ssaoFrame.offset = 16384;
    EXPECT_EQ(viewA.frame.offset, viewAOffset);
    EXPECT_EQ(viewA.light.offset, viewALight);
    EXPECT_EQ(viewA.ssaoFrame.offset, viewASsao);
}

TEST(RenderViewBindingTableTest, ShadowViewSlicesStayIndependent)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    FrameUploadArena    arena(factory, /*flightCount=*/1, /*initialCapacity=*/64u * 1024u);
    ASSERT_TRUE(arena.beginFlight(0, 1u));

    ShadowFrameResources::ViewPayloads payloadsA{};
    ShadowFrameResources::ViewPayloads payloadsB{};
    payloadsA.directionalCount = 2;
    payloadsB.directionalCount = 2;
    payloadsA.pointFaceCount   = 6;
    payloadsB.pointFaceCount   = 6;

    ShadowFrameResources::Binding viewA{};
    ShadowFrameResources::Binding viewB{};
    ASSERT_TRUE(ShadowFrameResources::writeViewPayloads(arena, 0, 16, payloadsA, viewA));
    ASSERT_TRUE(ShadowFrameResources::writeViewPayloads(arena, 0, 16, payloadsB, viewB));

    EXPECT_TRUE(viewA.directionalFrames[0].valid());
    EXPECT_TRUE(viewB.directionalFrames[0].valid());
    EXPECT_TRUE(viewA.pointFaces[0].valid());
    EXPECT_TRUE(viewB.pointFaces[0].valid());
    EXPECT_EQ(viewA.directionalFrames[0].buffer.get(), viewB.directionalFrames[0].buffer.get());
    EXPECT_EQ(viewA.directionalFrames[0].offset, 0u);
    EXPECT_GT(viewB.directionalFrames[0].offset, viewA.directionalFrames[0].offset);
    EXPECT_NE(viewA.directionalFrames[1].offset, viewB.directionalFrames[1].offset);
    EXPECT_NE(viewA.pointFaces[0].offset, viewB.pointFaces[0].offset);

    const uint64_t viewACascade0 = viewA.directionalFrames[0].offset;
    const uint64_t viewAFace0    = viewA.pointFaces[0].offset;
    viewB.directionalFrames[0].offset = 4096;
    viewB.pointFaces[0].offset        = 8192;
    EXPECT_EQ(viewA.directionalFrames[0].offset, viewACascade0);
    EXPECT_EQ(viewA.pointFaces[0].offset, viewAFace0);
}

TEST(RenderViewBindingTableTest, RecordingContextsAreIndependentOfFlightIndex)
{
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.submission), RenderSubmission*>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.view), RenderViewRecordingContext>);

    RenderViewRecordingContext viewA{.viewSlot = 0};
    RenderViewRecordingContext viewB{.viewSlot = 1};
    EXPECT_NE(viewA.viewSlot, viewB.viewSlot);
    EXPECT_EQ(viewA.viewSlot, 0u);
}

} // namespace ya
