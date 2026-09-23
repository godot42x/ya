#include "Render3D/Common/ViewTargetStore.h"

#include "RHI/Core/RenderResourceFactory.h"

#include <gtest/gtest.h>

#include <array>

namespace ya
{
namespace
{

const SceneViewKey kViewA{.owner = 1, .local = 1};
const SceneViewKey kViewB{.owner = 1, .local = 2};

class StoreTestImage final : public IImage
{
    ImageCreateInfo _desc;

  public:
    explicit StoreTestImage(ImageCreateInfo desc) : _desc(std::move(desc)) {}
    ImageHandle getHandle() const override { return ImageHandle{reinterpret_cast<void*>(1)}; }
    uint32_t getWidth() const override { return _desc.extent.width; }
    uint32_t getHeight() const override { return _desc.extent.height; }
    EFormat::T getFormat() const override { return _desc.format; }
    uint32_t getMipLevels() const override { return _desc.mipLevels; }
    uint32_t getArrayLayers() const override { return _desc.arrayLayers; }
    EImageUsage::T getUsage() const override { return _desc.usage; }
    EImageLayout::T getCompatibilityLayout() const override { return _desc.initialLayout; }
    void setDebugName(const std::string& name) override { _desc.label = name; }
};

class StoreTestImageView final : public IImageView
{
  public:
    StoreTestImageView(IImage* image, const ImageViewCreateInfo& desc)
    {
        _image = image;
        _subresourceRange = {
            .aspectMask = desc.aspectFlags,
            .baseMipLevel = desc.baseMipLevel,
            .levelCount = desc.levelCount,
            .baseArrayLayer = desc.baseArrayLayer,
            .layerCount = desc.layerCount,
        };
    }
    ImageViewHandle getHandle() const override { return ImageViewHandle{reinterpret_cast<void*>(2)}; }
    EFormat::T getFormat() const override { return _image ? _image->getFormat() : EFormat::Undefined; }
    void setDebugName(const std::string&) override {}
};

class StoreTestFactory final : public IRenderResourceFactory
{
  public:
    uint32_t imageCreates = 0;

    std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo&) override { return nullptr; }
    std::shared_ptr<Sampler> createSampler(const SamplerDesc&) override { return nullptr; }
    std::shared_ptr<IImage> createImage(const ImageCreateInfo& desc) override
    {
        ++imageCreates;
        return std::make_shared<StoreTestImage>(desc);
    }
    std::shared_ptr<IImage> importImage(const ImportedImageDesc&) override { return nullptr; }
    std::shared_ptr<IImageView> createImageView(
        std::shared_ptr<IImage> image,
        const ImageViewCreateInfo& desc) override
    {
        return std::make_shared<StoreTestImageView>(image.get(), desc);
    }
};

ViewTargetRequest request(SceneViewKey key, Extent2D extent)
{
    return ViewTargetRequest{
        .viewId = key.viewId(),
        .pipeline = ERenderPipelineKind::Forward,
        .extent = extent,
        .attachments = {
            {EViewAttachment::SceneColor, EFormat::R16G16B16A16_SFLOAT, EImageUsage::ColorAttachment | EImageUsage::Sampled},
            {EViewAttachment::SceneDepth, EFormat::D32_SFLOAT, EImageUsage::DepthStencilAttachment | EImageUsage::Sampled, ESampleCount::Sample_1, true},
        },
    };
}

} // namespace

TEST(ViewTargetStoreTest, ExactRequestReusesAllocationAndResizeReplacesOnce)
{
    StoreTestFactory factory;
    ViewTargetStore store;
    store.init(factory);
    store.registerView(kViewA);

    auto initial = request(kViewA, {.width = 1280, .height = 720});
    ASSERT_TRUE(store.prepare(std::span<const ViewTargetRequest>(&initial, 1)));
    const ViewTargetLease first = store.lease(kViewA.viewId());
    ASSERT_TRUE(first);
    EXPECT_EQ(factory.imageCreates, 2u);

    ASSERT_TRUE(store.prepare(std::span<const ViewTargetRequest>(&initial, 1)));
    const ViewTargetLease reused = store.lease(kViewA.viewId());
    EXPECT_EQ(reused.allocation, first.allocation);
    EXPECT_EQ(reused.allocation->generation, first.allocation->generation);
    EXPECT_EQ(factory.imageCreates, 2u);

    auto resized = request(kViewA, {.width = 640, .height = 480});
    ASSERT_TRUE(store.prepare(std::span<const ViewTargetRequest>(&resized, 1)));
    const ViewTargetLease replacement = store.lease(kViewA.viewId());
    ASSERT_TRUE(replacement);
    EXPECT_NE(replacement.allocation, first.allocation);
    EXPECT_GT(replacement.allocation->generation, first.allocation->generation);
    EXPECT_EQ(factory.imageCreates, 4u);
    EXPECT_EQ(first.find(EViewAttachment::SceneColor)->getExtent().width, 1280u);
    EXPECT_EQ(replacement.find(EViewAttachment::SceneColor)->getExtent().width, 640u);
}

TEST(ViewTargetStoreTest, DifferentViewsOwnIndependentAllocations)
{
    StoreTestFactory factory;
    ViewTargetStore store;
    store.init(factory);
    store.registerView(kViewA);
    store.registerView(kViewB);

    std::array requests{
        request(kViewA, {.width = 1280, .height = 720}),
        request(kViewB, {.width = 256, .height = 256}),
    };
    ASSERT_TRUE(store.prepare(requests));

    const auto world   = store.lease(kViewA.viewId());
    const auto preview = store.lease(kViewB.viewId());
    ASSERT_TRUE(world);
    ASSERT_TRUE(preview);
    EXPECT_NE(world.allocation, preview.allocation);
    EXPECT_NE(world.find(EViewAttachment::SceneColor), preview.find(EViewAttachment::SceneColor));
    EXPECT_EQ(store.residentAllocationCount(), 2u);
}

TEST(ViewTargetStoreTest, RegisteredViewKeepsAllocationAcrossSkippedFrame)
{
    StoreTestFactory factory;
    ViewTargetStore store;
    store.init(factory);
    store.registerView(kViewA);

    auto initial = request(kViewA, {.width = 1280, .height = 720});
    ASSERT_TRUE(store.prepare(std::span<const ViewTargetRequest>(&initial, 1)));
    const ViewTargetLease first = store.lease(kViewA.viewId());
    ASSERT_TRUE(first);

    // A registered View that this frame does not request keeps its allocation:
    // absence from the plan is "hidden", not "destroyed".
    const std::span<const ViewTargetRequest> noRequests;
    ASSERT_TRUE(store.prepare(noRequests));
    const ViewTargetLease kept = store.lease(kViewA.viewId());
    EXPECT_EQ(kept.allocation, first.allocation);
    EXPECT_EQ(store.residentAllocationCount(), 1u);
    EXPECT_EQ(factory.imageCreates, 2u);
}

TEST(ViewTargetStoreTest, PrepareSkipsUnregisteredView)
{
    StoreTestFactory factory;
    ViewTargetStore store;
    store.init(factory);

    auto initial = request(kViewA, {.width = 1280, .height = 720});
    ASSERT_TRUE(store.prepare(std::span<const ViewTargetRequest>(&initial, 1)));
    EXPECT_FALSE(store.lease(kViewA.viewId()));
    EXPECT_EQ(store.residentAllocationCount(), 0u);

    // Unregistering an unknown or invalid key is a no-op, not a crash.
    store.unregisterView(kViewA);
    store.unregisterView(SceneViewKey{});
}

TEST(ViewTargetStoreTest, UnregisterDropsAllocationAndPublication)
{
    StoreTestFactory factory;
    ViewTargetStore store;
    store.init(factory);
    store.registerView(kViewA);

    auto initial = request(kViewA, {.width = 1280, .height = 720});
    ASSERT_TRUE(store.prepare(std::span<const ViewTargetRequest>(&initial, 1)));
    const ViewTargetLease live = store.lease(kViewA.viewId());
    ASSERT_TRUE(live);

    const SceneViewId viewId = kViewA.viewId();
    ASSERT_TRUE(store.beginPublication(0, 1u));
    ASSERT_TRUE(store.beginPublication(1, 1u));
    ASSERT_NE(store.publishView(0, RenderViewOutput{.desc = {.viewId = viewId}}), nullptr);
    ASSERT_NE(store.publishView(1, RenderViewOutput{.desc = {.viewId = viewId}}), nullptr);

    store.unregisterView(kViewA);

    EXPECT_FALSE(store.lease(viewId));
    EXPECT_EQ(store.residentAllocationCount(), 0u);
    EXPECT_EQ(store.findPublication(0, viewId), nullptr);
    EXPECT_EQ(store.findPublication(1, viewId), nullptr);

    // The recorded submission keeps its keepalive copy of the allocation until
    // its fence, even though the store no longer holds a long-term reference.
    ASSERT_TRUE(live.allocation);
    EXPECT_EQ(live.allocation->generation, 1u);
}

} // namespace ya
