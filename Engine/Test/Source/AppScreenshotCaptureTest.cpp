#include "Runtime/Application/Utility/AppScreenshotCapture.h"
#include "Runtime/Application/Utility/OffscreenJobRunner.h"
#include "Runtime/Application/App.h"

#include "Render/Render.h"
#include "Render/Core/RenderImage.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

class TestBuffer final : public IBuffer
{
  private:
    std::string _name;
    uint32_t    _size = 0;
    EBufferUsage _usage = EBufferUsage::None;

  public:
    explicit TestBuffer(const BufferCreateInfo& desc)
        : _name(desc.label), _size(desc.size), _usage(desc.usage)
    {}

    bool writeData(const void*, uint32_t = 0, uint32_t = 0) override { return true; }
    bool flush(uint32_t = 0, uint32_t = 0) override { return true; }
    void unmap() override {}
    BufferHandle getHandle() const override { return {}; }
    uint32_t getSize() const override { return _size; }
    EBufferUsage getUsage() const override { return _usage; }
    bool isHostVisible() const override { return true; }
    const std::string& getName() const override { return _name; }

  protected:
    void mapInternal(void** ptr) override { *ptr = nullptr; }
};

class TestImage final : public IImage
{
  private:
    ImageCreateInfo _desc;

  public:
    explicit TestImage(const ImageCreateInfo& desc)
        : _desc(desc)
    {}

    ImageHandle getHandle() const override { return ImageHandle{reinterpret_cast<void*>(0x1)}; }
    uint32_t getWidth() const override { return _desc.extent.width; }
    uint32_t getHeight() const override { return _desc.extent.height; }
    EFormat::T getFormat() const override { return _desc.format; }
    uint32_t getMipLevels() const override { return _desc.mipLevels; }
    uint32_t getArrayLayers() const override { return _desc.arrayLayers; }
    EImageUsage::T getUsage() const override { return _desc.usage; }
    EImageLayout::T getCompatibilityLayout() const override { return _desc.initialLayout; }
    void setDebugName(const std::string& name) override { _desc.label = name; }
};

class TestImageView final : public IImageView
{
  public:
    TestImageView(std::shared_ptr<IImage> image, const ImageViewCreateInfo& desc)
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

    ImageViewHandle getHandle() const override { return ImageViewHandle{reinterpret_cast<void*>(0x2)}; }
    EFormat::T getFormat() const override { return _image ? _image->getFormat() : EFormat::Undefined; }
    void setDebugName(const std::string&) override {}
};

class TestResourceFactory final : public IRenderResourceFactory
{
  public:
    std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) override
    {
        return std::make_shared<TestBuffer>(desc);
    }

    std::shared_ptr<Sampler> createSampler(const SamplerDesc&) override { return nullptr; }

    std::shared_ptr<IImage> createImage(const ImageCreateInfo& desc) override
    {
        return std::make_shared<TestImage>(desc);
    }

    std::shared_ptr<IImage> importImage(const ImportedImageDesc& desc) override
    {
        return std::make_shared<TestImage>(ImageCreateInfo{
            .label         = desc.label,
            .format        = desc.format,
            .extent        = {.width = desc.extent.width, .height = desc.extent.height, .depth = desc.extent.depth},
            .mipLevels     = desc.mipLevels,
            .arrayLayers   = desc.arrayLayers,
            .usage         = desc.usage,
            .initialLayout = desc.initialLayout,
        });
    }

    std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage> image, const ImageViewCreateInfo& desc) override
    {
        return std::make_shared<TestImageView>(std::move(image), desc);
    }
};

class TestRender final : public IRender
{
  public:
    TestResourceFactory factory;

    TestRender()
    {
        _renderAPI = ERenderAPI::Vulkan;
    }

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
    ICommandBuffer* beginIsolateCommands(const std::string& = "") override { return nullptr; }
    void endIsolateCommands(ICommandBuffer*) override {}
    ISwapchain* getSwapchain() override { return nullptr; }
    IDescriptorSetHelper* getDescriptorHelper() override { return nullptr; }
    IRenderResourceFactory* getResourceFactory() override { return &factory; }
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

std::shared_ptr<RenderImage> makePresentationImage()
{
    auto image = std::make_shared<TestImage>(ImageCreateInfo{
        .label         = "presentation",
        .format        = EFormat::R8G8B8A8_UNORM,
        .extent        = {.width = 320, .height = 180, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .usage         = EImageUsage::ColorAttachment | EImageUsage::TransferSrc,
        .initialLayout = EImageLayout::PresentSrcKHR,
    });
    auto view = std::make_shared<TestImageView>(image, ImageViewCreateInfo{
        .label       = "presentation.view",
        .viewType    = EImageViewType::View2D,
        .aspectFlags = EImageAspect::Color,
    });

    auto renderImage = std::make_shared<RenderImage>();
    renderImage->label = "presentation";
    renderImage->image = std::move(image);
    renderImage->defaultView = std::move(view);
    return renderImage;
}

} // namespace

TEST(AppScreenshotCaptureTest, PresentationRequestRetainsPresentationOwnerUntilReset)
{
    TestRender render;
    AppScreenshotCaptureState state;

    auto presentationImage = makePresentationImage();
    std::weak_ptr<RenderImage> presentationWeak = presentationImage;

    const bool bRequested = AppScreenshotCapture::request(&render,
                                                          OffscreenJobQueueService{},
                                                          nullptr,
                                                          nullptr,
                                                          presentationImage,
                                                          state,
                                                          "Engine/Saved/Screenshot/test.png",
                                                          EAutomationScreenshotTarget::Presentation);
    ASSERT_TRUE(bRequested);

    presentationImage.reset();
    EXPECT_FALSE(presentationWeak.expired());
    EXPECT_TRUE(state.bPendingPresentationCapture);
    EXPECT_NE(state.presentationSourceImage, nullptr);

    AppScreenshotCapture::reset(state);
    EXPECT_TRUE(presentationWeak.expired());
}

TEST(AppScreenshotCaptureTest, ViewportRequestRetainsSourceImageAfterCallerDropsRenderImageOwner)
{
    TestRender render;
    AppScreenshotCaptureState state;

    auto viewportImage = makePresentationImage();
    std::weak_ptr<IImage> viewportImageWeak = viewportImage->getImageShared();

    const bool bRequested = AppScreenshotCapture::request(&render,
                                                          OffscreenJobQueueService{},
                                                          nullptr,
                                                          viewportImage,
                                                          nullptr,
                                                          state,
                                                          "Engine/Saved/Screenshot/test_viewport.png",
                                                          EAutomationScreenshotTarget::Viewport);
    ASSERT_TRUE(bRequested);
    ASSERT_NE(state.pendingJob, nullptr);

    viewportImage.reset();
    EXPECT_FALSE(viewportImageWeak.expired());

    AppScreenshotCapture::reset(state);
    EXPECT_TRUE(viewportImageWeak.expired());
}

} // namespace ya
