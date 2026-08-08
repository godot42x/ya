#include "Foundation/RHI/Core/TextureUploadService.h"

#include "Foundation/RHI/Core/Buffer.h"
#include "Foundation/RHI/Core/CommandBuffer.h"
#include "Foundation/RHI/Core/Image.h"
#include "Foundation/RHI/Render.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace ya
{

namespace
{

class SpecStagingBuffer final : public IBuffer
{
  private:
    std::string  _name = "Test.Staging";
    EBufferUsage _usage = EBufferUsage::TransferSrc;
    uint32_t     _size  = 1024;

  public:
    bool writeData(const void*, uint32_t = 0, uint32_t = 0) override { return true; }
    bool flush(uint32_t = 0, uint32_t = 0) override { return true; }
    void unmap() override {}
    BufferHandle getHandle() const override
    {
        return BufferHandle{reinterpret_cast<void*>(static_cast<uintptr_t>(_size + 1))};
    }
    uint32_t getSize() const override { return _size; }
    EBufferUsage getUsage() const override { return _usage; }
    bool isHostVisible() const override { return true; }
    const std::string& getName() const override { return _name; }

  protected:
    void mapInternal(void** ptr) override { *ptr = nullptr; }
};

class UploadTestImage final : public IImage
{
  private:
    ImageCreateInfo _desc;

  public:
    explicit UploadTestImage(const ImageCreateInfo& desc) : _desc(desc) {}

    ImageHandle getHandle() const override
    {
        return ImageHandle{reinterpret_cast<void*>(static_cast<uintptr_t>(_desc.extent.width + 1))};
    }
    uint32_t getWidth() const override { return _desc.extent.width; }
    uint32_t getHeight() const override { return _desc.extent.height; }
    EFormat::T getFormat() const override { return _desc.format; }
    uint32_t getMipLevels() const override { return _desc.mipLevels; }
    uint32_t getArrayLayers() const override { return _desc.arrayLayers; }
    EImageUsage::T getUsage() const override { return _desc.usage; }
    EImageLayout::T getCompatibilityLayout() const override { return _desc.initialLayout; }
    void setDebugName(const std::string& name) override { _desc.label = name; }
};

class RecordingCommandBuffer final : public ICommandBuffer
{
  public:
    struct TransitionRecord
    {
        EImageLayout::T oldLayout = EImageLayout::Undefined;
        EImageLayout::T newLayout = EImageLayout::Undefined;
        bool            hasRange  = false;
    };

    std::vector<TransitionRecord> transitions;
    uint32_t                      copyBufferToImageCount = 0;
    uint32_t                      generateMipmapsCount   = 0;
    bool                          bMipmapsSupported      = true;

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
    void bufferMemoryBarrier(IBuffer*, EPipelineStage::T, EPipelineStage::T, EResourceAccess::T, EResourceAccess::T, uint64_t = 0, uint64_t = 0) override {}
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
    void copyBufferToImage(IBuffer*, IImage*, EImageLayout::T, const std::vector<BufferImageCopy>&) override
    {
        ++copyBufferToImageCount;
    }
    void copyImageToBuffer(IImage*, EImageLayout::T, IBuffer*, const std::vector<BufferImageCopy>&) override {}
    void copyImage(IImage*, EImageLayout::T, IImage*, EImageLayout::T, const std::vector<ImageCopy>&) override {}
    void beginRendering(const RenderingInfo&) override {}
    void endRendering(const RenderingInfo& = {}) override {}
    void transitionImageLayout(IImage*, EImageLayout::T oldLayout, EImageLayout::T newLayout, const ImageSubresourceRange* range = nullptr) override
    {
        transitions.push_back({
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .hasRange  = range != nullptr,
        });
    }
    void transitionImageLayoutAuto(IImage*, EImageLayout::T, const ImageSubresourceRange* = nullptr) override {}
    void debugBeginLabel(const char*, const float* = nullptr) override {}
    void debugEndLabel() override {}
    bool generateMipmaps(IImage*, EImageLayout::T, EImageLayout::T) override
    {
        ++generateMipmapsCount;
        return bMipmapsSupported;
    }
};

class UploadTestRender final : public IRender
{
  public:
    RecordingCommandBuffer* recorded = nullptr;

    void destroy() override {}
    bool begin(int32_t*) override { return false; }
    bool end(int32_t, std::vector<void*>) override { return false; }
    void getWindowSize(int& width, int& height) const override
    {
        width = 0;
        height = 0;
    }
    void setVsync(bool) override {}
    uint32_t getSwapchainWidth() const override { return 0; }
    uint32_t getSwapchainHeight() const override { return 0; }
    uint32_t getSwapchainImageCount() const override { return 0; }
    void allocateCommandBuffers(uint32_t, std::vector<std::shared_ptr<ICommandBuffer>>&) override {}
    void waitIdle() override {}
    ICommandBuffer* beginIsolateCommands(const std::string& = "") override { return recorded; }
    void endIsolateCommands(ICommandBuffer*) override {}
    ISwapchain* getSwapchain() override { return nullptr; }
    IDescriptorSetHelper* getDescriptorHelper() override { return nullptr; }
    IRenderResourceFactory* getResourceFactory() override { return nullptr; }
    void submitToQueue(const std::vector<void*>&, const std::vector<void*>&, const std::vector<void*>&, void* = nullptr) override {}
    int presentImage(int32_t, const std::vector<void*>&) override { return 0; }
    void* getCurrentImageAvailableSemaphore() override { return nullptr; }
    void* getCurrentFrameFence() override { return nullptr; }
    uint32_t getCurrentFrameIndex() const override { return 0; }
    void* getRenderFinishedSemaphore(uint32_t) override { return nullptr; }
    void* createSemaphore(const char* = nullptr) override { return nullptr; }
    void destroySemaphore(void*) override {}
    void advanceFrame() override {}

  protected:
    void* getNativeWindowHandle() const override { return nullptr; }
};

std::shared_ptr<UploadTestImage> makeUploadImage(uint32_t mipLevels = 1)
{
    return std::make_shared<UploadTestImage>(ImageCreateInfo{
        .label      = "Test.Upload",
        .format     = EFormat::R8G8B8A8_UNORM,
        .extent     = {.width = 16, .height = 16, .depth = 1},
        .mipLevels  = mipLevels,
        .arrayLayers = 1,
        .samples    = ESampleCount::Sample_1,
        .usage      = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::TransferDst),
    });
}

BufferImageCopy makeCopyRegion(uint32_t mipLevel)
{
    return BufferImageCopy{
        .bufferOffset     = 0,
        .bufferRowLength  = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask     = EImageAspect::Color,
            .mipLevel       = mipLevel,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .imageOffsetX     = 0,
        .imageOffsetY     = 0,
        .imageOffsetZ     = 0,
        .imageExtentWidth = 16,
        .imageExtentHeight = 16,
        .imageExtentDepth = 1,
    };
}

} // namespace

TEST(TextureUploadServiceTest, UploadRecordsTransitionsAndCopies)
{
    RecordingCommandBuffer cmdBuf;
    UploadTestRender render;
    render.recorded = &cmdBuf;

    auto image = makeUploadImage();
    auto staging = std::make_shared<SpecStagingBuffer>();

    TextureUploadService service;
    uint32_t outMipLevels = 0;
    ASSERT_TRUE(service.upload(
        render,
        TextureUploadRequest{
            .image   = image,
            .staging = staging,
            .regions = {makeCopyRegion(0)},
            .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
            .label   = "Test.Upload",
        },
        &outMipLevels));

    EXPECT_EQ(outMipLevels, 1u);
    ASSERT_EQ(cmdBuf.transitions.size(), 2u);
    EXPECT_EQ(cmdBuf.transitions[0].oldLayout, EImageLayout::Undefined);
    EXPECT_EQ(cmdBuf.transitions[0].newLayout, EImageLayout::TransferDst);
    EXPECT_EQ(cmdBuf.transitions[1].oldLayout, EImageLayout::TransferDst);
    EXPECT_EQ(cmdBuf.transitions[1].newLayout, EImageLayout::ShaderReadOnlyOptimal);
    EXPECT_EQ(cmdBuf.copyBufferToImageCount, 1u);
}

TEST(TextureUploadServiceTest, UploadUsesProvidedSubresourceRanges)
{
    RecordingCommandBuffer cmdBuf;
    UploadTestRender render;
    render.recorded = &cmdBuf;

    const ImageSubresourceRange cubeRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 6,
    };

    TextureUploadService service;
    ASSERT_TRUE(service.upload(
        render,
        TextureUploadRequest{
            .image         = makeUploadImage(),
            .staging       = std::make_shared<SpecStagingBuffer>(),
            .regions       = {makeCopyRegion(0)},
            .uploadRange   = cubeRange,
            .finalizeRange = cubeRange,
            .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
            .label         = "Test.Cube",
        }));

    ASSERT_EQ(cmdBuf.transitions.size(), 2u);
    EXPECT_TRUE(cmdBuf.transitions[0].hasRange);
    EXPECT_TRUE(cmdBuf.transitions[1].hasRange);
}

TEST(TextureUploadServiceTest, UploadRejectsInvalidRequests)
{
    RecordingCommandBuffer cmdBuf;
    UploadTestRender render;
    render.recorded = &cmdBuf;

    TextureUploadService service;
    EXPECT_FALSE(service.upload(render, TextureUploadRequest{
        .image   = nullptr,
        .staging = std::make_shared<SpecStagingBuffer>(),
        .regions = {makeCopyRegion(0)},
    }));
    EXPECT_FALSE(service.upload(render, TextureUploadRequest{
        .image   = makeUploadImage(),
        .staging = nullptr,
        .regions = {makeCopyRegion(0)},
    }));
    EXPECT_FALSE(service.upload(render, TextureUploadRequest{
        .image   = makeUploadImage(),
        .staging = std::make_shared<SpecStagingBuffer>(),
    }));
    EXPECT_EQ(cmdBuf.copyBufferToImageCount, 0u);
}

TEST(TextureUploadServiceTest, UploadMipGenerationFailureKeepsBaseLevel)
{
    RecordingCommandBuffer cmdBuf;
    cmdBuf.bMipmapsSupported = false;
    UploadTestRender render;
    render.recorded = &cmdBuf;

    TextureUploadService service;
    uint32_t outMipLevels = 0;
    ASSERT_TRUE(service.upload(
        render,
        TextureUploadRequest{
            .image            = makeUploadImage(4),
            .staging          = std::make_shared<SpecStagingBuffer>(),
            .regions          = {makeCopyRegion(0)},
            .bGenerateMipmaps = true,
            .finalLayout      = EImageLayout::ShaderReadOnlyOptimal,
            .label            = "Test.Mips",
        },
        &outMipLevels));

    EXPECT_EQ(outMipLevels, 1u);
    EXPECT_EQ(cmdBuf.generateMipmapsCount, 1u);
    ASSERT_EQ(cmdBuf.transitions.size(), 2u);
    EXPECT_EQ(cmdBuf.transitions[1].oldLayout, EImageLayout::TransferDst);
    EXPECT_EQ(cmdBuf.transitions[1].newLayout, EImageLayout::ShaderReadOnlyOptimal);
}

TEST(TextureUploadServiceTest, UploadMipGenerationSuccessKeepsImageMipCount)
{
    RecordingCommandBuffer cmdBuf;
    UploadTestRender render;
    render.recorded = &cmdBuf;

    TextureUploadService service;
    uint32_t outMipLevels = 0;
    ASSERT_TRUE(service.upload(
        render,
        TextureUploadRequest{
            .image            = makeUploadImage(4),
            .staging          = std::make_shared<SpecStagingBuffer>(),
            .regions          = {makeCopyRegion(0)},
            .bGenerateMipmaps = true,
            .finalLayout      = EImageLayout::ShaderReadOnlyOptimal,
            .label            = "Test.Mips",
        },
        &outMipLevels));

    EXPECT_EQ(outMipLevels, 4u);
    EXPECT_EQ(cmdBuf.generateMipmapsCount, 1u);
    EXPECT_EQ(cmdBuf.transitions.size(), 1u);
}

} // namespace ya
