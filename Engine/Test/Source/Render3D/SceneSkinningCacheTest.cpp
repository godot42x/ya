#include "Render3D/Common/SceneSkinningCache.h"
#include "Render3D/RenderFrameData.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ya
{
namespace
{

class CountingBuffer final : public IBuffer
{
    std::string  _name;
    EBufferUsage _usage = EBufferUsage::None;
    uint32_t     _size  = 0;

  public:
    int                  writes  = 0;
    int                  flushes = 0;
    std::vector<uint8_t> written;

    explicit CountingBuffer(const BufferCreateInfo& desc)
        : _name(desc.label), _usage(desc.usage), _size(desc.size)
    {}

    bool writeData(const void* data, uint32_t size, uint32_t = 0) override
    {
        ++writes;
        const auto* bytes = static_cast<const uint8_t*>(data);
        written.assign(bytes, bytes + size);
        return true;
    }
    bool flush(uint32_t = 0, uint32_t = 0) override
    {
        ++flushes;
        return true;
    }
    void         unmap() override {}
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

class CountingFactory final : public IRenderResourceFactory
{
  public:
    std::vector<std::shared_ptr<CountingBuffer>> buffers;

    std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) override
    {
        auto buffer = std::make_shared<CountingBuffer>(desc);
        buffers.push_back(buffer);
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

struct Fixture
{
    CountingFactory   factory;
    SceneSkinningCache cache;
    Scene              sceneA{"CacheA"};
    Scene              sceneB{"CacheB"};

    Fixture()
    {
        auto& ddq = DeferredDeletionQueue::get();
        ddq.flushAll();
        ddq.init(/*framesInFlight=*/1);
    }

    stdptr<IBuffer> resolve(Scene& scene, std::vector<RenderSkinningPalette>& palettes, uint32_t flight = 0)
    {
        return cache.resolve(&scene, 0, palettes.data(), static_cast<uint32_t>(palettes.size()),
                             flight, factory, "Test");
    }
};

} // namespace

TEST(SceneSkinningCacheTest, StablePointerAcrossFrames)
{
    Fixture f;
    std::vector<RenderSkinningPalette> palettes(2);

    auto first  = f.resolve(f.sceneA, palettes);
    auto second = f.resolve(f.sceneA, palettes);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(f.factory.buffers.size(), 1u);
}

TEST(SceneSkinningCacheTest, UnchangedContentSkipsUpload)
{
    Fixture f;
    std::vector<RenderSkinningPalette> palettes(2);

    auto buffer = f.resolve(f.sceneA, palettes);
    ASSERT_NE(buffer, nullptr);
    auto* counting = static_cast<CountingBuffer*>(buffer.get());
    EXPECT_EQ(counting->writes, 1);

    f.resolve(f.sceneA, palettes);
    EXPECT_EQ(counting->writes, 1);
    EXPECT_EQ(counting->flushes, 1);
}

TEST(SceneSkinningCacheTest, ChangedContentReuploadsInPlace)
{
    Fixture f;
    std::vector<RenderSkinningPalette> palettes(2);

    auto first = f.resolve(f.sceneA, palettes);
    ASSERT_NE(first, nullptr);
    palettes[1].boneMatrices[0][0].x = 2.0f;

    auto second = f.resolve(f.sceneA, palettes);
    EXPECT_EQ(first.get(), second.get());
    auto* counting = static_cast<CountingBuffer*>(second.get());
    EXPECT_EQ(counting->writes, 2);
    ASSERT_EQ(counting->written.size(), 2 * sizeof(RenderSkinningPalette));
    EXPECT_EQ(std::memcmp(counting->written.data() + sizeof(RenderSkinningPalette),
                           &palettes[1], sizeof(RenderSkinningPalette)),
                0);
}

TEST(SceneSkinningCacheTest, EmptyPalettesKeepsMinimumBuffer)
{
    Fixture f;
    std::vector<RenderSkinningPalette> palettes;

    auto buffer = f.cache.resolve(&f.sceneA, 0, nullptr, 0, 0, f.factory, "Test");
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->getSize(), 16u * sizeof(RenderSkinningPalette));
    auto* counting = static_cast<CountingBuffer*>(buffer.get());
    EXPECT_EQ(counting->writes, 0);
}

TEST(SceneSkinningCacheTest, GrowthRetiresOldBuffer)
{
    Fixture f;
    std::vector<RenderSkinningPalette> small(1);
    std::vector<RenderSkinningPalette> large(100);

    auto first = f.resolve(f.sceneA, small);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(DeferredDeletionQueue::get().pendingCount(), 0u);

    auto second = f.resolve(f.sceneA, large);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first.get(), second.get());
    EXPECT_GT(second->getSize(), first->getSize());
    EXPECT_EQ(DeferredDeletionQueue::get().pendingCount(), 1u);
    EXPECT_EQ(DeferredDeletionQueue::get().flushAll(), 1u);
}

TEST(SceneSkinningCacheTest, DifferentScenesStayIndependent)
{
    Fixture f;
    std::vector<RenderSkinningPalette> palettes(2);

    auto bufferA = f.resolve(f.sceneA, palettes);
    auto bufferB = f.resolve(f.sceneB, palettes);
    ASSERT_NE(bufferA, nullptr);
    ASSERT_NE(bufferB, nullptr);
    EXPECT_NE(bufferA.get(), bufferB.get());
    EXPECT_EQ(f.cache.entryCount(), 2u);
}

TEST(SceneSkinningCacheTest, FlightsStayIndependent)
{
    Fixture f;
    std::vector<RenderSkinningPalette> palettes(2);

    auto flight0 = f.resolve(f.sceneA, palettes, 0);
    auto flight1 = f.resolve(f.sceneA, palettes, 1);
    ASSERT_NE(flight0, nullptr);
    ASSERT_NE(flight1, nullptr);
    EXPECT_NE(flight0.get(), flight1.get());
}

TEST(SceneSkinningCacheTest, InvalidFlightIsRejected)
{
    Fixture f;
    std::vector<RenderSkinningPalette> palettes(1);

    EXPECT_EQ(f.resolve(f.sceneA, palettes, MAX_FLIGHTS_IN_FLIGHT), nullptr);
}

TEST(SceneSkinningCacheTest, DropScenesAbsentFromEvicts)
{
    Fixture f;
    std::vector<RenderSkinningPalette> palettes(1);

    auto bufferA = f.resolve(f.sceneA, palettes);
    auto bufferB = f.resolve(f.sceneB, palettes);
    ASSERT_NE(bufferA, nullptr);
    ASSERT_NE(bufferB, nullptr);
    EXPECT_EQ(f.cache.entryCount(), 2u);

    Scene* alive[] = {&f.sceneA};
    f.cache.dropScenesAbsentFrom(alive);
    EXPECT_EQ(f.cache.entryCount(), 1u);
    EXPECT_EQ(DeferredDeletionQueue::get().pendingCount(), 1u);

    auto bufferB2 = f.resolve(f.sceneB, palettes);
    EXPECT_NE(bufferB.get(), bufferB2.get());
    EXPECT_EQ(f.cache.entryCount(), 2u);

    auto bufferA2 = f.resolve(f.sceneA, palettes);
    EXPECT_EQ(bufferA.get(), bufferA2.get());
}

} // namespace ya
