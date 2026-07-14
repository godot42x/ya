#include "Render/Core/RenderGraph.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderGraphImportUtils.h"
#include "Render/Core/RenderGraphResourceRegistry.h"
#include "Render/Core/RenderingInfoUtils.h"

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

    std::vector<TransitionRecord> transitions;
    std::vector<BufferBarrierRecord> bufferBarriers;
    uint32_t beginRenderingCount = 0;
    uint32_t endRenderingCount   = 0;
    std::vector<CopyBufferRecord> copyBuffers;
    bool lastBeginRenderingHadDepth = false;
    EImageLayout::T lastDepthFinalLayout = EImageLayout::Undefined;

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
    void copyImage(IImage*, EImageLayout::T, IImage*, EImageLayout::T, const std::vector<ImageCopy>&) override {}
    void beginRendering(const RenderingInfo& info) override
    {
        ++beginRenderingCount;
        lastBeginRenderingHadDepth = info.depthAttachment.has_value();
        lastDepthFinalLayout = info.depthAttachment ? info.depthAttachment->finalLayout : EImageLayout::Undefined;
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
    void transitionRenderTargetLayout(IRenderTarget*, EImageLayout::T, EImageLayout::T = EImageLayout::Undefined, EImageLayout::T = EImageLayout::Undefined) override {}
    void debugBeginLabel(const char*, const float* = nullptr) override {}
    void debugEndLabel() override {}
};

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
        .buffer = reinterpret_cast<IBuffer*>(0x1),
    });

    const auto* transient = graph.getBuffer(transientHandle);
    const auto* imported  = graph.getBuffer(importedHandle);
    ASSERT_NE(transient, nullptr);
    ASSERT_NE(imported, nullptr);
    EXPECT_EQ(transient->lifetime, ERGResourceLifetime::Transient);
    EXPECT_EQ(imported->lifetime, ERGResourceLifetime::Imported);
    ASSERT_TRUE(imported->imported.has_value());
    EXPECT_EQ(imported->imported->buffer, reinterpret_cast<IBuffer*>(0x1));
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
    ASSERT_EQ(compiled.textureStates.size(), 2u);
    EXPECT_EQ(compiled.textureStates[0].requiredState.layout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_EQ(compiled.textureStates[1].requiredState.layout, EImageLayout::ShaderReadOnlyOptimal);
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
    EXPECT_NE(dump.find("textureStates("), std::string::npos);
    EXPECT_NE(dump.find("issues(1)"), std::string::npos);
    EXPECT_NE(dump.find("InvalidUsage"), std::string::npos);
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
        pass.write(storage);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.textureStates.size(), 1u);
    EXPECT_EQ(compiled.textureStates[0].requiredState.layout, EImageLayout::DepthStencilAttachmentOptimal);
    EXPECT_EQ(compiled.textureStates[0].requiredState.access,
              static_cast<EResourceAccess::T>(EResourceAccess::DepthStencilAttachmentRead | EResourceAccess::DepthStencilAttachmentWrite));
    ASSERT_EQ(compiled.bufferStates.size(), 1u);
    EXPECT_EQ(compiled.bufferStates[0].requiredState.access, EResourceAccess::ShaderWrite);
    EXPECT_EQ(compiled.bufferStates[0].requiredState.size, 256u);
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
    ASSERT_EQ(compiled.textureStates.size(), 1u);
    EXPECT_EQ(compiled.textureStates[0].requiredState.subresourceRange.baseMipLevel, 1u);
    EXPECT_EQ(compiled.textureStates[0].requiredState.subresourceRange.baseArrayLayer, 3u);
    EXPECT_EQ(compiled.textureStates[0].requiredState.subresourceRange.layerCount, 1u);
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
        pass.readWrite(commands);
    });
    const auto drawPass = graph.addPass("draw", [&](RGPassBuilder& pass) {
        pass.indirectRead(commands);
    });

    const auto compiled = graph.compile();
    ASSERT_TRUE(compiled.isValid());
    ASSERT_EQ(compiled.bufferStates.size(), 2u);
    EXPECT_EQ(compiled.bufferStates[0].requiredState.stages, EPipelineStage::ComputeShader);
    EXPECT_EQ(compiled.bufferStates[0].requiredState.access,
              static_cast<EResourceAccess::T>(EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite));
    EXPECT_EQ(compiled.bufferStates[1].requiredState.stages, EPipelineStage::DrawIndirect);
    EXPECT_EQ(compiled.bufferStates[1].requiredState.access, EResourceAccess::IndirectCommandRead);
    EXPECT_NE(std::find(compiled.dependencies.begin(), compiled.dependencies.end(), RGDependencyEdge{cullPass, drawPass}),
              compiled.dependencies.end());
}

TEST(RenderGraphCoreTest, CompileRejectsIndirectReadWithoutIndirectUsage)
{
    RenderGraph graph;
    const auto commands = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "storage-only.commands",
            .usage = EBufferUsage::StorageBuffer,
            .size  = 128,
        },
        .buffer = reinterpret_cast<IBuffer*>(0x1),
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
    ASSERT_EQ(compiled.textureStates.size(), 1u);
    EXPECT_EQ(compiled.textureStates[0].requiredState.subresourceRange.aspectMask, EImageAspect::Depth);
    EXPECT_EQ(compiled.textureStates[0].requiredState.subresourceRange.baseArrayLayer, 0u);
    EXPECT_EQ(compiled.textureStates[0].requiredState.subresourceRange.layerCount, 1u);

    RenderGraphResourceRegistry registry(factory);
    registry.sync(graph);

    const auto* imported = registry.resolveTexture(shadowDepth);
    ASSERT_NE(imported, nullptr);
    EXPECT_EQ(imported->getImage(), existingImage.get());
    EXPECT_EQ(imported->getImageView(), existingView.get());
    EXPECT_EQ(factory.createdViews, createdViewsBeforeSync);
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
    const auto textureHandle = graphA.createTexture(RGTextureDesc{
        .label  = "persistent.ao",
        .format = EFormat::R8_UNORM,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, ERGResourceLifetime::Persistent);
    const auto bufferHandle = graphA.createBuffer(RGBufferDesc{
        .label = "persistent.constants",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    }, ERGResourceLifetime::Persistent);

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
    graphB.createTexture(RGTextureDesc{
        .label  = "persistent.ao",
        .format = EFormat::R8_UNORM,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, ERGResourceLifetime::Persistent);
    graphB.createBuffer(RGBufferDesc{
        .label = "persistent.constants",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    }, ERGResourceLifetime::Persistent);

    registry.sync(graphB);
    EXPECT_EQ(registry.resolveTexture(textureHandle), firstTexture);
    EXPECT_EQ(registry.resolveBuffer(bufferHandle), firstBuffer);
    EXPECT_EQ(factory.createdImages, 1u);
    EXPECT_EQ(factory.createdViews, 1u);
    EXPECT_EQ(factory.createdBuffers, 1u);
}

TEST(RenderGraphCoreTest, ResourceRegistryRefreshesImportedKeepAliveWithoutRecreatingView)
{
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

    EXPECT_TRUE(ownerAWeak.expired());
    EXPECT_FALSE(ownerBWeak.expired());
    EXPECT_EQ(factory.importedImages, 0u);
    EXPECT_EQ(factory.createdViews, createdViewsBeforeSync);

    registry.clear();
    EXPECT_TRUE(ownerBWeak.expired());
}

TEST(RenderGraphCoreTest, ResourceRegistryReplacesResourcesWhenDescriptorsChange)
{
    TestResourceFactory factory;
    RenderGraphResourceRegistry registry(factory);

    RenderGraph graphA;
    const auto textureHandle = graphA.createTexture(RGTextureDesc{
        .label  = "persistent.history",
        .format = EFormat::R32_SFLOAT,
        .extent = Extent3D{160, 90, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, ERGResourceLifetime::Persistent);
    const auto bufferHandle = graphA.createBuffer(RGBufferDesc{
        .label = "persistent.history.buffer",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 128,
    }, ERGResourceLifetime::Persistent);

    registry.sync(graphA);
    const auto* firstTexture = registry.resolveTexture(textureHandle);
    auto*       firstBuffer  = registry.resolveBuffer(bufferHandle);
    ASSERT_NE(firstTexture, nullptr);
    ASSERT_NE(firstBuffer, nullptr);

    RenderGraph graphB;
    graphB.createTexture(RGTextureDesc{
        .label  = "persistent.history",
        .format = EFormat::R32_SFLOAT,
        .extent = Extent3D{320, 180, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, ERGResourceLifetime::Persistent);
    graphB.createBuffer(RGBufferDesc{
        .label = "persistent.history.buffer",
        .usage = EBufferUsage::StorageBuffer,
        .size  = 256,
    }, ERGResourceLifetime::Persistent);

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
        graph.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label       = "history.imported.face",
                .format      = EFormat::R16G16B16A16_SFLOAT,
                .extent      = Extent3D{128, 128, 1},
                .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .mipLevels   = 1,
                .arrayLayers = 1,
            },
            .importDesc = ImportedImageDesc{
                .label        = "history.imported.face",
                .nativeHandle = reinterpret_cast<void*>(0x303),
                .format       = EFormat::R16G16B16A16_SFLOAT,
                .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .extent       = Extent3D{128, 128, 1},
                .mipLevels    = 4,
                .arrayLayers  = 6,
            },
            .viewDesc = ImageViewCreateInfo{
                .label          = std::string(viewLabel),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 2,
                .levelCount     = 1,
                .baseArrayLayer = 5,
                .layerCount     = 1,
            },
        });
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
    graphA.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = "history.imported.face",
            .format      = EFormat::R16G16B16A16_SFLOAT,
            .extent      = Extent3D{128, 128, 1},
            .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .mipLevels   = 1,
            .arrayLayers = 1,
        },
        .importDesc = ImportedImageDesc{
            .label        = "history.imported.face",
            .nativeHandle = reinterpret_cast<void*>(0x404),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{128, 128, 1},
            .mipLevels    = 4,
            .arrayLayers  = 6,
        },
        .viewDesc = ImageViewCreateInfo{
            .label          = "history.imported.face.view",
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    });

    registry.sync(graphA);
    EXPECT_EQ(factory.importedImages, 1u);
    EXPECT_EQ(factory.createdViews, 1u);

    RenderGraph graphB;
    graphB.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = "history.imported.face",
            .format      = EFormat::R16G16B16A16_SFLOAT,
            .extent      = Extent3D{128, 128, 1},
            .usage       = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .mipLevels   = 1,
            .arrayLayers = 1,
        },
        .importDesc = ImportedImageDesc{
            .label        = "history.imported.face",
            .nativeHandle = reinterpret_cast<void*>(0x404),
            .format       = EFormat::R16G16B16A16_SFLOAT,
            .usage        = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .extent       = Extent3D{128, 128, 1},
            .mipLevels    = 4,
            .arrayLayers  = 6,
        },
        .viewDesc = ImageViewCreateInfo{
            .label          = "history.imported.face.view",
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 1,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    });

    registry.sync(graphB);
    EXPECT_EQ(factory.importedImages, 2u);
    EXPECT_EQ(factory.createdViews, 2u);
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

    const auto spec = makeAttachmentImageSpec(
        image,
        view,
        EAttachmentLoadOp::Load,
        EAttachmentStoreOp::Store,
        EImageLayout::ColorAttachmentOptimal,
        EImageLayout::ShaderReadOnlyOptimal);

    ASSERT_EQ(spec.image, image.get());
    ASSERT_EQ(spec.imageView, view.get());
    EXPECT_EQ(spec.retainedImage.get(), image.get());
    EXPECT_EQ(spec.retainedImageView.get(), view.get());
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
            pass.write(buffer);
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
            pass.read(buffer);
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
        [=](RGPassBuilder& pass) { pass.read(imported); },
        [](RGRenderContext&) {});

    ASSERT_TRUE(executor.execute(graph, cmdBuf));
    ASSERT_EQ(cmdBuf.bufferBarriers.size(), 1u);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].srcStage, EPipelineStage::Host);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstStage, EPipelineStage::AllCommands);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].srcAccess, EResourceAccess::HostWrite);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].dstAccess, EResourceAccess::ShaderRead);
    EXPECT_EQ(cmdBuf.bufferBarriers[0].size, 256u);
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

} // namespace ya
