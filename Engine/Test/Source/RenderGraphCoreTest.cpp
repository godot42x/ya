#include "Render/Core/Graph/RenderGraph.h"
#include "Render/Core/Graph/RenderGraphExecutor.h"
#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Render/Core/Graph/RenderGraphResourceRegistry.h"
#include "Render/Core/RenderingInfoUtils.h"
#include "Render/Core/FrameUploadArena.h"
#include "Resource/DeferredDeletionQueue.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

class TestBuffer final : public IBuffer
{
  private:
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
    bool isHostVisible() const override { return true; }
    const std::string& getName() const override { return _name; }
    EBufferUsage getUsage() const { return _usage; }

  protected:
    void mapInternal(void** ptr) override { *ptr = nullptr; }
};

class TestSampler final : public Sampler
{
  public:
    SamplerHandle getHandle() const override { return SamplerHandle{reinterpret_cast<void*>(0x1234)}; }
};

class TestImage final : public IImage
{
  private:
    ImageCreateInfo  _desc;
    EImageLayout::T  _compatibilityLayout = EImageLayout::Undefined;

  public:
    explicit TestImage(const ImageCreateInfo& desc)
        : _desc(desc), _compatibilityLayout(desc.initialLayout)
    {}

    ImageHandle getHandle() const override { return ImageHandle{reinterpret_cast<void*>(static_cast<uintptr_t>(_desc.extent.width + 1))}; }
    uint32_t getWidth() const override { return _desc.extent.width; }
    uint32_t getHeight() const override { return _desc.extent.height; }
    EFormat::T getFormat() const override { return _desc.format; }
    uint32_t getMipLevels() const override { return _desc.mipLevels; }
    uint32_t getArrayLayers() const override { return _desc.arrayLayers; }
    EImageUsage::T getUsage() const override { return _desc.usage; }
    EImageLayout::T getCompatibilityLayout() const override { return _compatibilityLayout; }
    void setDebugName(const std::string& name) override { _desc.label = name; }
};

class TestImageView final : public IImageView
{
  private:
    ImageViewCreateInfo _desc;

  public:
    TestImageView(std::shared_ptr<IImage> image, const ImageViewCreateInfo& desc)
        : _desc(desc)
    {
        _image = image.get();
        _subresourceRange = ImageSubresourceRange{
            .aspectMask     = desc.aspectFlags,
            .baseMipLevel   = desc.baseMipLevel,
            .levelCount     = desc.levelCount,
            .baseArrayLayer = desc.baseArrayLayer,
            .layerCount     = desc.layerCount,
        };
    }

    ImageViewHandle getHandle() const override { return ImageViewHandle{reinterpret_cast<void*>(static_cast<uintptr_t>(_desc.levelCount + 1))}; }
    EFormat::T getFormat() const override { return _image ? _image->getFormat() : EFormat::Undefined; }
    void setDebugName(const std::string& name) override { _desc.label = name; }
};

class TrackedImage final : public IImage
{
  public:
    explicit TrackedImage(std::vector<std::string>* destructionOrder)
        : _destructionOrder(destructionOrder)
    {}

    ~TrackedImage() override
    {
        if (_destructionOrder) {
            _destructionOrder->push_back("image");
        }
    }

    ImageHandle getHandle() const override { return ImageHandle{reinterpret_cast<void*>(0x101)}; }
    uint32_t getWidth() const override { return 1; }
    uint32_t getHeight() const override { return 1; }
    EFormat::T getFormat() const override { return EFormat::R8G8B8A8_UNORM; }
    uint32_t getMipLevels() const override { return 1; }
    uint32_t getArrayLayers() const override { return 1; }
    EImageUsage::T getUsage() const override { return EImageUsage::Sampled; }
    EImageLayout::T getCompatibilityLayout() const override { return EImageLayout::ShaderReadOnlyOptimal; }
    void setDebugName(const std::string&) override {}

  private:
    std::vector<std::string>* _destructionOrder = nullptr;
};

class TrackedImageView final : public IImageView
{
  public:
    TrackedImageView(IImage* image, std::vector<std::string>* destructionOrder)
        : _destructionOrder(destructionOrder)
    {
        _image = image;
        _subresourceRange = ImageSubresourceRange{
            .aspectMask     = EImageAspect::Color,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
    }

    ~TrackedImageView() override
    {
        if (_destructionOrder) {
            _destructionOrder->push_back("view");
        }
    }

    ImageViewHandle getHandle() const override { return ImageViewHandle{reinterpret_cast<void*>(0x202)}; }
    EFormat::T getFormat() const override { return _image ? _image->getFormat() : EFormat::Undefined; }
    void setDebugName(const std::string&) override {}

  private:
    std::vector<std::string>* _destructionOrder = nullptr;
};

class TestResourceFactory final : public IRenderResourceFactory
{
  public:
    uint32_t createdBuffers  = 0;
    uint32_t createdImages   = 0;
    uint32_t importedImages  = 0;
    uint32_t createdViews    = 0;
    std::vector<BufferCreateInfo> createdBufferDescs;
    std::vector<ImageCreateInfo>  createdImageDescs;
    std::vector<ImportedImageDesc> importedImageDescs;
    std::vector<std::shared_ptr<IBuffer>> ownedBuffers;
    std::vector<std::shared_ptr<IImage>>  ownedImages;
    std::vector<std::shared_ptr<IImage>>  importedImageStorage;
    std::vector<std::shared_ptr<IImageView>> ownedViews;

    std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) override
    {
        ++createdBuffers;
        createdBufferDescs.push_back(desc);
        auto buffer = std::make_shared<TestBuffer>(desc);
        ownedBuffers.push_back(buffer);
        return buffer;
    }

    std::shared_ptr<Sampler> createSampler(const SamplerDesc&) override
    {
        return nullptr;
    }

    std::shared_ptr<IImage> createImage(const ImageCreateInfo& desc) override
    {
        ++createdImages;
        createdImageDescs.push_back(desc);
        auto image = std::make_shared<TestImage>(desc);
        ownedImages.push_back(image);
        return image;
    }

    std::shared_ptr<IImage> importImage(const ImportedImageDesc& desc) override
    {
        ++importedImages;
        importedImageDescs.push_back(desc);
        ImageCreateInfo imageDesc{
            .label       = desc.label,
            .format      = desc.format,
            .extent      = {.width = desc.extent.width, .height = desc.extent.height, .depth = desc.extent.depth},
            .mipLevels   = desc.mipLevels,
            .arrayLayers = desc.arrayLayers,
            .usage       = desc.usage,
            .initialLayout = desc.initialLayout,
        };
        auto image = std::make_shared<TestImage>(imageDesc);
        importedImageStorage.push_back(image);
        return image;
    }

    std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage> image, const ImageViewCreateInfo& desc) override
    {
        ++createdViews;
        auto view = std::make_shared<TestImageView>(std::move(image), desc);
        ownedViews.push_back(view);
        return view;
    }
};

class TestCommandBuffer final : public ICommandBuffer
{
  public:
    struct TransitionRecord
    {
        EImageLayout::T oldLayout = EImageLayout::Undefined;
        EImageLayout::T newLayout = EImageLayout::Undefined;
    };
    struct BufferBarrierRecord
    {
        EPipelineStage::T  srcStage   = EPipelineStage::None;
        EPipelineStage::T  dstStage   = EPipelineStage::None;
        EResourceAccess::T srcAccess  = EResourceAccess::None;
        EResourceAccess::T dstAccess  = EResourceAccess::None;
        uint64_t           offset     = 0;
        uint64_t           size       = 0;
    };
    struct CopyBufferRecord
    {
        uint64_t size      = 0;
        uint64_t srcOffset = 0;
        uint64_t dstOffset = 0;
    };
    struct CopyImageToBufferRecord
    {
        EImageLayout::T              srcLayout = EImageLayout::Undefined;
        std::vector<BufferImageCopy> regions;
    };

    std::vector<TransitionRecord> transitions;
    std::vector<BufferBarrierRecord> bufferBarriers;
    uint32_t beginRenderingCount = 0;
    uint32_t endRenderingCount   = 0;
    std::vector<CopyBufferRecord> copyBuffers;
    std::vector<CopyImageToBufferRecord> copyImageToBuffers;
    bool lastBeginRenderingHadDepth = false;
    EImageLayout::T lastDepthFinalLayout = EImageLayout::Undefined;
    bool lastBeginRenderingHadResolve = false;
    EResolveMode::T lastResolveMode = EResolveMode::None;

  private:
    ResourceStateTracker _imageStateTracker;

  public:
    CommandBufferHandle getHandle() const override { return {}; }
    CommandBufferHandle getTypedHandle() const override { return {}; }
    bool begin(bool = false) override { return true; }
    bool end() override { return true; }
    void reset() override {}
    void bindPipeline(IGraphicsPipeline*) override {}
    void bindComputePipeline(IComputePipeline*) override {}
    void bindVertexBuffer(uint32_t, const IBuffer*, uint64_t = 0) override {}
    void bindIndexBuffer(IBuffer*, uint64_t = 0, bool = false) override {}
    void draw(uint32_t, uint32_t = 1, uint32_t = 0, uint32_t = 0) override {}
    void drawIndexed(uint32_t, uint32_t = 1, uint32_t = 0, int32_t = 0, uint32_t = 0) override {}
    void drawIndirect(IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void drawIndexedIndirect(IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void drawIndexedIndirectCount(IBuffer*, uint64_t, IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void fillBuffer(IBuffer*, uint64_t, uint64_t, uint32_t) override {}
    void bufferMemoryBarrier(IBuffer*, EPipelineStage::T srcStage, EPipelineStage::T dstStage, EResourceAccess::T srcAccess, EResourceAccess::T dstAccess, uint64_t offset = 0, uint64_t size = 0) override
    {
        bufferBarriers.push_back({
            .srcStage  = srcStage,
            .dstStage  = dstStage,
            .srcAccess = srcAccess,
            .dstAccess = dstAccess,
            .offset    = offset,
            .size      = size,
        });
    }
    void setViewport(float, float, float, float, float = 0.0f, float = 1.0f) override {}
    void setScissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void setCullMode(ECullMode::T) override {}
    void setPolygonMode(EPolygonMode::T) override {}
    void setDepthBias(float, float, float) override {}
    void bindDescriptorSets(IPipelineLayout*, uint32_t, const std::vector<DescriptorSetHandle>&, const std::vector<uint32_t>& = {}) override {}
    void bindComputeDescriptorSets(IPipelineLayout*, uint32_t, const std::vector<DescriptorSetHandle>&, const std::vector<uint32_t>& = {}) override {}
    void pushConstants(IPipelineLayout*, EShaderStage::T, uint32_t, uint32_t, const void*) override {}
    void copyBuffer(IBuffer*, IBuffer*, uint64_t size, uint64_t srcOffset = 0, uint64_t dstOffset = 0) override
    {
        copyBuffers.push_back({
            .size      = size,
            .srcOffset = srcOffset,
            .dstOffset = dstOffset,
        });
    }
    void dispatch(uint32_t, uint32_t, uint32_t) override {}
    void dispatchIndirect(IBuffer*, uint64_t = 0) override {}
    void copyBufferToImage(IBuffer*, IImage*, EImageLayout::T, const std::vector<BufferImageCopy>&) override {}
    void copyImageToBuffer(IImage*, EImageLayout::T srcImageLayout, IBuffer*, const std::vector<BufferImageCopy>& regions) override
    {
        copyImageToBuffers.push_back({
            .srcLayout = srcImageLayout,
            .regions = regions,
        });
    }
    void copyImage(IImage*, EImageLayout::T, IImage*, EImageLayout::T, const std::vector<ImageCopy>&) override {}
    void beginRendering(const RenderingInfo& info) override
    {
        ++beginRenderingCount;
        lastBeginRenderingHadDepth = info.attachments.depth.has_value();
        lastDepthFinalLayout = info.attachments.depth ? info.attachments.depth->finalLayout : EImageLayout::Undefined;
        lastBeginRenderingHadResolve = !info.attachments.colors.empty() &&
                                       info.attachments.colors.front().resolveImage != nullptr &&
                                       info.attachments.colors.front().resolveImageView != nullptr;
        lastResolveMode = !info.attachments.colors.empty()
            ? info.attachments.colors.front().resolveMode
            : EResolveMode::None;
    }
    void endRendering(const RenderingInfo& = {}) override { ++endRenderingCount; }
    void transitionImageLayout(IImage*, EImageLayout::T oldLayout, EImageLayout::T newLayout, const ImageSubresourceRange* = nullptr) override
    {
        transitions.push_back({.oldLayout = oldLayout, .newLayout = newLayout});
    }
    void transitionImageLayoutAuto(IImage* image, EImageLayout::T newLayout, const ImageSubresourceRange* range = nullptr) override
    {
        if (!image) {
            return;
        }
        for (const auto& transition : _imageStateTracker.transition(*image, newLayout, range)) {
            transitions.push_back({
                .oldLayout = transition.oldState.layout,
                .newLayout = transition.newState.layout,
            });
        }
    }
    void debugBeginLabel(const char*, const float* = nullptr) override {}
    void debugEndLabel() override {}
};

std::vector<RGTextureStatePlan> collectTextureStatePlans(const RGCompiledGraph& compiled)
{
    std::vector<RGTextureStatePlan> states;
    for (const auto& passPlan : compiled.passPlans) {
        states.insert(states.end(), passPlan.textureStates.begin(), passPlan.textureStates.end());
    }
    return states;
}

std::vector<RGBufferStatePlan> collectBufferStatePlans(const RGCompiledGraph& compiled)
{
    std::vector<RGBufferStatePlan> states;
    for (const auto& passPlan : compiled.passPlans) {
        states.insert(states.end(), passPlan.bufferStates.begin(), passPlan.bufferStates.end());
    }
    return states;
}

} // namespace

TEST(RenderGraphCoreTest, CreateTextureAllocatesGenerationBackedHandle)
{
    RenderGraph graph;
    const auto  handle = graph.createTexture(RGTextureDesc{
         .label  = "gbuffer.albedo",
         .format = EFormat::R8G8B8A8_UNORM,
         .extent = Extent3D{1280, 720, 1},
         .usage  = EImageUsage::ColorAttachment,
    });

    ASSERT_TRUE(handle.isValid());
    const auto* resource = graph.getTexture(handle);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->handle, handle);
    EXPECT_EQ(resource->lifetime, ERGResourceLifetime::Transient);
    EXPECT_EQ(resource->desc.label, "gbuffer.albedo");
}

TEST(RenderGraphCoreTest, ImportTextureStoresImportedDescriptor)
{
    RenderGraph graph;
    const auto  handle = graph.importTexture(RGImportedTextureDesc{
         .desc = RGTextureDesc{
             .label = "swapchain",
         },
         .importDesc = ImportedImageDesc{
             .label         = "swapchain",
             .nativeHandle  = reinterpret_cast<void*>(0x1),
             .format        = EFormat::B8G8R8A8_UNORM,
             .usage         = EImageUsage::ColorAttachment,
             .extent        = Extent3D{1920, 1080, 1},
             .initialLayout = EImageLayout::PresentSrcKHR,
             .finalLayout   = EImageLayout::PresentSrcKHR,
         },
    });

    const auto* resource = graph.getTexture(handle);
    ASSERT_NE(resource, nullptr);
    ASSERT_TRUE(resource->imported.has_value());
    EXPECT_EQ(resource->lifetime, ERGResourceLifetime::Imported);
    EXPECT_EQ(resource->desc.format, EFormat::B8G8R8A8_UNORM);
    EXPECT_EQ(resource->imported->importDesc.initialLayout, EImageLayout::PresentSrcKHR);
    EXPECT_FALSE(resource->imported->image);
    EXPECT_FALSE(resource->imported->viewDesc.has_value());
}

TEST(RenderGraphCoreTest, CreateAndImportBufferTrackSeparateResources)
{
    RenderGraph graph;
    auto importedBacking = std::make_shared<TestBuffer>(BufferCreateInfo{
        .label = "external.readback",
        .size = 512,
        .usage = EBufferUsage::TransferDst,
    });

    const auto transientHandle = graph.createBuffer(RGBufferDesc{
        .label = "frame.uniforms",
        .usage = EBufferUsage::UniformBuffer,
        .size  = 256,
    });
    const auto importedHandle = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "external.readback",
            .usage = EBufferUsage::TransferDst,
            .size  = 512,
        },
        .buffer = importedBacking.get(),
    });

    const auto* transient = graph.getBuffer(transientHandle);
    const auto* imported  = graph.getBuffer(importedHandle);
    ASSERT_NE(transient, nullptr);
    ASSERT_NE(imported, nullptr);
    EXPECT_EQ(transient->lifetime, ERGResourceLifetime::Transient);
    EXPECT_EQ(imported->lifetime, ERGResourceLifetime::Imported);
    ASSERT_TRUE(imported->imported.has_value());
    EXPECT_EQ(imported->imported->buffer, importedBacking.get());
}

TEST(RenderGraphCoreTest, InvalidHandleLookupReturnsNull)
{
    RenderGraph graph;
    EXPECT_EQ(graph.getTexture(RGTextureHandle{}), nullptr);
    EXPECT_EQ(graph.getBuffer(RGBufferHandle{}), nullptr);
}

TEST(RenderGraphCoreTest, CompileBuildsStableDependencyOrder)
{
    RenderGraph graph;
    const auto  texture = graph.createTexture(RGTextureDesc{
         .label  = "hdr",
         .format = EFormat::R16G16B16A16_SFLOAT,
         .extent = Extent3D{1280, 720, 1},
         .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    const auto writer = graph.addPass("writer", [&](RGPassBuilder& pass) {
        pass.useColorAttachment(texture);
    });
    const auto reader = graph.addPass("reader", [&](RGPassBuilder& pass) {
        pass.read(texture);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.order.size(), 2u);
    EXPECT_EQ(compiled.order[0], writer);
    EXPECT_EQ(compiled.order[1], reader);
    ASSERT_EQ(compiled.dependencies.size(), 1u);
    EXPECT_EQ(compiled.dependencies[0].from, writer);
    EXPECT_EQ(compiled.dependencies[0].to, reader);
    ASSERT_EQ(compiled.passPlans.size(), 2u);
    EXPECT_EQ(compiled.passPlans[0].pass, writer);
    EXPECT_EQ(compiled.passPlans[1].pass, reader);
    EXPECT_EQ(compiled.passPlans[0].kind, ERGPassKind::Raster);
    EXPECT_EQ(compiled.passPlans[1].kind, ERGPassKind::Compute);
    const auto textureStates = collectTextureStatePlans(compiled);
    ASSERT_EQ(textureStates.size(), 2u);
    EXPECT_EQ(textureStates[0].requiredState.layout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_EQ(textureStates[1].requiredState.layout, EImageLayout::ShaderReadOnlyOptimal);
}

TEST(RenderGraphCoreTest, CompileTracksTextureReadersBeforeSubsequentWrite)
{
    RenderGraph graph;
    const auto  texture = graph.createTexture(RGTextureDesc{
         .label  = "shared",
         .format = EFormat::R16G16B16A16_SFLOAT,
         .extent = Extent3D{1280, 720, 1},
         .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    const auto firstWriter = graph.addPass("first-writer", [&](RGPassBuilder& pass) {
        pass.useColorAttachment(texture);
    });
    const auto firstReader = graph.addPass("first-reader", [&](RGPassBuilder& pass) {
        pass.read(texture);
    });
    const auto secondReader = graph.addPass("second-reader", [&](RGPassBuilder& pass) {
        pass.read(texture);
    });
    const auto secondWriter = graph.addPass("second-writer", [&](RGPassBuilder& pass) {
        pass.useColorAttachment(texture);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.order.size(), 4u);
    EXPECT_EQ(compiled.order[0], firstWriter);
    EXPECT_EQ(compiled.order[1], firstReader);
    EXPECT_EQ(compiled.order[2], secondReader);
    EXPECT_EQ(compiled.order[3], secondWriter);

    const auto hasDependency = [&compiled](RGPassHandle from, RGPassHandle to)
    {
        return std::find(compiled.dependencies.begin(),
                         compiled.dependencies.end(),
                         RGDependencyEdge{from, to}) != compiled.dependencies.end();
    };
    EXPECT_TRUE(hasDependency(firstWriter, firstReader));
    EXPECT_TRUE(hasDependency(firstWriter, secondReader));
    EXPECT_TRUE(hasDependency(firstWriter, secondWriter));
    EXPECT_TRUE(hasDependency(firstReader, secondWriter));
    EXPECT_TRUE(hasDependency(secondReader, secondWriter));
}

TEST(RenderGraphCoreTest, DescribeCompiledTopologyExportsRealPassOrderAndDependencies)
{
    RenderGraph graph;
    const auto  texture = graph.createTexture(RGTextureDesc{
         .label  = "topology",
         .format = EFormat::R16G16B16A16_SFLOAT,
         .extent = Extent3D{320, 180, 1},
         .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    const auto writer = graph.addPass("writer", [&](RGPassBuilder& pass) {
        pass.useColorAttachment(texture);
    });
    const auto reader = graph.addPass("reader", [&](RGPassBuilder& pass) {
        pass.read(texture);
        pass.declareCompute();
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());

    const auto topology = graph.describeCompiledTopology(compiled);
    ASSERT_EQ(topology.passOrder.size(), 2u);
    EXPECT_EQ(topology.passOrder[0].pass, writer);
    EXPECT_EQ(topology.passOrder[0].name, "writer");
    EXPECT_EQ(topology.passOrder[0].kind, ERGPassKind::Raster);
    EXPECT_EQ(topology.passOrder[0].orderIndex, 0u);
    EXPECT_EQ(topology.passOrder[1].pass, reader);
    EXPECT_EQ(topology.passOrder[1].name, "reader");
    EXPECT_EQ(topology.passOrder[1].kind, ERGPassKind::Compute);
    EXPECT_EQ(topology.passOrder[1].orderIndex, 1u);

    ASSERT_EQ(topology.dependencies.size(), 1u);
    EXPECT_EQ(topology.dependencies[0].from, writer);
    EXPECT_EQ(topology.dependencies[0].to, reader);
    EXPECT_EQ(topology.dependencies[0].fromName, "writer");
    EXPECT_EQ(topology.dependencies[0].toName, "reader");
}

TEST(RenderGraphCoreTest, CompileIncludesExplicitPassDependency)
{
    RenderGraph graph;
    const auto producer = graph.addPass("producer", [](RGPassBuilder&) {});
    const auto consumer = graph.addPass("consumer", [producer](RGPassBuilder& pass) {
        pass.dependsOn(producer);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    EXPECT_NE(std::find(compiled.dependencies.begin(), compiled.dependencies.end(),
                        RGDependencyEdge{producer, consumer}),
              compiled.dependencies.end());
    ASSERT_EQ(compiled.order.size(), 2u);
    EXPECT_EQ(compiled.order[0], producer);
    EXPECT_EQ(compiled.order[1], consumer);
}

TEST(RenderGraphCoreTest, CompileRejectsReadBeforeWriteForTransientTexture)
{
    RenderGraph graph;
    const auto  texture = graph.createTexture(RGTextureDesc{
         .label  = "ao",
         .format = EFormat::R8_UNORM,
         .extent = Extent3D{640, 480, 1},
         .usage  = EImageUsage::Sampled | EImageUsage::ColorAttachment,
    });

    graph.addPass("consumer", [&](RGPassBuilder& pass) {
        pass.read(texture);
    });

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled.isValid());
    ASSERT_EQ(compiled.issues.size(), 1u);
    EXPECT_EQ(compiled.issues[0].kind, RGCompileIssue::EKind::ReadBeforeWrite);
}

TEST(RenderGraphCoreTest, CompileAllowsImportedTextureReadWithoutPriorWriter)
{
    RenderGraph graph;
    const auto  imported = graph.importTexture(RGImportedTextureDesc{
         .desc = RGTextureDesc{
             .label = "swapchain",
         },
         .importDesc = ImportedImageDesc{
             .label        = "swapchain",
             .nativeHandle = reinterpret_cast<void*>(0x1),
             .format       = EFormat::B8G8R8A8_UNORM,
             .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
             .extent       = Extent3D{1280, 720, 1},
         },
    });

    const auto consumer = graph.addPass("consumer", [&](RGPassBuilder& pass) {
        pass.read(imported);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.order.size(), 1u);
    EXPECT_EQ(compiled.order[0], consumer);
}

TEST(RenderGraphCoreTest, PassContextResolvesDeclaredResources)
{
    RenderGraph graph;
    const auto  texture = graph.createTexture(RGTextureDesc{
         .label  = "luminance",
         .format = EFormat::R32_SFLOAT,
         .extent = Extent3D{256, 256, 1},
         .usage  = EImageUsage::Sampled | EImageUsage::ColorAttachment,
    });
    const auto passHandle = graph.addPass("reader", [&](RGPassBuilder& pass) {
        pass.read(texture);
    });

    auto context = graph.createPassContext(passHandle);
    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->getPass().name, "reader");
    EXPECT_EQ(context->getTextureDesc(texture).label, "luminance");
}

TEST(RenderGraphCoreTest, CompileRejectsTextureUsageMismatch)
{
    RenderGraph graph;
    const auto  texture = graph.createTexture(RGTextureDesc{
         .label  = "not-sampled",
         .format = EFormat::R8G8B8A8_UNORM,
         .extent = Extent3D{64, 64, 1},
         .usage  = EImageUsage::ColorAttachment,
    });

    graph.addPass("reader", [&](RGPassBuilder& pass) {
        pass.read(texture);
    });

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled.isValid());
    ASSERT_EQ(compiled.issues.size(), 1u);
    EXPECT_EQ(compiled.issues[0].kind, RGCompileIssue::EKind::InvalidUsage);
}

TEST(RenderGraphCoreTest, DebugDumpIncludesPassOrderDependenciesAndIssues)
{
    RenderGraph graph;
    const auto  texture = graph.createTexture(RGTextureDesc{
         .label  = "ao",
         .format = EFormat::R8_UNORM,
         .extent = Extent3D{64, 64, 1},
         .usage  = EImageUsage::ColorAttachment,
    });

    graph.addPass("consumer", [&](RGPassBuilder& pass) {
        pass.read(texture);
    });

    const auto dump = graph.debugDump(graph.compile());
    EXPECT_NE(dump.find("passes(1)"), std::string::npos);
    EXPECT_NE(dump.find("consumer"), std::string::npos);
    EXPECT_NE(dump.find("passPlans("), std::string::npos);
    EXPECT_NE(dump.find("kind=Compute"), std::string::npos);
    EXPECT_NE(dump.find("    textureStates("), std::string::npos);
    EXPECT_NE(dump.find("issues(1)"), std::string::npos);
    EXPECT_NE(dump.find("InvalidUsage"), std::string::npos);
}

TEST(RenderGraphCoreTest, CompileInfersCopyPassKindForTransferOnlyPass)
{
    RenderGraph graph;
    auto srcBacking = std::make_shared<TestBuffer>(BufferCreateInfo{
        .label = "readback.src",
        .size = 128,
        .usage = EBufferUsage::TransferSrc,
    });
    auto dstBacking = std::make_shared<TestBuffer>(BufferCreateInfo{
        .label = "readback.dst",
        .size = 128,
        .usage = EBufferUsage::TransferDst,
    });
    const auto src = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "readback.src",
            .usage = EBufferUsage::TransferSrc,
            .size  = 128,
        },
        .buffer = srcBacking.get(),
    });
    const auto dst = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "readback.dst",
            .usage = EBufferUsage::TransferDst,
            .size  = 128,
        },
        .buffer = dstBacking.get(),
    });

    graph.addPass("copy", [&](RGPassBuilder& pass) {
        pass.transferSrc(src);
        pass.transferDst(dst);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.passPlans.size(), 1u);
    EXPECT_EQ(compiled.passPlans[0].kind, ERGPassKind::Copy);
}

TEST(RenderGraphCoreTest, CompileRejectsCopyPassWithNonTransferUsage)
{
    RenderGraph graph;
    const auto texture = graph.createTexture(RGTextureDesc{
        .label  = "hdr",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{64, 64, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    graph.addPass("bad-copy", [&](RGPassBuilder& pass) {
        pass.declareCopy();
        pass.read(texture);
    });

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled.isValid());
    EXPECT_NE(std::find_if(compiled.issues.begin(),
                           compiled.issues.end(),
                           [](const RGCompileIssue& issue) {
                               return issue.kind == RGCompileIssue::EKind::InvalidPassKind;
                           }),
              compiled.issues.end());
}

TEST(RenderGraphCoreTest, CompileRejectsPersistentResourceWithoutStableKey)
{
    RenderGraph graph;
    graph.createTexture(RGTextureDesc{
        .label  = "persistent.missing-key",
        .format = EFormat::R8G8B8A8_UNORM,
        .extent = Extent3D{64, 64, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, ERGResourceLifetime::Persistent);

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled.isValid());
    EXPECT_NE(std::find_if(compiled.issues.begin(),
                           compiled.issues.end(),
                           [](const RGCompileIssue& issue) {
                               return issue.kind == RGCompileIssue::EKind::InvalidPersistentIdentity;
                           }),
              compiled.issues.end());
}

TEST(RenderGraphCoreTest, CompileRejectsConflictingPersistentTextureKeys)
{
    RenderGraph graph;
    graph.createPersistentTexture(RGTextureDesc{
        .label  = "history.a",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{128, 128, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "history"});
    graph.createPersistentTexture(RGTextureDesc{
        .label  = "history.b",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{256, 128, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "history"});

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled.isValid());
    EXPECT_NE(std::find_if(compiled.issues.begin(),
                           compiled.issues.end(),
                           [](const RGCompileIssue& issue) {
                               return issue.kind == RGCompileIssue::EKind::InvalidPersistentIdentity;
                           }),
              compiled.issues.end());
}

TEST(RenderGraphCoreTest, CompileStoresDeclaredRasterPlanInCompiledPassPlan)
{
    RenderGraph graph;
    const auto color = graph.createTexture(RGTextureDesc{
        .label  = "lighting",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    const auto depth = graph.createTexture(RGTextureDesc{
        .label  = "depth",
        .format = EFormat::D32_SFLOAT,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::DepthStencilAttachment,
    });

    graph.addPass("declared-raster", [&](RGPassBuilder& pass) {
        pass.declareRaster({
            .renderArea = Rect2D{.pos = {4, 8}, .extent = {160, 90}},
            .layerCount = 3,
            .colors = {{
                .color       = color,
                .clearValue  = ClearValue(0.25f, 0.5f, 0.75f, 1.0f),
                .loadOp      = EAttachmentLoadOp::Clear,
                .storeOp     = EAttachmentStoreOp::Store,
                .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
            }},
            .depth = RGDepthAttachmentDesc{
                .depth       = depth,
                .loadOp      = EAttachmentLoadOp::Load,
                .storeOp     = EAttachmentStoreOp::Store,
                .finalLayout = EImageLayout::DepthStencilAttachmentOptimal,
            },
        });
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.passPlans.size(), 1u);
    ASSERT_TRUE(compiled.passPlans[0].rasterPlan.has_value());
    const auto& rasterPlan = *compiled.passPlans[0].rasterPlan;
    EXPECT_EQ(rasterPlan.renderArea.pos.x, 4.0f);
    EXPECT_EQ(rasterPlan.renderArea.pos.y, 8.0f);
    EXPECT_EQ(rasterPlan.renderArea.extent.x, 160.0f);
    EXPECT_EQ(rasterPlan.renderArea.extent.y, 90.0f);
    EXPECT_EQ(rasterPlan.layerCount, 3u);
    ASSERT_EQ(rasterPlan.colors.size(), 1u);
    EXPECT_EQ(rasterPlan.colors[0].color, color);
    EXPECT_EQ(rasterPlan.colors[0].finalLayout, EImageLayout::ShaderReadOnlyOptimal);
    ASSERT_TRUE(rasterPlan.depth.has_value());
    EXPECT_EQ(rasterPlan.depth->depth, depth);
}

TEST(RenderGraphCoreTest, CompileBuildsTransientBufferLifetimeMetadata)
{
    RenderGraph graph;
    const auto transientA = graph.createBuffer(RGBufferDesc{
        .label = "transient.a",
        .usage = EBufferUsage::TransferDst | EBufferUsage::TransferSrc,
        .size  = 128,
    });
    const auto transientB = graph.createBuffer(RGBufferDesc{
        .label = "transient.b",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    });
    const auto transientUnused = graph.createBuffer(RGBufferDesc{
        .label = "transient.unused",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 64,
    });

    const auto writeA = graph.addPass("write-a", [&](RGPassBuilder& pass) {
        pass.transferDst(transientA);
    });
    const auto readAWriteB = graph.addPass("read-a-write-b", [&](RGPassBuilder& pass) {
        pass.transferSrc(transientA);
        pass.storageWrite(transientB);
    });
    const auto readB = graph.addPass("read-b", [&](RGPassBuilder& pass) {
        pass.storageRead(transientB);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.transientBufferLifetimes.size(), 3u);

    const auto findLifetime = [&](RGBufferHandle handle) -> const RGTransientBufferLifetimePlan* {
        const auto it = std::find_if(compiled.transientBufferLifetimes.begin(),
                                     compiled.transientBufferLifetimes.end(),
                                     [handle](const RGTransientBufferLifetimePlan& lifetime) {
                                         return lifetime.buffer == handle;
                                     });
        return it != compiled.transientBufferLifetimes.end() ? &*it : nullptr;
    };

    const auto* lifetimeA = findLifetime(transientA);
    ASSERT_NE(lifetimeA, nullptr);
    EXPECT_TRUE(lifetimeA->isUsed());
    EXPECT_EQ(lifetimeA->firstPass, writeA);
    EXPECT_EQ(lifetimeA->lastPass, readAWriteB);
    EXPECT_EQ(lifetimeA->firstPassIndex, 0u);
    EXPECT_EQ(lifetimeA->lastPassIndex, 1u);

    const auto* lifetimeB = findLifetime(transientB);
    ASSERT_NE(lifetimeB, nullptr);
    EXPECT_TRUE(lifetimeB->isUsed());
    EXPECT_EQ(lifetimeB->firstPass, readAWriteB);
    EXPECT_EQ(lifetimeB->lastPass, readB);
    EXPECT_EQ(lifetimeB->firstPassIndex, 1u);
    EXPECT_EQ(lifetimeB->lastPassIndex, 2u);

    const auto* lifetimeUnused = findLifetime(transientUnused);
    ASSERT_NE(lifetimeUnused, nullptr);
    EXPECT_FALSE(lifetimeUnused->isUsed());

    EXPECT_EQ(compiled.transientBufferDiagnostics.logicalCount, 3u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.logicalBytes, 448u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.usedCount, 2u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.usedBytes, 384u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.unusedCount, 1u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.unusedBytes, 64u);

    const auto dump = graph.debugDump(compiled);
    EXPECT_NE(dump.find("transientBufferLifetimes(3)"), std::string::npos);
    EXPECT_NE(dump.find("transient.unused unused"), std::string::npos);
    EXPECT_NE(dump.find("transientBufferDiagnostics logicalCount=3 logicalBytes=448 "
                        "usedCount=2 usedBytes=384 unusedCount=1 unusedBytes=64"),
              std::string::npos);
    EXPECT_NE(dump.find("physicalSlotCount=2 physicalBytes=384 aliasedBufferCount=0"), std::string::npos);
    EXPECT_NE(dump.find("physicalReuse=compiler-plan"), std::string::npos);
}

TEST(RenderGraphCoreTest, CompileOrdersTransientBufferLifetimesByTopologicalUse)
{
    RenderGraph graph;
    const auto lateCreatedButEarlyUsed = graph.createBuffer(RGBufferDesc{
        .label = "transient.early",
        .usage = EBufferUsage::TransferDst,
        .size  = 32,
    });
    const auto earlyCreatedButLateUsed = graph.createBuffer(RGBufferDesc{
        .label = "transient.late",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 64,
    });
    const auto middleUsed = graph.createBuffer(RGBufferDesc{
        .label = "transient.middle",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 48,
    });

    graph.addPass("write-early", [&](RGPassBuilder& pass) {
        pass.transferDst(lateCreatedButEarlyUsed);
    });
    graph.addPass("write-middle", [&](RGPassBuilder& pass) {
        pass.storageWrite(middleUsed);
    });
    graph.addPass("write-late", [&](RGPassBuilder& pass) {
        pass.storageWrite(earlyCreatedButLateUsed);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.transientBufferLifetimes.size(), 3u);

    EXPECT_EQ(compiled.transientBufferLifetimes[0].buffer, lateCreatedButEarlyUsed);
    EXPECT_EQ(compiled.transientBufferLifetimes[0].firstPassIndex, 0u);
    EXPECT_EQ(compiled.transientBufferLifetimes[1].buffer, middleUsed);
    EXPECT_EQ(compiled.transientBufferLifetimes[1].firstPassIndex, 1u);
    EXPECT_EQ(compiled.transientBufferLifetimes[2].buffer, earlyCreatedButLateUsed);
    EXPECT_EQ(compiled.transientBufferLifetimes[2].firstPassIndex, 2u);
}

TEST(RenderGraphCoreTest, CompileTransientBufferLifetimesFollowExplicitDependenciesAndIgnoreNonTransientBuffers)
{
    RenderGraph graph;
    auto importedBacking = std::make_shared<TestBuffer>(BufferCreateInfo{
        .label = "imported.buffer",
        .size = 64,
        .usage = EBufferUsage::StorageBuffer,
    });
    const auto imported = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "imported.buffer",
            .usage = EBufferUsage::StorageBuffer,
            .size  = 64,
        },
        .buffer       = importedBacking.get(),
        .initialState = BufferResourceState{
            .stages = EPipelineStage::ComputeShader,
            .access = EResourceAccess::ShaderRead,
        },
    });
    const auto branchLeft = graph.createBuffer(RGBufferDesc{
        .label = "transient.branch.left",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 96,
    });
    const auto branchRight = graph.createBuffer(RGBufferDesc{
        .label = "transient.branch.right",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 112,
    });
    const auto optionalUnused = graph.createBuffer(RGBufferDesc{
        .label = "transient.optional.unused",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 24,
    });

    const auto seed = graph.addPass("seed", [&](RGPassBuilder& pass) {
        pass.storageRead(imported);
    });
    const auto rightPass = graph.addPass("branch-right", [&](RGPassBuilder& pass) {
        pass.storageWrite(branchRight);
    });
    const auto leftPass = graph.addPass("branch-left", [&](RGPassBuilder& pass) {
        pass.storageWrite(branchLeft);
        pass.dependsOn(rightPass);
        pass.dependsOn(seed);
    });
    const auto merge = graph.addPass("merge", [&](RGPassBuilder& pass) {
        pass.storageRead(branchLeft);
        pass.storageRead(branchRight);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.transientBufferLifetimes.size(), 3u);

    const auto findLifetime = [&](RGBufferHandle handle) -> const RGTransientBufferLifetimePlan* {
        const auto it = std::find_if(compiled.transientBufferLifetimes.begin(),
                                     compiled.transientBufferLifetimes.end(),
                                     [handle](const RGTransientBufferLifetimePlan& lifetime) {
                                         return lifetime.buffer == handle;
                                     });
        return it != compiled.transientBufferLifetimes.end() ? &*it : nullptr;
    };

    const auto* leftLifetime = findLifetime(branchLeft);
    ASSERT_NE(leftLifetime, nullptr);
    EXPECT_EQ(leftLifetime->firstPass, leftPass);
    EXPECT_EQ(leftLifetime->lastPass, merge);
    EXPECT_EQ(leftLifetime->firstPassIndex, 2u);
    EXPECT_EQ(leftLifetime->lastPassIndex, 3u);

    const auto* rightLifetime = findLifetime(branchRight);
    ASSERT_NE(rightLifetime, nullptr);
    EXPECT_EQ(rightLifetime->firstPass, rightPass);
    EXPECT_EQ(rightLifetime->lastPass, merge);
    EXPECT_EQ(rightLifetime->firstPassIndex, 1u);
    EXPECT_EQ(rightLifetime->lastPassIndex, 3u);

    const auto* unusedLifetime = findLifetime(optionalUnused);
    ASSERT_NE(unusedLifetime, nullptr);
    EXPECT_FALSE(unusedLifetime->isUsed());

    EXPECT_EQ(compiled.transientBufferLifetimes[0].buffer, rightLifetime->buffer);
    EXPECT_EQ(compiled.transientBufferLifetimes[1].buffer, leftLifetime->buffer);
    EXPECT_EQ(compiled.transientBufferLifetimes[2].buffer, unusedLifetime->buffer);
}

TEST(RenderGraphCoreTest, CompileAllocatesDeterministicTransientBufferSlots)
{
    RenderGraph graph;
    const auto first = graph.createBuffer(RGBufferDesc{
        .label     = "transient.first",
        .usage     = EBufferUsage::TransferDst,
        .size      = 32,
        .alignment = 16,
    });
    const auto second = graph.createBuffer(RGBufferDesc{
        .label       = "transient.second",
        .usage       = EBufferUsage::TransferDst,
        .size        = 128,
        .memoryUsage = EMemoryUsage::Auto,
        .alignment   = 64,
    });
    const auto third = graph.createBuffer(RGBufferDesc{
        .label       = "transient.third",
        .usage       = EBufferUsage::StorageBuffer,
        .size        = 64,
        .memoryUsage = EMemoryUsage::Auto,
        .alignment   = 32,
    });

    graph.addPass("write-first", [&](RGPassBuilder& pass) {
        pass.transferDst(first);
    });
    graph.addPass("write-second", [&](RGPassBuilder& pass) {
        pass.transferDst(second);
    });
    graph.addPass("write-third", [&](RGPassBuilder& pass) {
        pass.storageWrite(third);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.transientBufferSlots.size(), 1u);
    ASSERT_EQ(compiled.transientBufferAssignments.size(), 3u);

    const auto& slot = compiled.transientBufferSlots.front();
    EXPECT_EQ(slot.slotIndex, 0u);
    EXPECT_EQ(slot.desc.size, 128u);
    EXPECT_EQ(slot.desc.alignment, 64u);
    EXPECT_EQ(slot.desc.usage, EBufferUsage::TransferDst | EBufferUsage::StorageBuffer);
    ASSERT_EQ(slot.buffers.size(), 3u);
    EXPECT_EQ(slot.buffers[0], first);
    EXPECT_EQ(slot.buffers[1], second);
    EXPECT_EQ(slot.buffers[2], third);

    EXPECT_EQ(compiled.transientBufferAssignments[0].buffer, first);
    EXPECT_EQ(compiled.transientBufferAssignments[0].slotIndex, 0u);
    EXPECT_EQ(compiled.transientBufferAssignments[1].buffer, second);
    EXPECT_EQ(compiled.transientBufferAssignments[1].slotIndex, 0u);
    EXPECT_EQ(compiled.transientBufferAssignments[2].buffer, third);
    EXPECT_EQ(compiled.transientBufferAssignments[2].slotIndex, 0u);

    EXPECT_EQ(compiled.transientBufferDiagnostics.physicalSlotCount, 1u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.physicalBytes, 128u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.aliasedBufferCount, 2u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.aliasBoundaryCount, 2u);
    EXPECT_NEAR(compiled.transientBufferDiagnostics.reuseRatio, 2.0 / 3.0, 0.0001);
    ASSERT_EQ(compiled.transientBufferAliasBoundaries.size(), 2u);
    EXPECT_EQ(compiled.transientBufferAliasBoundaries[0].previousBuffer, first);
    EXPECT_EQ(compiled.transientBufferAliasBoundaries[0].nextBuffer, second);
    EXPECT_EQ(compiled.transientBufferAliasBoundaries[1].previousBuffer, second);
    EXPECT_EQ(compiled.transientBufferAliasBoundaries[1].nextBuffer, third);
}

TEST(RenderGraphCoreTest, CompileDoesNotAliasOverlappingOrIncompatibleTransientBuffers)
{
    RenderGraph graph;
    const auto overlapA = graph.createBuffer(RGBufferDesc{
        .label       = "transient.overlap.a",
        .usage       = EBufferUsage::StorageBuffer,
        .size        = 64,
        .memoryUsage = EMemoryUsage::GpuOnly,
    });
    const auto overlapB = graph.createBuffer(RGBufferDesc{
        .label       = "transient.overlap.b",
        .usage       = EBufferUsage::StorageBuffer,
        .size        = 96,
        .memoryUsage = EMemoryUsage::GpuOnly,
    });
    const auto incompatible = graph.createBuffer(RGBufferDesc{
        .label       = "transient.incompatible",
        .usage       = EBufferUsage::StorageBuffer,
        .size        = 48,
        .memoryUsage = EMemoryUsage::CpuToGpu,
    });
    const auto persistent = graph.createPersistentBuffer(
        RGBufferDesc{
            .label       = "persistent.buffer",
            .usage       = EBufferUsage::StorageBuffer,
            .size        = 64,
            .memoryUsage = EMemoryUsage::GpuOnly,
        },
        RGPersistentBufferKey{"persistent.buffer"});
    auto importedBacking = std::make_shared<TestBuffer>(BufferCreateInfo{
        .label = "imported.buffer",
        .size = 64,
        .usage = EBufferUsage::StorageBuffer,
        .memoryUsage = EMemoryUsage::GpuOnly,
    });
    const auto imported = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label       = "imported.buffer",
            .usage       = EBufferUsage::StorageBuffer,
            .size        = 64,
            .memoryUsage = EMemoryUsage::GpuOnly,
        },
        .buffer       = importedBacking.get(),
        .initialState = BufferResourceState{
            .stages = EPipelineStage::ComputeShader,
            .access = EResourceAccess::ShaderRead,
        },
    });

    graph.addPass("write-a", [&](RGPassBuilder& pass) {
        pass.storageWrite(overlapA);
    });
    graph.addPass("write-b", [&](RGPassBuilder& pass) {
        pass.storageRead(overlapA);
        pass.storageWrite(overlapB);
    });
    graph.addPass("read-b", [&](RGPassBuilder& pass) {
        pass.storageRead(overlapB);
    });
    graph.addPass("write-incompatible", [&](RGPassBuilder& pass) {
        pass.storageWrite(incompatible);
        pass.storageWrite(persistent);
        pass.storageRead(imported);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.transientBufferAssignments.size(), 3u);
    ASSERT_EQ(compiled.transientBufferSlots.size(), 3u);

    const auto findAssignment = [&](RGBufferHandle handle) {
        return std::find_if(compiled.transientBufferAssignments.begin(),
                            compiled.transientBufferAssignments.end(),
                            [handle](const RGTransientBufferAssignment& assignment) {
                                return assignment.buffer == handle;
                            });
    };
    const auto aAssignment = findAssignment(overlapA);
    const auto bAssignment = findAssignment(overlapB);
    const auto incompatibleAssignment = findAssignment(incompatible);
    ASSERT_NE(aAssignment, compiled.transientBufferAssignments.end());
    ASSERT_NE(bAssignment, compiled.transientBufferAssignments.end());
    ASSERT_NE(incompatibleAssignment, compiled.transientBufferAssignments.end());
    EXPECT_NE(aAssignment->slotIndex, bAssignment->slotIndex);
    EXPECT_NE(aAssignment->slotIndex, incompatibleAssignment->slotIndex);
    EXPECT_NE(bAssignment->slotIndex, incompatibleAssignment->slotIndex);

    EXPECT_EQ(compiled.transientBufferDiagnostics.physicalSlotCount, 3u);
    EXPECT_EQ(compiled.transientBufferDiagnostics.aliasedBufferCount, 0u);
}

TEST(RenderGraphCoreTest, CompileBuildsImportedFinalizePlans)
{
    RenderGraph graph;

    auto sharedImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label         = "swapchain",
        .format        = EFormat::B8G8R8A8_UNORM,
        .extent        = {.width = 1280, .height = 720, .depth = 1},
        .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
        .initialLayout = EImageLayout::Undefined,
    });
    auto sharedView = std::make_shared<TestImageView>(sharedImage, ImageViewCreateInfo{
        .label       = "swapchain.view",
        .aspectFlags = EImageAspect::Color,
        .levelCount  = 1,
        .layerCount  = 1,
    });
    TestBuffer readbackBuffer(BufferCreateInfo{
        .label = "readback.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 512,
    });

    const auto importedTexture = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "swapchain",
            .format = EFormat::B8G8R8A8_UNORM,
            .extent = Extent3D{1280, 720, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
        },
        .importDesc = ImportedImageDesc{
            .label         = "swapchain",
            .nativeHandle  = static_cast<void*>(sharedImage->getHandle()),
            .format        = EFormat::B8G8R8A8_UNORM,
            .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
            .extent        = Extent3D{1280, 720, 1},
            .initialLayout = EImageLayout::Undefined,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        },
        .image     = sharedImage,
        .imageView = sharedView,
    });
    const auto importedBuffer = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "readback.dst",
            .usage = EBufferUsage::TransferDst,
            .size  = 512,
        },
        .buffer = &readbackBuffer,
        .initialState = BufferResourceState{
            .stages = EPipelineStage::Transfer,
            .access = EResourceAccess::TransferWrite,
            .size   = 512,
        },
        .finalState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostRead,
            .size   = 512,
        },
    });

    graph.addPass(
        "copy-to-imported",
        [&](RGPassBuilder& pass) {
            pass.transferDst(importedTexture);
            pass.transferDst(importedBuffer);
        },
        [](RGRenderContext&) {});

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.importedTextureFinalizes.size(), 1u);
    EXPECT_EQ(compiled.importedTextureFinalizes[0].texture, importedTexture);
    EXPECT_EQ(compiled.importedTextureFinalizes[0].finalLayout, EImageLayout::PresentSrcKHR);
    ASSERT_EQ(compiled.importedBufferFinalizes.size(), 1u);
    EXPECT_EQ(compiled.importedBufferFinalizes[0].buffer, importedBuffer);
    EXPECT_EQ(compiled.importedBufferFinalizes[0].finalState.stages, EPipelineStage::Host);
    EXPECT_EQ(compiled.importedBufferFinalizes[0].finalState.access, EResourceAccess::HostRead);
}

TEST(RenderGraphCoreTest, CompileBuildsBufferAndDepthStatePlans)
{
    RenderGraph graph;
    const auto depth = graph.createTexture(RGTextureDesc{
        .label  = "depth",
        .format = EFormat::D32_SFLOAT,
        .extent = Extent3D{320, 240, 1},
        .usage  = EImageUsage::DepthStencilAttachment,
    });
    const auto storage = graph.createBuffer(RGBufferDesc{
        .label = "culling.output",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    });

    graph.addPass("depth-prepass", [&](RGPassBuilder& pass) {
        pass.useDepthAttachment(depth);
        pass.storageWrite(storage);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    const auto textureStates = collectTextureStatePlans(compiled);
    ASSERT_EQ(textureStates.size(), 1u);
    EXPECT_EQ(textureStates[0].requiredState.layout, EImageLayout::DepthStencilAttachmentOptimal);
    EXPECT_EQ(textureStates[0].requiredState.access,
              static_cast<EResourceAccess::T>(EResourceAccess::DepthStencilAttachmentRead | EResourceAccess::DepthStencilAttachmentWrite));
    const auto bufferStates = collectBufferStatePlans(compiled);
    ASSERT_EQ(bufferStates.size(), 1u);
    EXPECT_EQ(bufferStates[0].requiredState.access, EResourceAccess::ShaderWrite);
    EXPECT_EQ(bufferStates[0].requiredState.size, 256u);
}

TEST(RenderGraphCoreTest, CompileTracksExplicitUniformAndStorageBufferStates)
{
    RenderGraph graph;
    auto uniformBacking = std::make_shared<TestBuffer>(BufferCreateInfo{
        .label = "frame.uniform",
        .size = 256,
        .usage = EBufferUsage::UniformBuffer,
    });
    const auto uniformBuffer = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "frame.uniform",
            .usage = EBufferUsage::UniformBuffer,
            .size  = 256,
        },
        .buffer = uniformBacking.get(),
    });
    const auto storageBuffer = graph.createBuffer(RGBufferDesc{
        .label = "cull.storage",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 512,
    });

    graph.addPass("buffer-users", [&](RGPassBuilder& pass) {
        pass.uniformRead(uniformBuffer, {.offset = 32, .size = 64});
        pass.storageWrite(storageBuffer, {.offset = 128, .size = 96});
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    const auto bufferStates = collectBufferStatePlans(compiled);
    ASSERT_EQ(bufferStates.size(), 2u);
    EXPECT_EQ(bufferStates[0].requiredState.access, EResourceAccess::ShaderRead);
    EXPECT_EQ(bufferStates[0].requiredState.offset, 32u);
    EXPECT_EQ(bufferStates[0].requiredState.size, 64u);
    EXPECT_EQ(bufferStates[1].requiredState.access, EResourceAccess::ShaderWrite);
    EXPECT_EQ(bufferStates[1].requiredState.offset, 128u);
    EXPECT_EQ(bufferStates[1].requiredState.size, 96u);
}

TEST(RenderGraphCoreTest, CompileUsesImportedViewRangeForTextureStatePlan)
{
    RenderGraph graph;
    const auto  importedFace = graph.importTexture(RGImportedTextureDesc{
         .desc = RGTextureDesc{
             .label       = "cubemap.face3",
             .format      = EFormat::R16G16B16A16_SFLOAT,
             .extent      = Extent3D{128, 128, 1},
             .mipLevels   = 1,
             .arrayLayers = 1,
             .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
         },
         .importDesc = ImportedImageDesc{
             .label        = "cubemap.face3",
             .nativeHandle = reinterpret_cast<void*>(0x123),
             .format       = EFormat::R16G16B16A16_SFLOAT,
             .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
             .extent       = Extent3D{128, 128, 1},
             .mipLevels    = 4,
             .arrayLayers  = 6,
         },
         .viewDesc = ImageViewCreateInfo{
             .label          = "cubemap.face3.view",
             .viewType       = EImageViewType::View2D,
             .aspectFlags    = EImageAspect::Color,
             .baseMipLevel   = 1,
             .levelCount     = 1,
             .baseArrayLayer = 3,
             .layerCount     = 1,
         },
    });

    graph.addPass("face-writer", [&](RGPassBuilder& pass) {
        pass.useColorAttachment(importedFace);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    const auto textureStates = collectTextureStatePlans(compiled);
    ASSERT_EQ(textureStates.size(), 1u);
    EXPECT_EQ(textureStates[0].requiredState.subresourceRange.baseMipLevel, 1u);
    EXPECT_EQ(textureStates[0].requiredState.subresourceRange.baseArrayLayer, 3u);
    EXPECT_EQ(textureStates[0].requiredState.subresourceRange.layerCount, 1u);
}

TEST(RenderGraphCoreTest, CompileModelsComputeReadWriteToIndirectReadDependency)
{
    RenderGraph graph;
    const auto commands = graph.createBuffer(RGBufferDesc{
        .label = "shadow.commands",
        .usage = EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer,
        .size  = 512,
    });

    const auto cullPass = graph.addPass("cull", [&](RGPassBuilder& pass) {
        pass.storageReadWrite(commands);
    });
    const auto drawPass = graph.addPass("draw", [&](RGPassBuilder& pass) {
        pass.indirectRead(commands);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    const auto bufferStates = collectBufferStatePlans(compiled);
    ASSERT_EQ(bufferStates.size(), 2u);
    EXPECT_EQ(bufferStates[0].requiredState.stages, EPipelineStage::ComputeShader);
    EXPECT_EQ(bufferStates[0].requiredState.access,
              static_cast<EResourceAccess::T>(EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite));
    EXPECT_EQ(bufferStates[1].requiredState.stages, EPipelineStage::DrawIndirect);
    EXPECT_EQ(bufferStates[1].requiredState.access, EResourceAccess::IndirectCommandRead);
    EXPECT_NE(std::find(compiled.dependencies.begin(), compiled.dependencies.end(), RGDependencyEdge{cullPass, drawPass}),
              compiled.dependencies.end());
}

TEST(RenderGraphCoreTest, CompileDoesNotAddDependenciesForNonOverlappingImportedBufferRanges)
{
    RenderGraph graph;
    auto readbackBacking = std::make_shared<TestBuffer>(BufferCreateInfo{
        .label = "readback",
        .size = 256,
        .usage = EBufferUsage::TransferSrc | EBufferUsage::TransferDst,
    });
    const auto readback = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "readback",
            .usage = EBufferUsage::TransferSrc | EBufferUsage::TransferDst,
            .size  = 256,
        },
        .buffer = readbackBacking.get(),
    });

    const auto writer = graph.addPass("writer-low", [&](RGPassBuilder& pass) {
        pass.transferDst(readback, {.offset = 0, .size = 64});
    });
    const auto reader = graph.addPass("reader-high", [&](RGPassBuilder& pass) {
        pass.transferSrc(readback, {.offset = 128, .size = 64});
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    EXPECT_EQ(std::find(compiled.dependencies.begin(), compiled.dependencies.end(), RGDependencyEdge{writer, reader}),
              compiled.dependencies.end());
}

TEST(RenderGraphCoreTest, CompileAddsDependenciesForOverlappingBufferRanges)
{
    RenderGraph graph;
    const auto storage = graph.createBuffer(RGBufferDesc{
        .label = "shared.storage",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    });

    const auto writer = graph.addPass("writer", [&](RGPassBuilder& pass) {
        pass.storageWrite(storage, {.offset = 32, .size = 96});
    });
    const auto reader = graph.addPass("reader", [&](RGPassBuilder& pass) {
        pass.storageRead(storage, {.offset = 64, .size = 32});
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    EXPECT_NE(std::find(compiled.dependencies.begin(), compiled.dependencies.end(), RGDependencyEdge{writer, reader}),
              compiled.dependencies.end());
}

TEST(RenderGraphCoreTest, CompileRejectsIndirectReadWithoutIndirectUsage)
{
    RenderGraph graph;
    auto commandsBacking = std::make_shared<TestBuffer>(BufferCreateInfo{
        .label = "storage-only.commands",
        .size = 128,
        .usage = EBufferUsage::StorageBuffer,
    });
    const auto commands = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "storage-only.commands",
            .usage = EBufferUsage::StorageBuffer,
            .size  = 128,
        },
        .buffer = commandsBacking.get(),
    });
    graph.addPass("draw", [&](RGPassBuilder& pass) {
        pass.indirectRead(commands);
    });

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled.isValid());
    ASSERT_EQ(compiled.issues.size(), 1u);
    EXPECT_EQ(compiled.issues.front().kind, RGCompileIssue::EKind::InvalidUsage);
}

TEST(RenderGraphCoreTest, ImportedSubresourceHelperKeepsProvidedViewAndCompileRange)
{
    TestResourceFactory factory;
    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "shadow.atlas",
        .format      = EFormat::D32_SFLOAT,
        .extent      = {.width = 512, .height = 512, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 7,
        .usage       = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
    });
    ImageViewCreateInfo viewDesc{
        .label          = "shadow.directional.view",
        .viewType       = EImageViewType::View2D,
        .aspectFlags    = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    auto existingView = factory.createImageView(existingImage, viewDesc);
    const auto createdViewsBeforeSync = factory.createdViews;

    RenderGraph graph;
    const auto shadowDepth = graph.importTexture(makeImportedTextureDesc(
        existingImage,
        existingView,
        "shadow.directional",
        EImageLayout::ShaderReadOnlyOptimal));

    graph.addPass("shadow-consumer", [&](RGPassBuilder& pass) {
        pass.read(shadowDepth);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    const auto textureStates = collectTextureStatePlans(compiled);
    ASSERT_EQ(textureStates.size(), 1u);
    EXPECT_EQ(textureStates[0].requiredState.subresourceRange.aspectMask, EImageAspect::Depth);
    EXPECT_EQ(textureStates[0].requiredState.subresourceRange.baseArrayLayer, 0u);
    EXPECT_EQ(textureStates[0].requiredState.subresourceRange.layerCount, 1u);

    RenderGraphResourceRegistry registry(factory);
    registry.sync(graph);

    const auto* imported = registry.resolveTexture(shadowDepth);
    ASSERT_NE(imported, nullptr);
    EXPECT_EQ(imported->getImage(), existingImage.get());
    EXPECT_EQ(imported->getImageView(), existingView.get());
    EXPECT_EQ(factory.createdViews, createdViewsBeforeSync);
}

TEST(RenderGraphCoreTest, ImportTexturePreservesExplicitSubresourceViewDimensions)
{
    TestResourceFactory factory;
    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "shadow.array",
        .format      = EFormat::D32_SFLOAT,
        .extent      = {.width = 1024, .height = 1024, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 42,
        .usage       = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
    });
    auto existingView = factory.createImageView(existingImage, ImageViewCreateInfo{
        .label          = "shadow.array.slice0",
        .viewType       = EImageViewType::View2D,
        .aspectFlags    = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    });

    RenderGraph graph;
    const auto  imported = graph.importTexture(makeImportedTextureDesc(
        existingImage,
        existingView,
        "shadow.array.slice0",
        EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::DepthStencilAttachment));

    const auto* resource = graph.getTexture(imported);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->desc.mipLevels, 1u);
    EXPECT_EQ(resource->desc.arrayLayers, 1u);
    ASSERT_TRUE(resource->imported.has_value());
    ASSERT_TRUE(resource->imported->subresourceRange.has_value());
    EXPECT_EQ(resource->imported->subresourceRange->levelCount, 1u);
    EXPECT_EQ(resource->imported->subresourceRange->layerCount, 1u);
}

TEST(RenderGraphCoreTest, ResourceRegistryCreatesTransientAndImportedResources)
{
    RenderGraph graph;
    const auto transientTexture = graph.createTexture(RGTextureDesc{
        .label  = "gbuffer.normal",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{512, 512, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    const auto importedTexture = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label = "swapchain",
        },
        .importDesc = ImportedImageDesc{
            .label        = "swapchain",
            .nativeHandle = reinterpret_cast<void*>(0x1),
            .format       = EFormat::B8G8R8A8_UNORM,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{512, 512, 1},
        },
    });
    const auto transientBuffer = graph.createBuffer(RGBufferDesc{
        .label = "lighting.constants",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 1024,
    });
    TestBuffer importedBacking(BufferCreateInfo{
        .label = "external.readback",
        .usage = EBufferUsage::TransferDst,
        .size  = 128,
    });
    const auto importedBuffer = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "external.readback",
            .usage = EBufferUsage::TransferDst,
            .size  = 128,
        },
        .buffer = &importedBacking,
    });

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    registry.sync(graph);

    ASSERT_NE(registry.resolveTexture(transientTexture), nullptr);
    ASSERT_NE(registry.resolveTexture(importedTexture), nullptr);
    ASSERT_NE(registry.resolveBuffer(transientBuffer), nullptr);
    EXPECT_EQ(registry.resolveBuffer(importedBuffer), &importedBacking);
    EXPECT_EQ(factory.createdImages, 1u);
    EXPECT_EQ(factory.importedImages, 1u);
    EXPECT_EQ(factory.createdBuffers, 1u);
    EXPECT_EQ(factory.createdViews, 2u);
}

TEST(RenderGraphCoreTest, ResourceRegistryMaterializesOneBufferPerCompiledTransientSlot)
{
    RenderGraph graph;
    const auto first = graph.createBuffer(RGBufferDesc{
        .label = "slot.first",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 64,
    });
    const auto second = graph.createBuffer(RGBufferDesc{
        .label = "slot.second",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 128,
    });
    graph.addPass("write-first", [&](RGPassBuilder& pass) {
        pass.storageWrite(first);
    });
    graph.addPass("write-second", [&](RGPassBuilder& pass) {
        pass.storageWrite(second);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.transientBufferSlots.size(), 1u);

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    registry.sync(graph, &compiled);

    ASSERT_NE(registry.resolveBuffer(first), nullptr);
    ASSERT_EQ(registry.resolveBuffer(first), registry.resolveBuffer(second));
    ASSERT_EQ(factory.createdBuffers, 1u);
    ASSERT_EQ(factory.createdBufferDescs.size(), 1u);
    EXPECT_EQ(factory.createdBufferDescs.front().size, 128u);
    EXPECT_EQ(factory.createdBufferDescs.front().usage, EBufferUsage::StorageBuffer);
}

TEST(RenderGraphCoreTest, ResourceRegistryReusesTransientBufferPoolAcrossFramesAndGrowsSafely)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    RenderGraph graphA;
    const auto bufferA = graphA.createBuffer(RGBufferDesc{
        .label       = "pool.a",
        .usage       = EBufferUsage::StorageBuffer,
        .size        = 64,
        .memoryUsage = EMemoryUsage::GpuOnly,
    });
    graphA.addPass("write-a", [&](RGPassBuilder& pass) {
        pass.storageWrite(bufferA);
    });
    const auto compiledA = graphA.compile();
    ASSERT_TRUE(compiledA.isValid());
    registry.sync(graphA, &compiledA);
    auto* firstOwner = registry.resolveBuffer(bufferA);
    ASSERT_NE(firstOwner, nullptr);
    EXPECT_EQ(factory.createdBuffers, 1u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().lastHitCount, 0u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().lastMissCount, 1u);

    RenderGraph graphB;
    const auto bufferB = graphB.createBuffer(RGBufferDesc{
        .label       = "pool.b",
        .usage       = EBufferUsage::StorageBuffer,
        .size        = 32,
        .memoryUsage = EMemoryUsage::GpuOnly,
    });
    graphB.addPass("write-b", [&](RGPassBuilder& pass) {
        pass.storageWrite(bufferB);
    });
    const auto compiledB = graphB.compile();
    ASSERT_TRUE(compiledB.isValid());
    registry.sync(graphB, &compiledB);
    EXPECT_EQ(registry.resolveBuffer(bufferB), firstOwner);
    EXPECT_EQ(factory.createdBuffers, 1u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().lastHitCount, 1u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().lastMissCount, 0u);

    RenderGraph graphC;
    const auto bufferC = graphC.createBuffer(RGBufferDesc{
        .label       = "pool.c",
        .usage       = EBufferUsage::StorageBuffer,
        .size        = 128,
        .memoryUsage = EMemoryUsage::GpuOnly,
    });
    graphC.addPass("write-c", [&](RGPassBuilder& pass) {
        pass.storageWrite(bufferC);
    });
    const auto compiledC = graphC.compile();
    ASSERT_TRUE(compiledC.isValid());
    registry.sync(graphC, &compiledC);
    auto* grownOwner = registry.resolveBuffer(bufferC);
    ASSERT_NE(grownOwner, nullptr);
    EXPECT_NE(grownOwner, firstOwner);
    EXPECT_EQ(factory.createdBuffers, 2u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().lastHitCount, 0u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().lastMissCount, 1u);

    RenderGraph graphD;
    const auto bufferD = graphD.createBuffer(RGBufferDesc{
        .label       = "pool.d",
        .usage       = EBufferUsage::StorageBuffer,
        .size        = 32,
        .memoryUsage = EMemoryUsage::CpuToGpu,
    });
    graphD.addPass("write-d", [&](RGPassBuilder& pass) {
        pass.storageWrite(bufferD);
    });
    const auto compiledD = graphD.compile();
    ASSERT_TRUE(compiledD.isValid());
    registry.sync(graphD, &compiledD);
    EXPECT_NE(registry.resolveBuffer(bufferD), nullptr);
    EXPECT_EQ(factory.createdBuffers, 3u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().totalHitCount, 1u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().totalMissCount, 3u);
}

TEST(RenderGraphCoreTest, ResourceRegistryLegacySyncKeepsLogicalTransientBuffersSeparate)
{
    RenderGraph graph;
    const auto first = graph.createBuffer(RGBufferDesc{
        .label = "legacy.first",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 64,
    });
    const auto second = graph.createBuffer(RGBufferDesc{
        .label = "legacy.second",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 64,
    });
    graph.addPass("write-first", [&](RGPassBuilder& pass) {
        pass.storageWrite(first);
    });
    graph.addPass("write-second", [&](RGPassBuilder& pass) {
        pass.storageWrite(second);
    });

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    registry.sync(graph);

    ASSERT_NE(registry.resolveBuffer(first), nullptr);
    ASSERT_NE(registry.resolveBuffer(second), nullptr);
    EXPECT_NE(registry.resolveBuffer(first), registry.resolveBuffer(second));
    EXPECT_EQ(factory.createdBuffers, 2u);
    EXPECT_EQ(registry.getTransientBufferPoolDiagnostics().poolEntryCount, 0u);
}

TEST(RenderGraphCoreTest, ExecutorForcesBarrierAtTransientAliasBoundary)
{
    RenderGraph graph;
    const auto first = graph.createBuffer(RGBufferDesc{
        .label = "alias.first",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 64,
    });
    const auto second = graph.createBuffer(RGBufferDesc{
        .label = "alias.second",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 128,
    });
    graph.addPass("write-first", [&](RGPassBuilder& pass) {
        pass.storageWrite(first);
    });
    graph.addPass("write-second", [&](RGPassBuilder& pass) {
        pass.storageWrite(second);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.transientBufferSlots.size(), 1u);
    ASSERT_EQ(compiled.transientBufferAliasBoundaries.size(), 1u);

    TestResourceFactory factory;
    TestCommandBuffer   commandBuffer;
    RenderGraphExecutor executor(factory);
    ASSERT_TRUE(executor.execute(graph, commandBuffer));

    ASSERT_EQ(commandBuffer.bufferBarriers.size(), 2u);
    EXPECT_EQ(commandBuffer.bufferBarriers[0].size, 64u);
    EXPECT_EQ(commandBuffer.bufferBarriers[1].offset, 0u);
    EXPECT_EQ(commandBuffer.bufferBarriers[1].size, 128u);
}

TEST(RenderGraphCoreTest, ResourceRegistryCanImportExistingImageWithCustomViewDesc)
{
    TestResourceFactory factory;
    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "existing.cubemap",
        .format      = EFormat::R16G16B16A16_SFLOAT,
        .extent      = {.width = 256, .height = 256, .depth = 1},
        .mipLevels   = 4,
        .arrayLayers = 6,
        .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    RenderGraph graph;
    const auto faceHandle = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = "existing.cubemap.face2",
            .format      = EFormat::R16G16B16A16_SFLOAT,
            .extent      = Extent3D{256, 256, 1},
            .mipLevels   = 1,
            .arrayLayers = 1,
            .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        },
        .importDesc = ImportedImageDesc{
            .label        = "existing.cubemap.face2",
            .nativeHandle = static_cast<void*>(existingImage->getHandle()),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{256, 256, 1},
            .mipLevels    = 4,
            .arrayLayers  = 6,
        },
        .image = existingImage,
        .viewDesc = ImageViewCreateInfo{
            .label          = "existing.cubemap.face2.view",
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 1,
            .levelCount     = 1,
            .baseArrayLayer = 2,
            .layerCount     = 1,
        },
    });

    RenderGraphResourceRegistry registry(factory);
    registry.sync(graph);

    const auto* imported = registry.resolveTexture(faceHandle);
    ASSERT_NE(imported, nullptr);
    ASSERT_NE(imported->getImage(), nullptr);
    EXPECT_EQ(imported->getImage(), existingImage.get());
    EXPECT_EQ(factory.importedImages, 0u);
    EXPECT_EQ(factory.createdViews, 1u);
}

TEST(RenderGraphCoreTest, ResourceRegistryUsesProvidedImportedImageViewAndRetainsOwner)
{
    TestResourceFactory factory;
    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "existing.color",
        .format      = EFormat::R8G8B8A8_UNORM,
        .extent      = {.width = 128, .height = 128, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    auto existingView = factory.createImageView(existingImage, ImageViewCreateInfo{
        .label       = "existing.color.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });
    const auto createdViewsBeforeSync = factory.createdViews;

    RenderGraphResourceRegistry registry(factory);
    std::weak_ptr<int>          retainedOwner;
    RGTextureHandle             handle{};

    {
        RenderGraph graph;
        auto owner = std::make_shared<int>(7);
        retainedOwner = owner;
        handle = graph.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label  = "existing.color.import",
                .format = EFormat::R8G8B8A8_UNORM,
                .extent = Extent3D{128, 128, 1},
                .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            },
            .importDesc = ImportedImageDesc{
                .label        = "existing.color.import",
                .nativeHandle = static_cast<void*>(existingImage->getHandle()),
                .format       = EFormat::R8G8B8A8_UNORM,
                .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .extent       = Extent3D{128, 128, 1},
            },
            .image = existingImage,
            .imageView = existingView,
            .retainedResources = {owner},
        });

        registry.sync(graph);
        owner.reset();

        const auto* imported = registry.resolveTexture(handle);
        ASSERT_NE(imported, nullptr);
        EXPECT_EQ(imported->getImage(), existingImage.get());
        EXPECT_EQ(imported->getImageView(), existingView.get());
        EXPECT_FALSE(retainedOwner.expired());
    }

    EXPECT_FALSE(retainedOwner.expired());
    EXPECT_EQ(factory.importedImages, 0u);
    EXPECT_EQ(factory.createdViews, createdViewsBeforeSync);

    registry.clear();
    EXPECT_TRUE(retainedOwner.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryReusesStableResourcesAcrossSyncs)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    RenderGraph graphA;
    const auto textureHandle = graphA.createPersistentTexture(RGTextureDesc{
        .label  = "persistent.ao",
        .format = EFormat::R8_UNORM,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "persistent.ao"});
    const auto bufferHandle = graphA.createPersistentBuffer(RGBufferDesc{
        .label = "persistent.constants",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    }, RGPersistentBufferKey{.value = "persistent.constants"});

    registry.sync(graphA);
    const auto* firstTexture = registry.resolveTexture(textureHandle);
    auto        firstTextureOwner = registry.resolveTextureShared(textureHandle);
    auto*       firstBuffer  = registry.resolveBuffer(bufferHandle);
    ASSERT_NE(firstTexture, nullptr);
    ASSERT_NE(firstTextureOwner, nullptr);
    EXPECT_EQ(firstTextureOwner.get(), firstTexture);
    ASSERT_NE(firstBuffer, nullptr);
    EXPECT_EQ(factory.createdImages, 1u);
    EXPECT_EQ(factory.createdViews, 1u);
    EXPECT_EQ(factory.createdBuffers, 1u);

    RenderGraph graphB;
    const auto transientBefore = graphB.createTexture(RGTextureDesc{
        .label  = "transient.before",
        .format = EFormat::R8_UNORM,
        .extent = Extent3D{32, 32, 1},
        .usage  = EImageUsage::ColorAttachment,
    });
    (void)transientBefore;
    const auto textureHandleB = graphB.createPersistentTexture(RGTextureDesc{
        .label  = "persistent.ao",
        .format = EFormat::R8_UNORM,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "persistent.ao"});
    const auto bufferHandleB = graphB.createPersistentBuffer(RGBufferDesc{
        .label = "persistent.constants",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    }, RGPersistentBufferKey{.value = "persistent.constants"});

    registry.sync(graphB);
    EXPECT_EQ(registry.resolveTexture(textureHandleB), firstTexture);
    EXPECT_EQ(registry.resolveBuffer(bufferHandleB), firstBuffer);
    EXPECT_EQ(factory.createdImages, 2u);
    EXPECT_EQ(factory.createdViews, 2u);
    EXPECT_EQ(factory.createdBuffers, 1u);
}

TEST(RenderGraphCoreTest, ResourceRegistryKeepsPersistentResourcesAcrossTemporaryOmission)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    RenderGraph graphA;
    const auto textureHandleA = graphA.createPersistentTexture(RGTextureDesc{
        .label  = "persistent.history",
        .format = EFormat::R32_SFLOAT,
        .extent = Extent3D{160, 90, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "persistent.history"});
    const auto bufferHandleA = graphA.createPersistentBuffer(RGBufferDesc{
        .label = "persistent.history.constants",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 128,
    }, RGPersistentBufferKey{.value = "persistent.history.constants"});

    registry.sync(graphA);
    const auto* firstTexture = registry.resolveTexture(textureHandleA);
    auto* firstBuffer = registry.resolveBuffer(bufferHandleA);
    ASSERT_NE(firstTexture, nullptr);
    ASSERT_NE(firstBuffer, nullptr);

    RenderGraph graphB;
    const auto transientOnly = graphB.createTexture(RGTextureDesc{
        .label  = "transient.only",
        .format = EFormat::R8_UNORM,
        .extent = Extent3D{16, 16, 1},
        .usage  = EImageUsage::ColorAttachment,
    });
    (void)transientOnly;
    registry.sync(graphB);
    EXPECT_EQ(factory.createdImages, 2u);
    EXPECT_EQ(factory.createdBuffers, 1u);

    RenderGraph graphC;
    const auto textureHandleC = graphC.createPersistentTexture(RGTextureDesc{
        .label  = "persistent.history",
        .format = EFormat::R32_SFLOAT,
        .extent = Extent3D{160, 90, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "persistent.history"});
    const auto bufferHandleC = graphC.createPersistentBuffer(RGBufferDesc{
        .label = "persistent.history.constants",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 128,
    }, RGPersistentBufferKey{.value = "persistent.history.constants"});

    registry.sync(graphC);
    EXPECT_EQ(registry.resolveTexture(textureHandleC), firstTexture);
    EXPECT_EQ(registry.resolveBuffer(bufferHandleC), firstBuffer);
    EXPECT_EQ(factory.createdImages, 2u);
    EXPECT_EQ(factory.createdViews, 2u);
    EXPECT_EQ(factory.createdBuffers, 1u);
}

TEST(RenderGraphCoreTest, ResourceRegistryRefreshesImportedKeepAliveWithoutRecreatingView)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "persistent.imported",
        .format      = EFormat::R16G16B16A16_SFLOAT,
        .extent      = {.width = 64, .height = 64, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    auto existingView = factory.createImageView(existingImage, ImageViewCreateInfo{
        .label       = "persistent.imported.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });
    const auto createdViewsBeforeSync = factory.createdViews;

    std::weak_ptr<int> ownerAWeak;
    {
        RenderGraph graphA;
        auto ownerA = std::make_shared<int>(1);
        ownerAWeak = ownerA;
        graphA.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label  = "persistent.imported",
                .format = EFormat::R16G16B16A16_SFLOAT,
                .extent = Extent3D{64, 64, 1},
                .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            },
            .importDesc = ImportedImageDesc{
                .label        = "persistent.imported",
                .nativeHandle = static_cast<void*>(existingImage->getHandle()),
                .format       = EFormat::R16G16B16A16_SFLOAT,
                .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .extent       = Extent3D{64, 64, 1},
            },
            .image = existingImage,
            .imageView = existingView,
            .retainedResources = {ownerA},
        });
        registry.sync(graphA);
        ownerA.reset();
    }

    EXPECT_FALSE(ownerAWeak.expired());

    std::weak_ptr<int> ownerBWeak;
    {
        RenderGraph graphB;
        auto ownerB = std::make_shared<int>(2);
        ownerBWeak = ownerB;
        graphB.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label  = "persistent.imported",
                .format = EFormat::R16G16B16A16_SFLOAT,
                .extent = Extent3D{64, 64, 1},
                .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            },
            .importDesc = ImportedImageDesc{
                .label        = "persistent.imported",
                .nativeHandle = static_cast<void*>(existingImage->getHandle()),
                .format       = EFormat::R16G16B16A16_SFLOAT,
                .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .extent       = Extent3D{64, 64, 1},
            },
            .image = existingImage,
            .imageView = existingView,
            .retainedResources = {ownerB},
        });
        registry.sync(graphB);
        ownerB.reset();
    }

    EXPECT_FALSE(ownerAWeak.expired());
    EXPECT_FALSE(ownerBWeak.expired());
    EXPECT_EQ(factory.importedImages, 0u);
    EXPECT_EQ(factory.createdViews, createdViewsBeforeSync);

    registry.clear();
    EXPECT_FALSE(ownerAWeak.expired());
    EXPECT_FALSE(ownerBWeak.expired());
    deletionQueue.flushAll();
    EXPECT_TRUE(ownerAWeak.expired());
    EXPECT_TRUE(ownerBWeak.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryDestructorDefersImportedKeepAliveReleaseThroughDeletionQueue)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "deferred.imported",
        .format      = EFormat::R16G16B16A16_SFLOAT,
        .extent      = {.width = 64, .height = 64, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    auto existingView = factory.createImageView(existingImage, ImageViewCreateInfo{
        .label       = "deferred.imported.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });

    std::weak_ptr<int> retainedOwner;
    {
        RenderGraphResourceRegistry registry(factory);
        RenderGraph graph;
        auto owner = std::make_shared<int>(42);
        retainedOwner = owner;
        graph.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label  = "deferred.imported",
                .format = EFormat::R16G16B16A16_SFLOAT,
                .extent = Extent3D{64, 64, 1},
                .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            },
            .importDesc = ImportedImageDesc{
                .label        = "deferred.imported",
                .nativeHandle = static_cast<void*>(existingImage->getHandle()),
                .format       = EFormat::R16G16B16A16_SFLOAT,
                .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .extent       = Extent3D{64, 64, 1},
            },
            .image = existingImage,
            .imageView = existingView,
            .retainedResources = {owner},
        });

        registry.sync(graph);
        owner.reset();
        EXPECT_FALSE(retainedOwner.expired());
    }

    EXPECT_FALSE(retainedOwner.expired());

    deletionQueue.flushAll();
    EXPECT_TRUE(retainedOwner.expired());
}

TEST(RenderGraphCoreTest, ExecutorClearDefersImportedTextureKeepAliveReleaseThroughDeletionQueue)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "presentation.imported",
        .format      = EFormat::R16G16B16A16_SFLOAT,
        .extent      = {.width = 64, .height = 64, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    auto existingView = factory.createImageView(existingImage, ImageViewCreateInfo{
        .label       = "presentation.imported.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });

    std::weak_ptr<int> retainedOwner;
    {
        RenderGraph graph;
        auto owner = std::make_shared<int>(99);
        retainedOwner = owner;
        const auto imported = graph.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label  = "presentation.imported",
                .format = EFormat::R16G16B16A16_SFLOAT,
                .extent = Extent3D{64, 64, 1},
                .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            },
            .importDesc = ImportedImageDesc{
                .label        = "presentation.imported",
                .nativeHandle = static_cast<void*>(existingImage->getHandle()),
                .format       = EFormat::R16G16B16A16_SFLOAT,
                .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .extent       = Extent3D{64, 64, 1},
            },
            .image = existingImage,
            .imageView = existingView,
            .retainedResources = {owner},
        });
        graph.addPass(
            "presentation-reader",
            [=](RGPassBuilder& pass) { pass.read(imported); },
            [](RGRenderContext&) {});

        ASSERT_TRUE(executor.execute(graph, cmdBuf));
        owner.reset();
        EXPECT_FALSE(retainedOwner.expired());
    }

    executor.clear();
    EXPECT_FALSE(retainedOwner.expired());

    deletionQueue.flushAll();
    EXPECT_TRUE(retainedOwner.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryReplacesResourcesWhenDescriptorsChange)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    RenderGraph graphA;
    const auto textureHandle = graphA.createPersistentTexture(RGTextureDesc{
        .label  = "persistent.history",
        .format = EFormat::R32_SFLOAT,
        .extent = Extent3D{160, 90, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "persistent.history"});
    const auto bufferHandle = graphA.createPersistentBuffer(RGBufferDesc{
        .label = "persistent.history.buffer",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 128,
    }, RGPersistentBufferKey{.value = "persistent.history.buffer"});

    registry.sync(graphA);
    const auto* firstTexture = registry.resolveTexture(textureHandle);
    auto*       firstBuffer  = registry.resolveBuffer(bufferHandle);
    ASSERT_NE(firstTexture, nullptr);
    ASSERT_NE(firstBuffer, nullptr);

    RenderGraph graphB;
    graphB.createPersistentTexture(RGTextureDesc{
        .label  = "persistent.history",
        .format = EFormat::R32_SFLOAT,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "persistent.history"});
    graphB.createPersistentBuffer(RGBufferDesc{
        .label = "persistent.history.buffer",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    }, RGPersistentBufferKey{.value = "persistent.history.buffer"});

    registry.sync(graphB);
    ASSERT_NE(registry.resolveTexture(textureHandle), nullptr);
    ASSERT_NE(registry.resolveBuffer(bufferHandle), nullptr);
    EXPECT_EQ(registry.resolveTexture(textureHandle)->getWidth(), 320u);
    EXPECT_EQ(registry.resolveTexture(textureHandle)->getHeight(), 180u);
    EXPECT_EQ(registry.resolveBuffer(bufferHandle)->getSize(), 256u);
    EXPECT_EQ(factory.createdImages, 2u);
    EXPECT_EQ(factory.createdViews, 2u);
    EXPECT_EQ(factory.createdBuffers, 2u);
    ASSERT_EQ(factory.createdImageDescs.size(), 2u);
    EXPECT_EQ(factory.createdImageDescs[1].extent.width, 320u);
    EXPECT_EQ(factory.createdImageDescs[1].extent.height, 180u);
    ASSERT_EQ(factory.createdBufferDescs.size(), 2u);
    EXPECT_EQ(factory.createdBufferDescs[1].size, 256u);
}

TEST(RenderGraphCoreTest, PrepareCapturesExplicitExportedTexturesOnly)
{
    TestResourceFactory factory;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    const auto exported = graph.createPersistentTexture(RGTextureDesc{
        .label  = "viewport",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{640, 360, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "viewport"});
    graph.createTexture(RGTextureDesc{
        .label  = "hidden",
        .format = EFormat::R8G8B8A8_UNORM,
        .extent = Extent3D{64, 64, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    graph.exportTexture(exported, "ViewportColor");

    RGCompiledGraph           compiled;
    RenderGraphExecutionResult result;
    ASSERT_TRUE(executor.prepare(graph, compiled, &result));
    ASSERT_TRUE(compiled.isValid());
    EXPECT_TRUE(result.hasExportedTexture("ViewportColor"));
    EXPECT_NE(result.getExportedTextureShared("ViewportColor"), nullptr);
    EXPECT_EQ(result.getExportedTexture("Missing"), nullptr);
    EXPECT_FALSE(result.hasExportedTexture("hidden"));
}

TEST(RenderGraphCoreTest, ExportedTextureOwnerSurvivesReplacementAcrossPrepare)
{
    TestResourceFactory factory;
    RenderGraphExecutor executor(factory);

    RenderGraph graphA;
    const auto textureA = graphA.createPersistentTexture(RGTextureDesc{
        .label  = "persistent.viewport",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "persistent.viewport"});
    graphA.exportTexture(textureA, "ViewportColor");

    RGCompiledGraph           compiledA;
    RenderGraphExecutionResult resultA;
    ASSERT_TRUE(executor.prepare(graphA, compiledA, &resultA));
    auto firstOwner = resultA.getExportedTextureShared("ViewportColor");
    ASSERT_NE(firstOwner, nullptr);
    EXPECT_EQ(firstOwner->getWidth(), 320u);

    RenderGraph graphB;
    const auto textureB = graphB.createPersistentTexture(RGTextureDesc{
        .label  = "persistent.viewport",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{640, 360, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "persistent.viewport"});
    graphB.exportTexture(textureB, "ViewportColor");

    RGCompiledGraph           compiledB;
    RenderGraphExecutionResult resultB;
    ASSERT_TRUE(executor.prepare(graphB, compiledB, &resultB));
    auto secondOwner = resultB.getExportedTextureShared("ViewportColor");
    ASSERT_NE(secondOwner, nullptr);
    EXPECT_EQ(secondOwner->getWidth(), 640u);
    EXPECT_NE(firstOwner.get(), secondOwner.get());
    EXPECT_EQ(firstOwner->getWidth(), 320u);
}

TEST(RenderGraphCoreTest, ResourceRegistryReimportsTextureWhenImportedDescChanges)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    RenderGraph graphA;
    const auto importedHandle = graphA.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "history.imported",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{128, 128, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        },
        .importDesc = ImportedImageDesc{
            .label        = "history.imported",
            .nativeHandle = reinterpret_cast<void*>(0x101),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{128, 128, 1},
        },
    });

    registry.sync(graphA);
    ASSERT_NE(registry.resolveTexture(importedHandle), nullptr);
    EXPECT_EQ(factory.importedImages, 1u);
    EXPECT_EQ(factory.createdViews, 1u);

    RenderGraph graphB;
    graphB.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "history.imported",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{128, 128, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        },
        .importDesc = ImportedImageDesc{
            .label        = "history.imported",
            .nativeHandle = reinterpret_cast<void*>(0x202),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{128, 128, 1},
        },
    });

    registry.sync(graphB);
    ASSERT_NE(registry.resolveTexture(importedHandle), nullptr);
    EXPECT_EQ(factory.importedImages, 2u);
    EXPECT_EQ(factory.createdViews, 2u);
    ASSERT_EQ(factory.importedImageDescs.size(), 2u);
    EXPECT_EQ(factory.importedImageDescs[1].nativeHandle, reinterpret_cast<void*>(0x202));
}

TEST(RenderGraphCoreTest, ImageViewDescKeyIgnoresDebugLabel)
{
    const ImageViewCreateInfo base{
        .label          = "view.a",
        .viewType       = EImageViewType::View2DArray,
        .aspectFlags    = EImageAspect::Color,
        .baseMipLevel   = 1,
        .levelCount     = 2,
        .baseArrayLayer = 3,
        .layerCount     = 4,
    };
    auto relabeled = base;
    relabeled.label = "view.b";

    EXPECT_NE(base.label, relabeled.label);
    EXPECT_TRUE(isSameImageViewDescKey(makeImageViewDescKey(base), makeImageViewDescKey(relabeled)));
    EXPECT_FALSE(isSameImageViewCreateInfo(base, relabeled));
}

TEST(RenderGraphCoreTest, ResourceRegistryKeepsImportedTextureWhenOnlyViewLabelChanges)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    const auto buildGraph = [](std::string_view viewLabel) {
        RenderGraph graph;
        RGImportedTextureDesc importedDesc{};
        importedDesc.desc = RGTextureDesc{
            .label       = "history.imported.face",
            .format      = EFormat::R16G16B16A16_SFLOAT,
            .extent      = Extent3D{128, 128, 1},
            .mipLevels   = 1,
            .arrayLayers = 1,
            .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        };
        importedDesc.importDesc = ImportedImageDesc{
            .label        = "history.imported.face",
            .nativeHandle = reinterpret_cast<void*>(0x303),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{128, 128, 1},
            .mipLevels    = 4,
            .arrayLayers  = 6,
        };
        importedDesc.viewDesc = ImageViewCreateInfo{
            .label          = std::string(viewLabel),
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 2,
            .levelCount     = 1,
            .baseArrayLayer = 5,
            .layerCount     = 1,
        };
        graph.importTexture(importedDesc);
        return graph;
    };

    auto graphA = buildGraph("history.imported.face.view.a");
    registry.sync(graphA);
    EXPECT_EQ(factory.importedImages, 1u);
    EXPECT_EQ(factory.createdViews, 1u);

    auto graphB = buildGraph("history.imported.face.view.b");
    registry.sync(graphB);
    EXPECT_EQ(factory.importedImages, 1u);
    EXPECT_EQ(factory.createdViews, 1u);
}

TEST(RenderGraphCoreTest, ResourceRegistryReimportsTextureWhenViewIdentityChanges)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    RenderGraph graphA;
    {
        RGImportedTextureDesc importedDesc{};
        importedDesc.desc = RGTextureDesc{
            .label       = "history.imported.face",
            .format      = EFormat::R16G16B16A16_SFLOAT,
            .extent      = Extent3D{128, 128, 1},
            .mipLevels   = 1,
            .arrayLayers = 1,
            .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        };
        importedDesc.importDesc = ImportedImageDesc{
            .label        = "history.imported.face",
            .nativeHandle = reinterpret_cast<void*>(0x404),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{128, 128, 1},
            .mipLevels    = 4,
            .arrayLayers  = 6,
        };
        importedDesc.viewDesc = ImageViewCreateInfo{
            .label          = "history.imported.face.view",
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        graphA.importTexture(importedDesc);
    }

    registry.sync(graphA);
    EXPECT_EQ(factory.importedImages, 1u);
    EXPECT_EQ(factory.createdViews, 1u);

    RenderGraph graphB;
    {
        RGImportedTextureDesc importedDesc{};
        importedDesc.desc = RGTextureDesc{
            .label       = "history.imported.face",
            .format      = EFormat::R16G16B16A16_SFLOAT,
            .extent      = Extent3D{128, 128, 1},
            .mipLevels   = 1,
            .arrayLayers = 1,
            .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        };
        importedDesc.importDesc = ImportedImageDesc{
            .label        = "history.imported.face",
            .nativeHandle = reinterpret_cast<void*>(0x404),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{128, 128, 1},
            .mipLevels    = 4,
            .arrayLayers  = 6,
        };
        importedDesc.viewDesc = ImageViewCreateInfo{
            .label          = "history.imported.face.view",
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 1,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        graphB.importTexture(importedDesc);
    }

    registry.sync(graphB);
    EXPECT_EQ(factory.importedImages, 2u);
    EXPECT_EQ(factory.createdViews, 2u);
}

TEST(RenderGraphCoreTest, ResourceRegistryKeepsSharedImportedTextureWhenOnlyLayoutContractChanges)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    auto image = factory.createImage(ImageCreateInfo{
        .label         = "shared.imported.layout.test",
        .format        = EFormat::B8G8R8A8_UNORM,
        .extent        = {.width = 256, .height = 144, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .initialLayout = EImageLayout::Undefined,
    });

    RenderGraph graphA;
    const auto importedHandle = graphA.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "shared.imported.layout.test",
            .format = EFormat::B8G8R8A8_UNORM,
            .extent = Extent3D{256, 144, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        },
        .importDesc = ImportedImageDesc{
            .label         = "shared.imported.layout.test",
            .nativeHandle  = static_cast<void*>(image->getHandle()),
            .format        = EFormat::B8G8R8A8_UNORM,
            .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent        = Extent3D{256, 144, 1},
            .initialLayout = EImageLayout::Undefined,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .image = image,
    });

    registry.sync(graphA);
    auto firstResource = registry.resolveTextureShared(importedHandle);
    ASSERT_NE(firstResource, nullptr);
    EXPECT_EQ(factory.importedImages, 0u);
    EXPECT_EQ(factory.createdViews, 1u);

    RenderGraph graphB;
    graphB.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "shared.imported.layout.test",
            .format = EFormat::B8G8R8A8_UNORM,
            .extent = Extent3D{256, 144, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        },
        .importDesc = ImportedImageDesc{
            .label         = "shared.imported.layout.test",
            .nativeHandle  = static_cast<void*>(image->getHandle()),
            .format        = EFormat::B8G8R8A8_UNORM,
            .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent        = Extent3D{256, 144, 1},
            .initialLayout = EImageLayout::PresentSrcKHR,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        },
        .image = image,
    });

    registry.sync(graphB);
    auto secondResource = registry.resolveTextureShared(importedHandle);
    ASSERT_NE(secondResource, nullptr);
    EXPECT_EQ(factory.importedImages, 0u);
    EXPECT_EQ(factory.createdViews, 1u);
    EXPECT_EQ(secondResource, firstResource);
}

TEST(RenderGraphCoreTest, ImageViewDoesNotOwnImageLifetime)
{
    TestResourceFactory factory;
    auto image = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "ownership.test",
        .format      = EFormat::R8G8B8A8_UNORM,
        .extent      = {.width = 32, .height = 32, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .usage       = EImageUsage::Sampled,
    });
    ASSERT_EQ(image.use_count(), 1);

    auto view = factory.createImageView(image, ImageViewCreateInfo{
        .label       = "ownership.test.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });

    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->getImage(), image.get());
    EXPECT_EQ(image.use_count(), 1);
}

TEST(RenderGraphCoreTest, RenderImageDestroysViewBeforeImage)
{
    std::vector<std::string> destructionOrder;

    {
        auto image = std::make_shared<TrackedImage>(&destructionOrder);
        auto view  = std::make_shared<TrackedImageView>(image.get(), &destructionOrder);

        auto renderImage         = std::make_shared<RenderImage>();
        renderImage->label       = "ordered.render.image";
        renderImage->image       = std::move(image);
        renderImage->defaultView = std::move(view);
    }

    ASSERT_EQ(destructionOrder.size(), 2u);
    EXPECT_EQ(destructionOrder[0], "view");
    EXPECT_EQ(destructionOrder[1], "image");
}

TEST(RenderGraphCoreTest, AttachmentImageSpecRetainsSharedOwnersAndSubresourceRange)
{
    TestResourceFactory factory;
    auto image = factory.createImage(ImageCreateInfo{
        .label       = "AttachmentImage",
        .format      = EFormat::R16G16B16A16_SFLOAT,
        .extent      = {.width = 64, .height = 64, .depth = 1},
        .mipLevels   = 4,
        .arrayLayers = 6,
        .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .initialLayout = EImageLayout::ShaderReadOnlyOptimal,
    });
    auto view = factory.createImageView(image, ImageViewCreateInfo{
        .label          = "AttachmentView",
        .viewType       = EImageViewType::View2DArray,
        .aspectFlags    = EImageAspect::Color,
        .baseMipLevel   = 2,
        .levelCount     = 1,
        .baseArrayLayer = 3,
        .layerCount     = 2,
    });

    const auto spec = makeRenderAttachment(
        view.get(),
        EAttachmentLoadOp::Load,
        EAttachmentStoreOp::Store,
        EImageLayout::ColorAttachmentOptimal,
        EImageLayout::ShaderReadOnlyOptimal);

    ASSERT_EQ(spec.image, image.get());
    ASSERT_EQ(spec.imageView, view.get());
    ASSERT_TRUE(spec.bHasSubresourceRange);
    EXPECT_EQ(spec.subresourceAspectMask, EImageAspect::Color);
    EXPECT_EQ(spec.subresourceBaseMipLevel, 2u);
    EXPECT_EQ(spec.subresourceLevelCount, 1u);
    EXPECT_EQ(spec.subresourceBaseArrayLayer, 3u);
    EXPECT_EQ(spec.subresourceLayerCount, 2u);
}

TEST(RenderGraphCoreTest, ImportTextureNormalizesSharedImageBackedDescriptors)
{
    RenderGraph graph;
    auto image = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "normalized.import",
        .format      = EFormat::R16G16B16A16_SFLOAT,
        .extent      = {.width = 96, .height = 48, .depth = 1},
        .mipLevels   = 5,
        .arrayLayers = 6,
        .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .initialLayout = EImageLayout::ShaderReadOnlyOptimal,
    });

    const auto handle = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label = "normalized.import",
        },
        .importDesc = ImportedImageDesc{
            .label       = "normalized.import",
            .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
        },
        .image = image,
    });

    const auto* resource = graph.getTexture(handle);
    ASSERT_NE(resource, nullptr);
    ASSERT_TRUE(resource->imported.has_value());
    EXPECT_EQ(resource->desc.format, EFormat::R16G16B16A16_SFLOAT);
    EXPECT_EQ(resource->desc.extent.width, 96u);
    EXPECT_EQ(resource->desc.extent.height, 48u);
    EXPECT_EQ(resource->desc.mipLevels, 5u);
    EXPECT_EQ(resource->desc.arrayLayers, 6u);
    EXPECT_EQ(resource->desc.usage, static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::Sampled));
    EXPECT_EQ(resource->imported->importDesc.nativeHandle, static_cast<void*>(image->getHandle()));
    EXPECT_EQ(resource->imported->importDesc.format, image->getFormat());
    EXPECT_EQ(resource->imported->importDesc.usage, image->getUsage());
    EXPECT_EQ(resource->imported->importDesc.extent.width, image->getWidth());
    EXPECT_EQ(resource->imported->importDesc.extent.height, image->getHeight());
    EXPECT_EQ(resource->imported->importDesc.mipLevels, image->getMipLevels());
    EXPECT_EQ(resource->imported->importDesc.arrayLayers, image->getArrayLayers());
}

TEST(RenderGraphCoreTest, ImportTextureAllowsSharedImageUsageSubset)
{
    RenderGraph graph;
    auto image = std::make_shared<TestImage>(ImageCreateInfo{
        .label         = "subset.import",
        .format        = EFormat::B8G8R8A8_UNORM,
        .extent        = {.width = 128, .height = 64, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .initialLayout = EImageLayout::ColorAttachmentOptimal,
    });

    const auto handle = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "subset.import",
            .format = EFormat::B8G8R8A8_UNORM,
            .extent = Extent3D{128, 64, 1},
            .usage  = EImageUsage::ColorAttachment,
        },
        .importDesc = ImportedImageDesc{
            .label         = "subset.import",
            .nativeHandle  = static_cast<void*>(image->getHandle()),
            .format        = EFormat::B8G8R8A8_UNORM,
            .usage         = EImageUsage::ColorAttachment,
            .extent        = Extent3D{128, 64, 1},
            .initialLayout = EImageLayout::ColorAttachmentOptimal,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        },
        .image = image,
    });

    const auto* resource = graph.getTexture(handle);
    ASSERT_NE(resource, nullptr);
    ASSERT_TRUE(resource->imported.has_value());
    EXPECT_EQ(resource->desc.usage, EImageUsage::ColorAttachment);
    EXPECT_EQ(resource->imported->importDesc.usage, static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::Sampled));
}

TEST(RenderGraphCoreTest, ExecutorRunsPassesInCompiledOrderAndResolvesResources)
{
    TestResourceFactory      factory;
    TestCommandBuffer        cmdBuf;
    RenderGraphExecutor      executor(factory);
    RenderGraph              graph;
    std::vector<std::string> executionOrder;

    const auto texture = graph.createTexture(RGTextureDesc{
        .label  = "hdr",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{1280, 720, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    const auto buffer = graph.createBuffer(RGBufferDesc{
        .label = "frame.storage",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 512,
    });

    graph.addPass(
        "writer",
        [&](RGPassBuilder& pass) {
            pass.useColorAttachment(texture);
            pass.storageWrite(buffer);
        },
        [&](RGRenderContext& ctx) {
            executionOrder.push_back(ctx.getPass().name);
            EXPECT_EQ(&ctx.getCommandBuffer(), &cmdBuf);
            ASSERT_NE(ctx.resolveTexture(texture), nullptr);
            ASSERT_NE(ctx.resolveBuffer(buffer), nullptr);
            EXPECT_EQ(ctx.getTextureDesc(texture).label, "hdr");
        });

    graph.addPass(
        "reader",
        [&](RGPassBuilder& pass) {
            pass.read(texture);
            pass.storageRead(buffer);
        },
        [&](RGRenderContext& ctx) {
            executionOrder.push_back(ctx.getPass().name);
            ASSERT_NE(ctx.resolveTexture(texture), nullptr);
            ASSERT_NE(ctx.resolveBuffer(buffer), nullptr);
            EXPECT_EQ(ctx.getBufferDesc(buffer).label, "frame.storage");
        });

    RGCompiledGraph compiled;
    ASSERT_TRUE(executor.execute(graph, cmdBuf, &compiled));
    ASSERT_TRUE(compiled.isValid());
    EXPECT_EQ(executionOrder, (std::vector<std::string>{"writer", "reader"}));
    ASSERT_EQ(cmdBuf.transitions.size(), 2u);
    EXPECT_EQ(cmdBuf.transitions[0].oldLayout, EImageLayout::Undefined);
    EXPECT_EQ(cmdBuf.transitions[0].newLayout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_EQ(cmdBuf.transitions[1].oldLayout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_EQ(cmdBuf.transitions[1].newLayout, EImageLayout::ShaderReadOnlyOptimal);
    ASSERT_EQ(cmdBuf.bufferBarriers.size(), 2u);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].srcStage, EPipelineStage::None);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstStage, EPipelineStage::AllCommands);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].srcAccess, EResourceAccess::None);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstAccess, EResourceAccess::ShaderWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].size, 512u);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].srcAccess, EResourceAccess::ShaderWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].dstAccess, EResourceAccess::ShaderRead);
    EXPECT_EQ(factory.createdImages, 1u);
    EXPECT_EQ(factory.createdViews, 1u);
    EXPECT_EQ(factory.createdBuffers, 1u);
}

TEST(RenderGraphCoreTest, RenderContextReportsDeclaredTextureAndBufferUsage)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestBuffer          dstBacking(BufferCreateInfo{
        .label = "dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 64,
    });

    const auto declared = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "declared",
            .format = EFormat::R8_UNORM,
            .extent = Extent3D{32, 32, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        },
        .importDesc = ImportedImageDesc{
            .label        = "declared",
            .nativeHandle = reinterpret_cast<void*>(0x1001),
            .format       = EFormat::R8_UNORM,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{32, 32, 1},
        },
    });
    const auto undeclared = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "undeclared",
            .format = EFormat::R8_UNORM,
            .extent = Extent3D{32, 32, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        },
        .importDesc = ImportedImageDesc{
            .label        = "undeclared",
            .nativeHandle = reinterpret_cast<void*>(0x1002),
            .format       = EFormat::R8_UNORM,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{32, 32, 1},
        },
    });
    const auto dstBuffer = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "dst",
            .usage = EBufferUsage::TransferDst,
            .size  = 64,
        },
        .buffer = &dstBacking,
    });

    graph.addPass(
        "reader",
        [&](RGPassBuilder& pass) {
            pass.read(declared);
            pass.transferDst(dstBuffer);
        },
        [&](RGRenderContext& ctx) {
            EXPECT_TRUE(ctx.hasDeclaredTextureUsage(declared));
            EXPECT_FALSE(ctx.hasDeclaredTextureUsage(undeclared));
            EXPECT_TRUE(ctx.hasDeclaredTextureAccess(declared, ERGPassResourceAccess::Read));
            EXPECT_FALSE(ctx.hasDeclaredTextureAccess(declared, ERGPassResourceAccess::TransferSrc));
            EXPECT_TRUE(ctx.hasDeclaredBufferUsage(dstBuffer));
            EXPECT_TRUE(ctx.hasDeclaredBufferAccess(dstBuffer, ERGBufferAccess::TransferWrite));
            EXPECT_FALSE(ctx.hasDeclaredBufferAccess(dstBuffer, ERGBufferAccess::StorageWrite));
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
}

TEST(RenderGraphCoreTest, RenderContextReportsTransferAccessRequirements)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestBuffer          srcBacking(BufferCreateInfo{
        .label = "src",
        .usage = EBufferUsage::TransferSrc | EBufferUsage::StorageBuffer,
        .size  = 64,
    });
    TestBuffer          dstBacking(BufferCreateInfo{
        .label = "dst",
        .usage = EBufferUsage::TransferDst | EBufferUsage::StorageBuffer,
        .size  = 64,
    });

    const auto src = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "src",
            .usage = EBufferUsage::TransferSrc | EBufferUsage::StorageBuffer,
            .size  = 64,
        },
        .buffer = &srcBacking,
    });
    const auto dst = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "dst",
            .usage = EBufferUsage::TransferDst | EBufferUsage::StorageBuffer,
            .size  = 64,
        },
        .buffer = &dstBacking,
    });

    graph.addPass(
        "copy",
        [&](RGPassBuilder& pass) {
            pass.transferSrc(src);
            pass.transferDst(dst);
        },
        [&](RGRenderContext& ctx) {
            EXPECT_TRUE(ctx.hasDeclaredBufferAccess(src, ERGBufferAccess::TransferRead));
            EXPECT_TRUE(ctx.hasDeclaredBufferAccess(dst, ERGBufferAccess::TransferWrite));
            EXPECT_FALSE(ctx.hasDeclaredBufferAccess(src, ERGBufferAccess::StorageRead));
            ctx.copyBuffer(src, dst, 64);
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
}

TEST(RenderGraphCoreTest, ExecutorCanBeginDeclaredRasterRenderingFromCompiledPassPlan)
{
    TestResourceFactory factory;
    RenderGraphExecutor executor(factory);
    RenderGraph graph;

    const auto color = graph.createTexture(RGTextureDesc{
        .label  = "hdr",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{256, 128, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    const auto depth = graph.createTexture(RGTextureDesc{
        .label  = "depth",
        .format = EFormat::D32_SFLOAT,
        .extent = Extent3D{256, 128, 1},
        .usage  = EImageUsage::DepthStencilAttachment,
    });

    graph.addPass(
        "declared-raster",
        [&](RGPassBuilder& pass) {
            pass.declareRaster({
                .renderArea = Rect2D{.pos = {0, 0}, .extent = {256, 128}},
                .layerCount = 1,
                .colors = {{
                    .color       = color,
                    .clearValue  = ClearValue::Black(),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = depth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::DepthStencilAttachmentOptimal,
                },
            });
        },
        [](RGRenderContext& ctx) {
            ctx.beginDeclaredRasterRendering();
            ctx.endRendering();
        });

    TestCommandBuffer cmdBuf;
    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    EXPECT_EQ(cmdBuf.beginRenderingCount, 1u);
    EXPECT_EQ(cmdBuf.endRenderingCount, 1u);
    EXPECT_TRUE(cmdBuf.lastBeginRenderingHadDepth);
    EXPECT_EQ(cmdBuf.lastDepthFinalLayout, EImageLayout::DepthStencilAttachmentOptimal);
}

TEST(RenderGraphCoreTest, ExecutorExposesTypedRasterExecutionParamsFromCompiledPlan)
{
    TestResourceFactory factory;
    RenderGraphExecutor executor(factory);
    RenderGraph graph;

    const auto color = graph.createTexture(RGTextureDesc{
        .label  = "hdr",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{512, 256, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    const auto depth = graph.createTexture(RGTextureDesc{
        .label  = "depth",
        .format = EFormat::D32_SFLOAT,
        .extent = Extent3D{512, 256, 1},
        .usage  = EImageUsage::DepthStencilAttachment,
    });

    bool seenParams = false;

    graph.addPass(
        "typed-raster-params",
        [&](RGPassBuilder& pass) {
            pass.declareRaster({
                .renderArea = Rect2D{.pos = {12, 24}, .extent = {320, 200}},
                .layerCount = 2,
                .colors = {{
                    .color       = color,
                    .clearValue  = ClearValue(0.1f, 0.2f, 0.3f, 1.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = depth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::DepthStencilAttachmentOptimal,
                },
            });
        },
        [&](RGRenderContext& ctx) {
            const auto params = ctx.getRasterPassExecutionParams();
            EXPECT_EQ(params.getRenderExtent().width, 320u);
            EXPECT_EQ(params.getRenderExtent().height, 200u);
            EXPECT_EQ(params.rasterPlan.layerCount, 2u);
            EXPECT_EQ(params.getColorAttachment().color, color);
            EXPECT_EQ(params.getColorAttachment().finalLayout, EImageLayout::ShaderReadOnlyOptimal);
            EXPECT_EQ(params.getDepthAttachment().depth, depth);
            seenParams = true;
        });

    TestCommandBuffer cmdBuf;
    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    EXPECT_TRUE(seenParams);
}

TEST(RenderGraphCoreTest, ExecutorRejectsInvalidGraphWithoutRunningPasses)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    bool                executed = false;

    const auto texture = graph.createTexture(RGTextureDesc{
        .label  = "ao",
        .format = EFormat::R8_UNORM,
        .extent = Extent3D{640, 480, 1},
        .usage  = EImageUsage::Sampled | EImageUsage::ColorAttachment,
    });

    graph.addPass(
        "consumer",
        [&](RGPassBuilder& pass) {
            pass.read(texture);
        },
        [&](RGRenderContext&) {
            executed = true;
        });

    RGCompiledGraph compiled;
    EXPECT_FALSE(executor.execute(graph, cmdBuf, &compiled));
    EXPECT_FALSE(compiled.isValid());
    EXPECT_FALSE(executed);
    EXPECT_EQ(factory.createdImages, 0u);
    EXPECT_EQ(factory.createdBuffers, 0u);
}

TEST(RenderGraphCoreTest, ExecutorSeedsImportedBufferBarrierFromDeclaredInitialState)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestBuffer          uniformBuffer(BufferCreateInfo{
        .label = "frame.uniform",
        .usage = EBufferUsage::UniformBuffer,
        .size  = 256,
    });

    const auto imported = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "frame.uniform",
            .usage = EBufferUsage::UniformBuffer,
            .size  = 256,
        },
        .buffer = &uniformBuffer,
        .initialState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostWrite,
            .size   = 256,
        },
    });
    graph.addPass(
        "uniform-reader",
        [=](RGPassBuilder& pass) { pass.uniformRead(imported); },
        [](RGRenderContext&) {});

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_EQ(cmdBuf.bufferBarriers.size(), 1u);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].srcStage, EPipelineStage::Host);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstStage, EPipelineStage::AllCommands);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].srcAccess, EResourceAccess::HostWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstAccess, EResourceAccess::ShaderRead);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].size, 256u);
}

TEST(RenderGraphCoreTest, ExecutorTracksInitialStatesForNonOverlappingSharedBufferRanges)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestBuffer           uploadBuffer(BufferCreateInfo{
        .label = "shared.upload",
        .usage = EBufferUsage::UniformBuffer,
        .size  = 512,
    });

    const auto frameSlice = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "shared.upload.frame",
            .usage = EBufferUsage::UniformBuffer,
            .size  = 512,
        },
        .buffer = &uploadBuffer,
        .initialState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostWrite,
            .offset = 0,
            .size   = 128,
        },
    });
    const auto lightSlice = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "shared.upload.light",
            .usage = EBufferUsage::UniformBuffer,
            .size  = 512,
        },
        .buffer = &uploadBuffer,
        .initialState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostWrite,
            .offset = 256,
            .size   = 128,
        },
    });

    graph.addPass(
        "shared-upload-reader",
        [=](RGPassBuilder& pass) {
            pass.uniformRead(frameSlice, RGBufferRange{.offset = 0, .size = 128});
            pass.uniformRead(lightSlice, RGBufferRange{.offset = 256, .size = 128});
        },
        [](RGRenderContext&) {});

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_EQ(cmdBuf.bufferBarriers.size(), 2u);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].srcAccess, EResourceAccess::HostWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].offset, 0u);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].size, 128u);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].srcAccess, EResourceAccess::HostWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].offset, 256u);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].size, 128u);
}

TEST(RenderGraphCoreTest, ExecutorRetainsImportedBufferKeepAliveResources)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestBuffer          storageBuffer(BufferCreateInfo{
        .label = "point.visible",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 512,
    });
    auto owner = std::make_shared<int>(42);

    const auto imported = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "point.visible",
            .usage = EBufferUsage::StorageBuffer,
            .size  = 512,
        },
        .buffer = &storageBuffer,
        .initialState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostWrite,
            .size   = 512,
        },
        .retainedResources = {owner},
    });
    graph.addPass(
        "storage-reader",
        [=](RGPassBuilder& pass) { pass.storageRead(imported); },
        [](RGRenderContext&) {});

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_EQ(cmdBuf.retainedResources.size(), 1u);
    EXPECT_EQ(cmdBuf.retainedResources[0].get(), owner.get());
}

TEST(RenderGraphCoreTest, ExecutorRestoresImportedBufferFinalStateAfterTransferPass)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestBuffer          readbackBuffer(BufferCreateInfo{
        .label = "readback.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 512,
    });

    const auto imported = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "readback.dst",
            .usage = EBufferUsage::TransferDst,
            .size  = 512,
        },
        .buffer = &readbackBuffer,
        .finalState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostRead,
            .size   = 512,
        },
    });
    graph.addPass(
        "copy-readback",
        [=](RGPassBuilder& pass) { pass.transferDst(imported); },
        [](RGRenderContext&) {});

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_EQ(cmdBuf.bufferBarriers.size(), 2u);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstStage, EPipelineStage::Transfer);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstAccess, EResourceAccess::TransferWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].srcStage, EPipelineStage::Transfer);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].dstStage, EPipelineStage::Host);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].srcAccess, EResourceAccess::TransferWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].dstAccess, EResourceAccess::HostRead);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].size, 512u);
}

TEST(RenderGraphCoreTest, ExecuteCompiledRestoresImportedBufferFinalStateAfterTransferPass)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestBuffer          readbackBuffer(BufferCreateInfo{
        .label = "readback.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 512,
    });

    const auto imported = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "readback.dst",
            .usage = EBufferUsage::TransferDst,
            .size  = 512,
        },
        .buffer = &readbackBuffer,
        .finalState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostRead,
            .size   = 512,
        },
    });
    graph.addPass(
        "copy-readback",
        [=](RGPassBuilder& pass) { pass.transferDst(imported); },
        [](RGRenderContext&) {});

    RGCompiledGraph compiled;
    ASSERT_TRUE(executor.prepare(graph, compiled));
    ASSERT_TRUE(compiled.isValid());
    ASSERT_TRUE(executor.executeCompiled(graph, compiled, cmdBuf));
    ASSERT_EQ(cmdBuf.bufferBarriers.size(), 2u);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstStage, EPipelineStage::Transfer);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstAccess, EResourceAccess::TransferWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].srcStage, EPipelineStage::Transfer);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].dstStage, EPipelineStage::Host);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].srcAccess, EResourceAccess::TransferWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].dstAccess, EResourceAccess::HostRead);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].size, 512u);
}

TEST(RenderGraphCoreTest, ResolveTextureRetainsImportedTextureKeepAliveResources)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "post.input",
        .format      = EFormat::R16G16B16A16_SFLOAT,
        .extent      = {.width = 128, .height = 128, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .usage       = EImageUsage::Sampled | EImageUsage::TransferSrc,
    });
    auto existingView = factory.createImageView(existingImage, ImageViewCreateInfo{
        .label       = "post.input.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });
    auto owner = std::make_shared<int>(77);

    const auto imported = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "post.input",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{128, 128, 1},
            .usage  = EImageUsage::Sampled | EImageUsage::TransferSrc,
        },
        .importDesc = ImportedImageDesc{
            .label        = "post.input",
            .nativeHandle = static_cast<void*>(existingImage->getHandle()),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::Sampled | EImageUsage::TransferSrc,
            .extent       = Extent3D{128, 128, 1},
        },
        .image = existingImage,
        .imageView = existingView,
        .retainedResources = {owner},
    });
    graph.addPass(
        "sample-reader",
        [=](RGPassBuilder& pass) { pass.read(imported); },
        [=](RGRenderContext& ctx) {
            const auto* resolved = ctx.resolveTexture(imported);
            ASSERT_NE(resolved, nullptr);
            ASSERT_EQ(resolved->getImage(), existingImage.get());
            ASSERT_EQ(resolved->getImageView(), existingView.get());
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_EQ(cmdBuf.retainedResources.size(), 3u);
    EXPECT_EQ(cmdBuf.retainedResources[0].get(), existingImage.get());
    EXPECT_EQ(cmdBuf.retainedResources[1].get(), existingView.get());
    EXPECT_EQ(cmdBuf.retainedResources[2].get(), owner.get());
}

TEST(RenderGraphCoreTest, PassBindingContextResolvesTextureDescriptorAndRetainsOwners)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestSampler         sampler;

    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "binding.input",
        .format      = EFormat::R16G16B16A16_SFLOAT,
        .extent      = {.width = 128, .height = 128, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .usage       = EImageUsage::Sampled,
    });
    auto existingView = factory.createImageView(existingImage, ImageViewCreateInfo{
        .label       = "binding.input.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });
    auto owner = std::make_shared<int>(99);

    const auto imported = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "binding.input",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{128, 128, 1},
            .usage  = EImageUsage::Sampled,
        },
        .importDesc = ImportedImageDesc{
            .label        = "binding.input",
            .nativeHandle = static_cast<void*>(existingImage->getHandle()),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::Sampled,
            .extent       = Extent3D{128, 128, 1},
        },
        .image             = existingImage,
        .imageView         = existingView,
        .retainedResources = {owner},
    });

    std::optional<DescriptorImageInfo> resolved;
    graph.addPass(
        "binding-reader",
        [=](RGPassBuilder& pass) { pass.read(imported); },
        [&](RGRenderContext& ctx) {
            resolved = ctx.getBindingContext().resolveTextureDescriptor(imported, &sampler);
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->imageView, existingView->getHandle());
    EXPECT_EQ(resolved->sampler, sampler.getHandle());
    EXPECT_EQ(resolved->imageLayout, EImageLayout::ShaderReadOnlyOptimal);

    ASSERT_EQ(cmdBuf.retainedResources.size(), 3u);
    EXPECT_EQ(cmdBuf.retainedResources[0].get(), existingImage.get());
    EXPECT_EQ(cmdBuf.retainedResources[1].get(), existingView.get());
    EXPECT_EQ(cmdBuf.retainedResources[2].get(), owner.get());
}

TEST(RenderGraphCoreTest, PassBindingContextResolvesBufferDescriptorWithDeclaredRange)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;
    TestBuffer          backing(BufferCreateInfo{
        .label = "binding.ubo",
        .usage = EBufferUsage::UniformBuffer,
        .size  = 256,
    });
    auto owner = std::make_shared<int>(123);

    const auto imported = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "binding.ubo",
            .usage = EBufferUsage::UniformBuffer,
            .size  = 256,
        },
        .buffer            = &backing,
        .retainedResources = {owner},
    });

    std::optional<DescriptorBufferInfo> resolved;
    graph.addPass(
        "binding-ubo",
        [=](RGPassBuilder& pass) { pass.uniformRead(imported, RGBufferRange{.offset = 64, .size = 128}); },
        [&](RGRenderContext& ctx) {
            resolved = ctx.getBindingContext().resolveBufferDescriptor(imported);
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->buffer, backing.getHandle());
    EXPECT_EQ(resolved->offset, 64u);
    EXPECT_EQ(resolved->range, 128u);
    ASSERT_EQ(cmdBuf.retainedResources.size(), 1u);
    EXPECT_EQ(cmdBuf.retainedResources[0].get(), owner.get());
}

TEST(RenderGraphCoreTest, ResourceRegistryRefreshesImportedBufferKeepAliveWithoutReplacingBuffer)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    TestBuffer importedBacking(BufferCreateInfo{
        .label = "external.visible",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 512,
    });

    RGBufferHandle handle{};
    std::weak_ptr<int> ownerAWeak;
    {
        RenderGraph graphA;
        auto ownerA = std::make_shared<int>(1);
        ownerAWeak = ownerA;
        handle = graphA.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = "external.visible",
                .usage = EBufferUsage::StorageBuffer,
                .size  = 512,
            },
            .buffer = &importedBacking,
            .retainedResources = {ownerA},
        });
        registry.sync(graphA);
        ownerA.reset();
    }

    EXPECT_FALSE(ownerAWeak.expired());
    EXPECT_EQ(registry.resolveBuffer(handle), &importedBacking);
    EXPECT_EQ(factory.createdBuffers, 0u);

    std::weak_ptr<int> ownerBWeak;
    {
        RenderGraph graphB;
        auto ownerB = std::make_shared<int>(2);
        ownerBWeak = ownerB;
        graphB.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = "external.visible",
                .usage = EBufferUsage::StorageBuffer,
                .size  = 512,
            },
            .buffer = &importedBacking,
            .retainedResources = {ownerB},
        });
        registry.sync(graphB);
        ownerB.reset();
    }

    EXPECT_FALSE(ownerAWeak.expired());
    EXPECT_FALSE(ownerBWeak.expired());
    EXPECT_EQ(registry.resolveBuffer(handle), &importedBacking);
    EXPECT_EQ(factory.createdBuffers, 0u);

    registry.clear();
    EXPECT_FALSE(ownerAWeak.expired());
    EXPECT_FALSE(ownerBWeak.expired());
    deletionQueue.flushAll();
    EXPECT_TRUE(ownerAWeak.expired());
    EXPECT_TRUE(ownerBWeak.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryDoesNotRetireImportedBufferKeepAliveWhenOwnerIsUnchanged)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    TestBuffer importedBacking(BufferCreateInfo{
        .label = "external.stable.visible",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 512,
    });

    auto owner = std::make_shared<int>(5);
    std::weak_ptr<int> ownerWeak = owner;

    {
        RenderGraph graphA;
        graphA.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = "external.stable.visible",
                .usage = EBufferUsage::StorageBuffer,
                .size  = 512,
            },
            .buffer = &importedBacking,
            .retainedResources = {owner},
        });
        registry.sync(graphA);
        EXPECT_EQ(deletionQueue.pendingCount(), 0u);

        RenderGraph graphB;
        graphB.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = "external.stable.visible",
                .usage = EBufferUsage::StorageBuffer,
                .size  = 512,
            },
            .buffer = &importedBacking,
            .retainedResources = {owner},
        });
        registry.sync(graphB);
        EXPECT_EQ(deletionQueue.pendingCount(), 0u);
    }

    owner.reset();
    EXPECT_FALSE(ownerWeak.expired());

    registry.clear();
    EXPECT_EQ(deletionQueue.pendingCount(), 1u);
    deletionQueue.flushAll();
    EXPECT_TRUE(ownerWeak.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryPrunesImportedBufferKeepAliveWhenBufferIsRemoved)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    TestBuffer importedBacking(BufferCreateInfo{
        .label = "external.cull.instances",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    });

    std::weak_ptr<int> ownerWeak;
    {
        RenderGraph graph;
        auto owner = std::make_shared<int>(7);
        ownerWeak = owner;
        graph.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = "external.cull.instances",
                .usage = EBufferUsage::StorageBuffer,
                .size  = 256,
            },
            .buffer = &importedBacking,
            .retainedResources = {owner},
        });
        registry.sync(graph);
        owner.reset();
    }

    EXPECT_FALSE(ownerWeak.expired());

    RenderGraph emptyGraph;
    registry.sync(emptyGraph);
    if (deletionQueue.isInitialized()) {
        EXPECT_FALSE(ownerWeak.expired());
        deletionQueue.flushAll();
    }
    EXPECT_TRUE(ownerWeak.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryPruneDefersImportedBufferKeepAliveReleaseThroughDeletionQueue)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    TestBuffer importedBacking(BufferCreateInfo{
        .label = "external.async.instances",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    });

    std::weak_ptr<int> ownerWeak;
    {
        RenderGraph graph;
        auto owner = std::make_shared<int>(11);
        ownerWeak = owner;
        graph.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = "external.async.instances",
                .usage = EBufferUsage::StorageBuffer,
                .size  = 256,
            },
            .buffer = &importedBacking,
            .retainedResources = {owner},
        });
        registry.sync(graph);
        owner.reset();
    }

    EXPECT_FALSE(ownerWeak.expired());

    RenderGraph emptyGraph;
    registry.sync(emptyGraph);
    EXPECT_FALSE(ownerWeak.expired());

    deletionQueue.flushAll();
    EXPECT_TRUE(ownerWeak.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryDoesNotRetireImportedTextureKeepAliveWhenOwnerIsUnchanged)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    auto existingImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label       = "existing.stable.color",
        .format      = EFormat::R8G8B8A8_UNORM,
        .extent      = {.width = 128, .height = 128, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    auto existingView = factory.createImageView(existingImage, ImageViewCreateInfo{
        .label       = "existing.stable.color.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });

    auto owner = std::make_shared<int>(9);
    std::weak_ptr<int> ownerWeak = owner;

    {
        RenderGraph graphA;
        graphA.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label  = "existing.stable.color.import",
                .format = EFormat::R8G8B8A8_UNORM,
                .extent = Extent3D{128, 128, 1},
                .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            },
            .importDesc = ImportedImageDesc{
                .label        = "existing.stable.color.import",
                .nativeHandle = static_cast<void*>(existingImage->getHandle()),
                .format       = EFormat::R8G8B8A8_UNORM,
                .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .extent       = Extent3D{128, 128, 1},
            },
            .image = existingImage,
            .imageView = existingView,
            .retainedResources = {owner},
        });
        registry.sync(graphA);
        EXPECT_EQ(deletionQueue.pendingCount(), 0u);

        RenderGraph graphB;
        graphB.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label  = "existing.stable.color.import",
                .format = EFormat::R8G8B8A8_UNORM,
                .extent = Extent3D{128, 128, 1},
                .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            },
            .importDesc = ImportedImageDesc{
                .label        = "existing.stable.color.import",
                .nativeHandle = static_cast<void*>(existingImage->getHandle()),
                .format       = EFormat::R8G8B8A8_UNORM,
                .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .extent       = Extent3D{128, 128, 1},
            },
            .image = existingImage,
            .imageView = existingView,
            .retainedResources = {owner},
        });
        registry.sync(graphB);
        EXPECT_EQ(deletionQueue.pendingCount(), 0u);
    }

    owner.reset();
    EXPECT_FALSE(ownerWeak.expired());

    registry.clear();
    EXPECT_EQ(deletionQueue.pendingCount(), 1u);
    deletionQueue.flushAll();
    EXPECT_TRUE(ownerWeak.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryClearDefersImportedBufferKeepAliveReleaseThroughDeletionQueue)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);
    TestBuffer importedBacking(BufferCreateInfo{
        .label = "external.deferred.visible",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 512,
    });

    std::weak_ptr<int> ownerWeak;
    {
        RenderGraph graph;
        auto owner = std::make_shared<int>(29);
        ownerWeak = owner;
        graph.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = "external.deferred.visible",
                .usage = EBufferUsage::StorageBuffer,
                .size  = 512,
            },
            .buffer = &importedBacking,
            .retainedResources = {owner},
        });
        registry.sync(graph);
        owner.reset();
    }

    EXPECT_FALSE(ownerWeak.expired());

    registry.clear();
    EXPECT_FALSE(ownerWeak.expired());

    deletionQueue.flushAll();
    EXPECT_TRUE(ownerWeak.expired());
}

TEST(RenderGraphCoreTest, ExecutorSmokeRunsClearAndCopyCallbacks)
{
    TestResourceFactory      factory;
    TestCommandBuffer        cmdBuf;
    RenderGraphExecutor      executor(factory);
    RenderGraph              graph;
    std::vector<std::string> executionOrder;

    const auto colorTarget = graph.createTexture(RGTextureDesc{
        .label  = "postprocess.output",
        .format = EFormat::R8G8B8A8_UNORM,
        .extent = Extent3D{256, 256, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    TestBuffer importedSrc(BufferCreateInfo{
        .label = "upload.src",
        .usage = EBufferUsage::TransferSrc,
        .size  = 256,
    });
    const auto srcBuffer = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "upload.src",
            .usage = EBufferUsage::TransferSrc,
            .size  = 256,
        },
        .buffer = &importedSrc,
    });
    const auto dstBuffer = graph.createBuffer(RGBufferDesc{
        .label = "gpu.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 256,
    });

    graph.addPass(
        "clear",
        [&](RGPassBuilder& pass) {
            pass.useColorAttachment(colorTarget);
        },
        [&](RGRenderContext& ctx) {
            executionOrder.push_back(ctx.getPass().name);
            ctx.beginColorRendering({
                .color = colorTarget,
                .renderArea = Rect2D{
                    .offset = glm::vec2{0.0f, 0.0f},
                    .extent = glm::vec2{256.0f, 256.0f},
                },
                .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            });
            ctx.endRendering();
        });

    graph.addPass(
        "copy",
        [&](RGPassBuilder& pass) {
            pass.transferSrc(srcBuffer);
            pass.transferDst(dstBuffer);
        },
        [&](RGRenderContext& ctx) {
            executionOrder.push_back(ctx.getPass().name);
            ctx.copyBuffer(srcBuffer, dstBuffer, 256);
        });

    RGCompiledGraph compiled;
    ASSERT_TRUE(executor.execute(graph, cmdBuf, &compiled));
    ASSERT_TRUE(compiled.isValid());
    EXPECT_EQ(executionOrder, (std::vector<std::string>{"clear", "copy"}));
    EXPECT_EQ(cmdBuf.beginRenderingCount, 1u);
    EXPECT_EQ(cmdBuf.endRenderingCount, 1u);
    ASSERT_EQ(cmdBuf.copyBuffers.size(), 1u);
    EXPECT_EQ(cmdBuf.copyBuffers[0].size, 256u);
    ASSERT_EQ(cmdBuf.transitions.size(), 1u);
    EXPECT_EQ(cmdBuf.transitions[0].newLayout, EImageLayout::ColorAttachmentOptimal);
    ASSERT_EQ(cmdBuf.bufferBarriers.size(), 2u);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstAccess, EResourceAccess::TransferRead);
    EXPECT_EQ(cmdBuf.bufferBarriers[1].dstAccess, EResourceAccess::TransferWrite);
}

TEST(RenderGraphCoreTest, ExecutorRestoresImportedTextureFinalLayoutAfterTransferPass)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    auto sharedImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label         = "swapchain",
        .format        = EFormat::B8G8R8A8_UNORM,
        .extent        = {.width = 1280, .height = 720, .depth = 1},
        .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
        .initialLayout = EImageLayout::Undefined,
    });
    auto sharedView = std::make_shared<TestImageView>(sharedImage, ImageViewCreateInfo{
        .label       = "swapchain.view",
        .aspectFlags = EImageAspect::Color,
        .levelCount  = 1,
        .layerCount  = 1,
    });

    const auto imported = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "swapchain",
            .format = EFormat::B8G8R8A8_UNORM,
            .extent = Extent3D{1280, 720, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
        },
        .importDesc = ImportedImageDesc{
            .label         = "swapchain",
            .nativeHandle  = static_cast<void*>(sharedImage->getHandle()),
            .format        = EFormat::B8G8R8A8_UNORM,
            .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
            .extent        = Extent3D{1280, 720, 1},
            .initialLayout = EImageLayout::Undefined,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        },
        .image     = sharedImage,
        .imageView = sharedView,
    });

    graph.addPass(
        "copy-to-swapchain",
        [&](RGPassBuilder& pass) {
            pass.transferDst(imported);
        },
        [](RGRenderContext&) {});

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_EQ(cmdBuf.transitions.size(), 2u);
    EXPECT_EQ(cmdBuf.transitions[0].newLayout, EImageLayout::TransferDst);
    EXPECT_EQ(cmdBuf.transitions[1].oldLayout, EImageLayout::TransferDst);
    EXPECT_EQ(cmdBuf.transitions[1].newLayout, EImageLayout::PresentSrcKHR);
}

TEST(RenderGraphCoreTest, DebugDumpIncludesImportedFinalizePlans)
{
    RenderGraph graph;

    auto sharedImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label         = "swapchain",
        .format        = EFormat::B8G8R8A8_UNORM,
        .extent        = {.width = 1280, .height = 720, .depth = 1},
        .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
        .initialLayout = EImageLayout::Undefined,
    });
    auto sharedView = std::make_shared<TestImageView>(sharedImage, ImageViewCreateInfo{
        .label       = "swapchain.view",
        .aspectFlags = EImageAspect::Color,
        .levelCount  = 1,
        .layerCount  = 1,
    });
    TestBuffer readbackBuffer(BufferCreateInfo{
        .label = "readback.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 512,
    });

    graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "swapchain",
            .format = EFormat::B8G8R8A8_UNORM,
            .extent = Extent3D{1280, 720, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
        },
        .importDesc = ImportedImageDesc{
            .label         = "swapchain",
            .nativeHandle  = static_cast<void*>(sharedImage->getHandle()),
            .format        = EFormat::B8G8R8A8_UNORM,
            .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
            .extent        = Extent3D{1280, 720, 1},
            .initialLayout = EImageLayout::Undefined,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        },
        .image     = sharedImage,
        .imageView = sharedView,
    });
    graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "readback.dst",
            .usage = EBufferUsage::TransferDst,
            .size  = 512,
        },
        .buffer = &readbackBuffer,
        .finalState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostRead,
            .size   = 512,
        },
    });

    const auto dump = graph.debugDump(graph.compile());
    EXPECT_NE(dump.find("importedTextureFinalizes(1)"), std::string::npos);
    EXPECT_NE(dump.find("swapchain layout="), std::string::npos);
    EXPECT_NE(dump.find("importedBufferFinalizes(1)"), std::string::npos);
    EXPECT_NE(dump.find("readback.dst access="), std::string::npos);
}

TEST(RenderGraphCoreTest, ExecuteCompiledRestoresImportedTextureFinalLayoutAfterTransferPass)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    auto sharedImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label         = "swapchain",
        .format        = EFormat::B8G8R8A8_UNORM,
        .extent        = {.width = 1280, .height = 720, .depth = 1},
        .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
        .initialLayout = EImageLayout::Undefined,
    });
    auto sharedView = std::make_shared<TestImageView>(sharedImage, ImageViewCreateInfo{
        .label       = "swapchain.view",
        .aspectFlags = EImageAspect::Color,
        .levelCount  = 1,
        .layerCount  = 1,
    });

    const auto imported = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "swapchain",
            .format = EFormat::B8G8R8A8_UNORM,
            .extent = Extent3D{1280, 720, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
        },
        .importDesc = ImportedImageDesc{
            .label         = "swapchain",
            .nativeHandle  = static_cast<void*>(sharedImage->getHandle()),
            .format        = EFormat::B8G8R8A8_UNORM,
            .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
            .extent        = Extent3D{1280, 720, 1},
            .initialLayout = EImageLayout::Undefined,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        },
        .image     = sharedImage,
        .imageView = sharedView,
    });

    graph.addPass(
        "copy-to-swapchain",
        [&](RGPassBuilder& pass) {
            pass.transferDst(imported);
        },
        [](RGRenderContext&) {});

    RGCompiledGraph compiled;
    ASSERT_TRUE(executor.prepare(graph, compiled));
    ASSERT_TRUE(compiled.isValid());
    ASSERT_TRUE(executor.executeCompiled(graph, compiled, cmdBuf));
    ASSERT_EQ(cmdBuf.transitions.size(), 2u);
    EXPECT_EQ(cmdBuf.transitions[0].newLayout, EImageLayout::TransferDst);
    EXPECT_EQ(cmdBuf.transitions[1].oldLayout, EImageLayout::TransferDst);
    EXPECT_EQ(cmdBuf.transitions[1].newLayout, EImageLayout::PresentSrcKHR);
}

TEST(RenderGraphCoreTest, ExecuteWrapperMatchesPrepareAndExecuteCompiledForImportedFinalizeContract)
{
    auto buildGraph = [](TestResourceFactory& factory, TestBuffer& readbackBuffer) {
        RenderGraph graph;

        auto sharedImage = std::make_shared<TestImage>(ImageCreateInfo{
            .label         = "swapchain",
            .format        = EFormat::B8G8R8A8_UNORM,
            .extent        = {.width = 1280, .height = 720, .depth = 1},
            .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
            .initialLayout = EImageLayout::Undefined,
        });
        auto sharedView = std::make_shared<TestImageView>(sharedImage, ImageViewCreateInfo{
            .label       = "swapchain.view",
            .aspectFlags = EImageAspect::Color,
            .levelCount  = 1,
            .layerCount  = 1,
        });

        const auto importedTexture = graph.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label  = "swapchain",
                .format = EFormat::B8G8R8A8_UNORM,
                .extent = Extent3D{1280, 720, 1},
                .usage  = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
            },
            .importDesc = ImportedImageDesc{
                .label         = "swapchain",
                .nativeHandle  = static_cast<void*>(sharedImage->getHandle()),
                .format        = EFormat::B8G8R8A8_UNORM,
                .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferDst,
                .extent        = Extent3D{1280, 720, 1},
                .initialLayout = EImageLayout::Undefined,
                .finalLayout   = EImageLayout::PresentSrcKHR,
            },
            .image     = sharedImage,
            .imageView = sharedView,
        });
        const auto importedBuffer = graph.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = "readback.dst",
                .usage = EBufferUsage::TransferDst,
                .size  = 512,
            },
            .buffer = &readbackBuffer,
            .finalState = BufferResourceState{
                .stages = EPipelineStage::Host,
                .access = EResourceAccess::HostRead,
                .size   = 512,
            },
        });

        graph.addPass(
            "copy-to-imported",
            [&](RGPassBuilder& pass) {
                pass.transferDst(importedTexture);
                pass.transferDst(importedBuffer);
            },
            [](RGRenderContext&) {});

        return graph;
    };

    TestResourceFactory factoryA;
    TestBuffer          readbackA(BufferCreateInfo{
        .label = "readback.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 512,
    });
    RenderGraphExecutor executeWrapperExecutor(factoryA);
    TestCommandBuffer   executeWrapperCmdBuf;
    auto                graphA = buildGraph(factoryA, readbackA);

    ASSERT_TRUE(executeWrapperExecutor.execute(graphA, executeWrapperCmdBuf));

    TestResourceFactory factoryB;
    TestBuffer          readbackB(BufferCreateInfo{
        .label = "readback.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 512,
    });
    RenderGraphExecutor compiledExecutor(factoryB);
    TestCommandBuffer   compiledCmdBuf;
    auto                graphB = buildGraph(factoryB, readbackB);
    RGCompiledGraph     compiled{};

    ASSERT_TRUE(compiledExecutor.prepare(graphB, compiled));
    ASSERT_TRUE(compiled.isValid());
    ASSERT_TRUE(compiledExecutor.executeCompiled(graphB, compiled, compiledCmdBuf));

    ASSERT_EQ(executeWrapperCmdBuf.transitions.size(), compiledCmdBuf.transitions.size());
    for (size_t i = 0; i < executeWrapperCmdBuf.transitions.size(); ++i) {
        EXPECT_EQ(executeWrapperCmdBuf.transitions[i].oldLayout, compiledCmdBuf.transitions[i].oldLayout);
        EXPECT_EQ(executeWrapperCmdBuf.transitions[i].newLayout, compiledCmdBuf.transitions[i].newLayout);
    }

    ASSERT_EQ(executeWrapperCmdBuf.bufferBarriers.size(), compiledCmdBuf.bufferBarriers.size());
    for (size_t i = 0; i < executeWrapperCmdBuf.bufferBarriers.size(); ++i) {
        EXPECT_EQ(executeWrapperCmdBuf.bufferBarriers[i].srcStage, compiledCmdBuf.bufferBarriers[i].srcStage);
        EXPECT_EQ(executeWrapperCmdBuf.bufferBarriers[i].dstStage, compiledCmdBuf.bufferBarriers[i].dstStage);
        EXPECT_EQ(executeWrapperCmdBuf.bufferBarriers[i].srcAccess, compiledCmdBuf.bufferBarriers[i].srcAccess);
        EXPECT_EQ(executeWrapperCmdBuf.bufferBarriers[i].dstAccess, compiledCmdBuf.bufferBarriers[i].dstAccess);
        EXPECT_EQ(executeWrapperCmdBuf.bufferBarriers[i].offset, compiledCmdBuf.bufferBarriers[i].offset);
        EXPECT_EQ(executeWrapperCmdBuf.bufferBarriers[i].size, compiledCmdBuf.bufferBarriers[i].size);
    }
}

TEST(RenderGraphCoreTest, RenderContextCanCopyTextureToBuffer)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    auto sharedImage = std::make_shared<TestImage>(ImageCreateInfo{
        .label         = "readback.src",
        .format        = EFormat::R8G8B8A8_UNORM,
        .extent        = {.width = 128, .height = 64, .depth = 1},
        .usage         = static_cast<EImageUsage::T>(EImageUsage::TransferSrc | EImageUsage::Sampled),
        .initialLayout = EImageLayout::ShaderReadOnlyOptimal,
    });
    auto sharedView = std::make_shared<TestImageView>(sharedImage, ImageViewCreateInfo{
        .label       = "readback.src.view",
        .aspectFlags = EImageAspect::Color,
        .levelCount  = 1,
        .layerCount  = 1,
    });
    const auto srcTexture = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label  = "readback.src",
            .format = EFormat::R8G8B8A8_UNORM,
            .extent = Extent3D{128, 64, 1},
            .usage  = static_cast<EImageUsage::T>(EImageUsage::TransferSrc | EImageUsage::Sampled),
        },
        .importDesc = ImportedImageDesc{
            .label         = "readback.src",
            .nativeHandle  = static_cast<void*>(sharedImage->getHandle()),
            .format        = EFormat::R8G8B8A8_UNORM,
            .usage         = static_cast<EImageUsage::T>(EImageUsage::TransferSrc | EImageUsage::Sampled),
            .extent        = Extent3D{128, 64, 1},
            .initialLayout = EImageLayout::ShaderReadOnlyOptimal,
            .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
        },
        .image     = sharedImage,
        .imageView = sharedView,
    });

    TestBuffer importedDst(BufferCreateInfo{
        .label = "readback.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 128u * 64u * 4u,
    });
    const auto dstBuffer = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "readback.dst",
            .usage = EBufferUsage::TransferDst,
            .size  = 128u * 64u * 4u,
        },
        .buffer = &importedDst,
    });

    graph.addPass(
        "readback-copy",
        [&](RGPassBuilder& pass) {
            pass.transferSrc(srcTexture);
            pass.transferDst(dstBuffer);
        },
        [&](RGRenderContext& ctx) {
            ctx.copyTextureToBuffer(srcTexture,
                                    dstBuffer,
                                    {BufferImageCopy{
                                        .bufferOffset      = 0,
                                        .bufferRowLength   = 0,
                                        .bufferImageHeight = 0,
                                        .imageSubresource  = {
                                            .aspectMask     = EImageAspect::Color,
                                            .mipLevel       = 0,
                                            .baseArrayLayer = 0,
                                            .layerCount     = 1,
                                        },
                                        .imageOffsetX      = 0,
                                        .imageOffsetY      = 0,
                                        .imageOffsetZ      = 0,
                                        .imageExtentWidth  = 128,
                                        .imageExtentHeight = 64,
                                        .imageExtentDepth  = 1,
                                    }});
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_EQ(cmdBuf.copyImageToBuffers.size(), 1u);
    EXPECT_EQ(cmdBuf.copyImageToBuffers.front().srcLayout, EImageLayout::TransferSrc);
    ASSERT_EQ(cmdBuf.copyImageToBuffers.front().regions.size(), 1u);
    EXPECT_EQ(cmdBuf.copyImageToBuffers.front().regions.front().imageExtentWidth, 128u);
    EXPECT_EQ(cmdBuf.copyImageToBuffers.front().regions.front().imageExtentHeight, 64u);
}

TEST(RenderGraphCoreTest, RenderContextHelpersDriveRenderingAndCopyCommands)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    const auto colorTarget = graph.createTexture(RGTextureDesc{
        .label  = "helper.target",
        .format = EFormat::R8G8B8A8_UNORM,
        .extent = Extent3D{64, 64, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    TestBuffer importedSrc(BufferCreateInfo{
        .label = "helper.src",
        .usage = EBufferUsage::TransferSrc,
        .size  = 32,
    });
    const auto srcBuffer = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "helper.src",
            .usage = EBufferUsage::TransferSrc,
            .size  = 32,
        },
        .buffer = &importedSrc,
    });
    const auto dstBuffer = graph.createBuffer(RGBufferDesc{
        .label = "helper.dst",
        .usage = EBufferUsage::TransferDst,
        .size  = 32,
    });

    graph.addPass(
        "helper-pass",
        [&](RGPassBuilder& pass) {
            pass.useColorAttachment(colorTarget);
            pass.transferSrc(srcBuffer);
            pass.transferDst(dstBuffer);
        },
        [&](RGRenderContext& ctx) {
            ctx.beginColorRendering({
                .color = colorTarget,
                .renderArea = Rect2D{
                    .offset = glm::vec2{0.0f, 0.0f},
                    .extent = glm::vec2{64.0f, 64.0f},
                },
            });
            ctx.endRendering();
            ctx.copyBuffer(srcBuffer, dstBuffer, 32, 4, 8);
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    EXPECT_EQ(cmdBuf.beginRenderingCount, 1u);
    EXPECT_EQ(cmdBuf.endRenderingCount, 1u);
    ASSERT_EQ(cmdBuf.copyBuffers.size(), 1u);
    EXPECT_EQ(cmdBuf.copyBuffers[0].size, 32u);
    EXPECT_EQ(cmdBuf.copyBuffers[0].srcOffset, 4u);
    EXPECT_EQ(cmdBuf.copyBuffers[0].dstOffset, 8u);
}

TEST(RenderGraphCoreTest, RasterRenderingHelperSupportsOptionalDepthAttachment)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    const auto colorTarget = graph.createTexture(RGTextureDesc{
        .label  = "helper.color",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{128, 128, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    const auto depthTarget = graph.createTexture(RGTextureDesc{
        .label  = "helper.depth",
        .format = EFormat::D32_SFLOAT,
        .extent = Extent3D{128, 128, 1},
        .usage  = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
    });

    graph.addPass(
        "helper-raster-pass",
        [&](RGPassBuilder& pass) {
            pass.useColorAttachment(colorTarget);
            pass.useDepthAttachment(depthTarget);
        },
        [&](RGRenderContext& ctx) {
            ctx.beginRasterRendering({
                .renderArea = Rect2D{
                    .offset = glm::vec2{0.0f, 0.0f},
                    .extent = glm::vec2{128.0f, 128.0f},
                },
                .colors = {{
                    .color = colorTarget,
                }},
                .depth = RGRenderContext::DepthRenderingDesc{
                    .depth = depthTarget,
                    .loadOp = EAttachmentLoadOp::Load,
                    .storeOp = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
            ctx.endRendering();
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    EXPECT_EQ(cmdBuf.beginRenderingCount, 1u);
    EXPECT_EQ(cmdBuf.endRenderingCount, 1u);
    EXPECT_TRUE(cmdBuf.lastBeginRenderingHadDepth);
    EXPECT_EQ(cmdBuf.lastDepthFinalLayout, EImageLayout::ShaderReadOnlyOptimal);
}

TEST(RenderGraphCoreTest, RasterRenderingHelperSupportsResolveAttachment)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    const auto colorTarget = graph.createTexture(RGTextureDesc{
        .label   = "helper.msaa.color",
        .format  = EFormat::R16G16B16A16_SFLOAT,
        .extent  = Extent3D{128, 128, 1},
        .samples = ESampleCount::Sample_4,
        .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    const auto resolveTarget = graph.createTexture(RGTextureDesc{
        .label  = "helper.resolve",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{128, 128, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    graph.addPass(
        "helper-resolve-pass",
        [&](RGPassBuilder& pass) {
            pass.useColorAttachment(colorTarget);
            pass.useColorAttachment(resolveTarget);
        },
        [&](RGRenderContext& ctx) {
            ctx.beginRasterRendering({
                .renderArea = Rect2D{
                    .offset = glm::vec2{0.0f, 0.0f},
                    .extent = glm::vec2{128.0f, 128.0f},
                },
                .colors = {{
                    .color       = colorTarget,
                    .resolve     = resolveTarget,
                    .resolveMode = EResolveMode::Average,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
            ctx.endRendering();
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    EXPECT_EQ(cmdBuf.beginRenderingCount, 1u);
    EXPECT_EQ(cmdBuf.endRenderingCount, 1u);
    EXPECT_TRUE(cmdBuf.lastBeginRenderingHadResolve);
    EXPECT_EQ(cmdBuf.lastResolveMode, EResolveMode::Average);
}

TEST(RenderGraphCoreTest, RasterRenderingHelperSupportsDepthOnlyPass)
{
    TestResourceFactory factory;
    TestCommandBuffer   cmdBuf;
    RenderGraphExecutor executor(factory);
    RenderGraph         graph;

    const auto depthTarget = graph.createTexture(RGTextureDesc{
        .label  = "shadow.depth",
        .format = EFormat::D32_SFLOAT,
        .extent = Extent3D{256, 256, 1},
        .usage  = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
    });

    graph.addPass(
        "shadow-depth-only",
        [&](RGPassBuilder& pass) {
            pass.useDepthAttachment(depthTarget);
        },
        [&](RGRenderContext& ctx) {
            ctx.beginRasterRendering({
                .renderArea = Rect2D{
                    .offset = glm::vec2{0.0f, 0.0f},
                    .extent = glm::vec2{256.0f, 256.0f},
                },
                .depth = RGRenderContext::DepthRenderingDesc{
                    .depth       = depthTarget,
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
            ctx.endRendering();
        });

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    EXPECT_EQ(cmdBuf.beginRenderingCount, 1u);
    EXPECT_EQ(cmdBuf.endRenderingCount, 1u);
    EXPECT_TRUE(cmdBuf.lastBeginRenderingHadDepth);
    EXPECT_EQ(cmdBuf.lastDepthFinalLayout, EImageLayout::ShaderReadOnlyOptimal);
}

TEST(RenderGraphCoreTest, FrameUploadArenaSharesAlignedSlicesPerFlight)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    FrameUploadArena   arena(factory, /*flightCount=*/2, /*initialCapacity=*/64);

    ASSERT_TRUE(arena.beginFlight(0));
    const auto first = arena.allocate(0, /*size=*/12, /*alignment=*/16);
    const auto second = arena.allocate(0, /*size=*/8, /*alignment=*/16);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(first->valid());
    ASSERT_TRUE(second->valid());
    EXPECT_EQ(first->buffer.get(), second->buffer.get());
    EXPECT_EQ(first->offset, 0u);
    EXPECT_EQ(second->offset, 16u);
    EXPECT_EQ(arena.bytesUsed(0), 24u);
    EXPECT_EQ(arena.capacity(0), 64u);

    const auto descriptor = second->descriptor();
    EXPECT_EQ(descriptor.buffer, second->bufferHandle());
    EXPECT_EQ(descriptor.offset, 16u);
    EXPECT_EQ(descriptor.range, 8u);

    const uint32_t value = 42;
    EXPECT_TRUE(first->write(&value, sizeof(value)));
    EXPECT_FALSE(first->write(&value, 13u));

    ASSERT_TRUE(arena.beginFlight(1));
    const auto otherFlight = arena.allocate(1, /*size=*/4, /*alignment=*/16);
    ASSERT_TRUE(otherFlight.has_value());
    EXPECT_NE(otherFlight->buffer.get(), first->buffer.get());
    EXPECT_EQ(otherFlight->offset, 0u);
    EXPECT_EQ(arena.bytesUsed(1), 4u);
}

TEST(RenderGraphCoreTest, FrameUploadArenaGrowsAndRetiresPreviousBacking)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory factory;
    FrameUploadArena   arena(factory, /*flightCount=*/1, /*initialCapacity=*/16);
    ASSERT_TRUE(arena.beginFlight(0));

    const auto first = arena.allocate(0, /*size=*/12, /*alignment=*/4);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(arena.capacity(0), 16u);
    const auto oldBacking = first->buffer;

    const auto second = arena.allocate(0, /*size=*/12, /*alignment=*/4);
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(second->buffer.get(), oldBacking.get());
    EXPECT_EQ(second->offset, 12u);
    EXPECT_EQ(arena.capacity(0), 32u);
    EXPECT_EQ(arena.bytesUsed(0), 24u);
    EXPECT_EQ(deletionQueue.pendingCount(), 1u);

    ASSERT_TRUE(arena.beginFlight(0));
    const auto afterReset = arena.allocate(0, /*size=*/4, /*alignment=*/16);
    ASSERT_TRUE(afterReset.has_value());
    EXPECT_EQ(afterReset->offset, 0u);
    EXPECT_EQ(afterReset->buffer.get(), second->buffer.get());

    EXPECT_FALSE(arena.allocate(1, 4, 4).has_value());
    EXPECT_FALSE(arena.allocate(0, 4, 0).has_value());
}

} // namespace ya
