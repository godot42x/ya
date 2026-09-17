#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/SceneFamilyResources.h"
#include "Render3D/RenderFrameData.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Core/Common/DeferredDeletionQueue.h"

#include <gtest/gtest.h>

#include <cstdint>
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

SceneSnapshot makeSnapshotWithPalettes(uint32_t count)
{
    SceneSnapshot snapshot;
    snapshot.skinningPalettes.resize(count);
    return snapshot;
}

} // namespace

TEST(SceneFamilyResourcesTest, SameSceneViewsShareFamilyOwner)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory  factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    RenderSubmission* live = pool.acquire(0, 1u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);

    SceneViewportTask viewA{.sceneId = 7, .viewId = 11, .snapshotIndex = 0};
    SceneViewportTask viewB{.sceneId = 7, .viewId = 12, .snapshotIndex = 0};
    EXPECT_EQ(makeSceneViewFamilyKey(viewA), makeSceneViewFamilyKey(viewB));

    SceneFamilyResources* familyA = live->allocateSceneFamily(makeSceneViewFamilyKey(viewA));
    SceneFamilyResources* familyB = live->allocateSceneFamily(makeSceneViewFamilyKey(viewB));
    ASSERT_NE(familyA, nullptr);
    EXPECT_EQ(familyA, familyB);
    EXPECT_EQ(live->sceneFamilyCount(), 1u);
}

TEST(SceneFamilyResourcesTest, DifferentScenesGetDifferentFamilyOwners)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory  factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    RenderSubmission* live = pool.acquire(0, 1u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);

    SceneViewportTask viewA{.sceneId = 1, .viewId = 11, .snapshotIndex = 0};
    SceneViewportTask viewB{.sceneId = 2, .viewId = 21, .snapshotIndex = 1};
    SceneFamilyResources* familyA = live->allocateSceneFamily(makeSceneViewFamilyKey(viewA));
    SceneFamilyResources* familyB = live->allocateSceneFamily(makeSceneViewFamilyKey(viewB));
    ASSERT_NE(familyA, nullptr);
    ASSERT_NE(familyB, nullptr);
    EXPECT_NE(familyA, familyB);
    EXPECT_EQ(live->sceneFamilyCount(), 2u);
}

TEST(SceneFamilyResourcesTest, DualSceneSkinningBuffersStayIndependent)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory  factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    RenderSubmission* live = pool.acquire(0, 1u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);

    SceneSnapshot snapA = makeSnapshotWithPalettes(1);
    SceneSnapshot snapB = makeSnapshotWithPalettes(2);
    SceneFamilyResources* familyA =
        live->allocateSceneFamily(SceneViewFamilyKey{.sceneId = 1}, &snapA);
    SceneFamilyResources* familyB =
        live->allocateSceneFamily(SceneViewFamilyKey{.sceneId = 2}, &snapB);
    ASSERT_NE(familyA, nullptr);
    ASSERT_NE(familyB, nullptr);

    ASSERT_TRUE(prepareSceneFamilySkinning(*live, *familyA, factory, nullptr, {}, "SceneA"));
    ASSERT_TRUE(prepareSceneFamilySkinning(*live, *familyB, factory, nullptr, {}, "SceneB"));

    ASSERT_NE(familyA->gpu().skinningBuffer, nullptr);
    ASSERT_NE(familyB->gpu().skinningBuffer, nullptr);
    EXPECT_NE(familyA->gpu().skinningBuffer.get(), familyB->gpu().skinningBuffer.get());
    EXPECT_EQ(familyA->gpu().skinningPaletteCount, 1u);
    EXPECT_EQ(familyB->gpu().skinningPaletteCount, 2u);

    const IBuffer* bufferA = familyA->gpu().skinningBuffer.get();
    const uint32_t countA  = familyA->gpu().skinningPaletteCount;
    ASSERT_TRUE(prepareSceneFamilySkinning(*live, *familyB, factory, nullptr, {}, "SceneB"));
    EXPECT_EQ(familyA->gpu().skinningBuffer.get(), bufferA);
    EXPECT_EQ(familyA->gpu().skinningPaletteCount, countA);
}

TEST(SceneFamilyResourcesTest, FinishRejectsLaterFamilyAllocation)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory  factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    RenderSubmission* live = pool.acquire(0, 4u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);
    ASSERT_NE(live->allocateSceneFamily(SceneViewFamilyKey{.sceneId = 1}), nullptr);
    ASSERT_TRUE(live->finish());
    EXPECT_EQ(live->allocateSceneFamily(SceneViewFamilyKey{.sceneId = 1}), nullptr);
    EXPECT_EQ(live->sceneFamilyCount(), 1u);
}

} // namespace ya
