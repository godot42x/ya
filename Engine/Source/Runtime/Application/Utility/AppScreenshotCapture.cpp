#include "Runtime/Application/Utility/AppScreenshotCapture.h"

#include "Runtime/Application/Utility/OffscreenJobRunner.h"

#include "Core/Log.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderGraphImportUtils.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanMemoryAllocator.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/OffscreenJob.h"
#include "Render/Core/RenderImage.h"
#include "Render/Core/RenderResourceFactory.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <vector>

namespace ya
{
namespace
{
constexpr uint32_t PNG_CHANNELS         = 4;
constexpr uint32_t BYTES_PER_PIXEL_RGBA = 4;

bool isSupportedScreenshotFormat(EFormat::T format)
{
    switch (format) {
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::R8G8B8A8_SRGB:
    case EFormat::B8G8R8A8_UNORM:
    case EFormat::B8G8R8A8_SRGB:
    case EFormat::R16G16B16A16_SFLOAT:
        return true;
    default:
        return false;
    }
}

float clampUnit(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float decodeFloat16(uint16_t value)
{
    const uint32_t sign     = static_cast<uint32_t>(value & 0x8000u) << 16u;
    const uint32_t exponent = (value >> 10u) & 0x1Fu;
    const uint32_t mantissa = value & 0x03FFu;

    uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        }
        else {
            uint32_t normalizedMantissa = mantissa;
            uint32_t adjustedExponent   = 113u;
            while ((normalizedMantissa & 0x0400u) == 0u) {
                normalizedMantissa <<= 1u;
                --adjustedExponent;
            }
            normalizedMantissa &= 0x03FFu;
            bits = sign | (adjustedExponent << 23u) | (normalizedMantissa << 13u);
        }
    }
    else if (exponent == 0x1Fu) {
        bits = sign | 0x7F800000u | (mantissa << 13u);
    }
    else {
        bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
    }

    float output = 0.0f;
    std::memcpy(&output, &bits, sizeof(output));
    return output;
}

void copyRgba8ToRgba8(std::vector<unsigned char>& output, const std::byte* inputData, uint32_t pixelCount)
{
    output.resize(static_cast<size_t>(pixelCount) * BYTES_PER_PIXEL_RGBA);
    std::memcpy(output.data(), inputData, output.size());
}

void copyBgra8ToRgba8(std::vector<unsigned char>& output, const std::byte* inputData, uint32_t pixelCount)
{
    output.resize(static_cast<size_t>(pixelCount) * BYTES_PER_PIXEL_RGBA);

    const auto* input = reinterpret_cast<const unsigned char*>(inputData);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        const size_t src = static_cast<size_t>(i) * BYTES_PER_PIXEL_RGBA;
        const size_t dst = static_cast<size_t>(i) * BYTES_PER_PIXEL_RGBA;
        output[dst + 0]  = input[src + 2];
        output[dst + 1]  = input[src + 1];
        output[dst + 2]  = input[src + 0];
        output[dst + 3]  = input[src + 3];
    }
}

void copyRgba16fToRgba8(std::vector<unsigned char>& output, const std::byte* inputData, uint32_t pixelCount)
{
    output.resize(static_cast<size_t>(pixelCount) * PNG_CHANNELS);

    const auto* input = reinterpret_cast<const uint16_t*>(inputData);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        const size_t src = static_cast<size_t>(i) * PNG_CHANNELS;
        const size_t dst = static_cast<size_t>(i) * PNG_CHANNELS;
        for (uint32_t c = 0; c < PNG_CHANNELS; ++c) {
            const float normalized = clampUnit(decodeFloat16(input[src + c]));
            output[dst + c]        = static_cast<unsigned char>(normalized * 255.0f + 0.5f);
        }
    }
}

std::shared_ptr<IBuffer> createReadbackBuffer(IRender* render, uint32_t width, uint32_t height, EFormat::T format)
{
    if (!render || width == 0 || height == 0) {
        return nullptr;
    }

    const uint32_t bytesPerPixel = format == EFormat::R16G16B16A16_SFLOAT ? 8u : 4u;
    const uint32_t bufferSize    = width * height * bytesPerPixel;
    return render->getResourceFactory()->createBuffer(BufferCreateInfo{
        .label       = "AutomationScreenshotReadback",
        .usage       = EBufferUsage::TransferDst,
        .size        = bufferSize,
        .memoryUsage = EMemoryUsage::GpuToCpu,
    });
}

struct ScreenshotSourceInfo
{
    std::shared_ptr<IImage> image;
    Extent2D                extent{};
    EFormat::T              format = EFormat::Undefined;

    [[nodiscard]] bool isValid() const
    {
        return image != nullptr && extent.width > 0 && extent.height > 0 && format != EFormat::Undefined;
    }
};

ScreenshotSourceInfo makeScreenshotSourceInfo(Texture* texture)
{
    if (!texture || !texture->getImage()) {
        return {};
    }

    return ScreenshotSourceInfo{
        .image  = texture->getImageShared(),
        .extent = texture->getExtent(),
        .format = texture->getFormat(),
    };
}

ScreenshotSourceInfo makeScreenshotSourceInfo(RenderImage* image)
{
    if (!image || !image->getImage()) {
        return {};
    }

    return ScreenshotSourceInfo{
        .image  = image->getImageShared(),
        .extent = image->getExtent(),
        .format = image->getFormat(),
    };
}

BufferImageCopy makeScreenshotReadbackRegion(Extent2D extent)
{
    return BufferImageCopy{
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
        .imageExtentWidth  = extent.width,
        .imageExtentHeight = extent.height,
        .imageExtentDepth  = 1,
    };
}

RGImportedBufferDesc makeImportedReadbackBufferDesc(const std::shared_ptr<IBuffer>& buffer)
{
    YA_CORE_ASSERT(buffer != nullptr, "Screenshot readback buffer must not be null");
    return RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = buffer->getName(),
            .usage = EBufferUsage::TransferDst,
            .size  = buffer->getSize(),
        },
        .buffer = buffer.get(),
        .finalState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostRead,
            .size   = buffer->getSize(),
        },
        .retainedResources = {buffer},
    };
}

bool executeScreenshotCopyGraph(RenderGraphExecutor& executor,
                                ICommandBuffer&      cmdBuf,
                                const RenderImage&   sourceImage,
                                EImageLayout::T      initialLayout,
                                EImageLayout::T      finalLayout,
                                const std::shared_ptr<IBuffer>& readbackBuffer,
                                std::string_view     graphLabel)
{
    if (!sourceImage.getImage() || !sourceImage.getImageView() || !readbackBuffer) {
        return false;
    }

    const Extent2D extent = sourceImage.getExtent();
    RenderGraph graph;
    auto importedSource = makeImportedTextureDesc(
        sourceImage,
        graphLabel,
        finalLayout,
        EImageUsage::TransferSrc);
    importedSource.importDesc.initialLayout = initialLayout;
    const auto src = graph.importTexture(importedSource);
    const auto dst = graph.importBuffer(makeImportedReadbackBufferDesc(readbackBuffer));

    [[maybe_unused]] const auto pass = graph.addPass(
        std::string(graphLabel),
        [src, dst](RGPassBuilder& pass) {
            pass.transferSrc(src);
            pass.transferDst(dst);
        },
        [src, dst, extent](RGRenderContext& ctx) {
            ctx.copyTextureToBuffer(src, dst, {makeScreenshotReadbackRegion(extent)});
        });

    return executor.execute(graph, cmdBuf);
}

bool writePngFromReadback(const AppScreenshotCaptureState& state)
{
    if (!state.readbackBuffer || state.width == 0 || state.height == 0) {
        return false;
    }

    std::filesystem::path outputPath(state.outputPath);
    if (outputPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            YA_CORE_ERROR("Failed to create screenshot directory {}: {}", outputPath.parent_path().string(), ec.message());
            return false;
        }
    }

    auto* mapped = state.readbackBuffer->map<std::byte>();
    if (!mapped) {
        YA_CORE_ERROR("Failed to map screenshot readback buffer");
        return false;
    }

    std::vector<unsigned char> pngPixels;
    const uint32_t             pixelCount = state.width * state.height;
    switch (state.sourceFormat) {
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::R8G8B8A8_SRGB:
        copyRgba8ToRgba8(pngPixels, mapped, pixelCount);
        break;
    case EFormat::B8G8R8A8_UNORM:
    case EFormat::B8G8R8A8_SRGB:
        copyBgra8ToRgba8(pngPixels, mapped, pixelCount);
        break;
    case EFormat::R16G16B16A16_SFLOAT:
        copyRgba16fToRgba8(pngPixels, mapped, pixelCount);
        break;
    default:
        state.readbackBuffer->unmap();
        YA_CORE_ERROR("Unsupported screenshot source format {}", static_cast<int>(state.sourceFormat));
        return false;
    }

    state.readbackBuffer->unmap();

    const int strideBytes = static_cast<int>(state.width * BYTES_PER_PIXEL_RGBA);
    const int ret         = stbi_write_png(state.outputPath.c_str(),
                                   static_cast<int>(state.width),
                                   static_cast<int>(state.height),
                                   static_cast<int>(PNG_CHANNELS),
                                   pngPixels.data(),
                                   strideBytes);
    if (ret == 0) {
        YA_CORE_ERROR("Failed to write screenshot PNG: {}", state.outputPath);
        return false;
    }

    YA_CORE_INFO("Saved screenshot: {}", state.outputPath);
    return true;
}
} // namespace

bool AppScreenshotCapture::request(IRender*                        render,
                                   const OffscreenJobQueueService& offscreenQueueService,
                                   std::shared_ptr<RenderImage>    postprocessSourceImage,
                                   std::shared_ptr<RenderImage>    viewportSourceImage,
                                   std::shared_ptr<RenderImage>    presentationSourceImage,
                                   AppScreenshotCaptureState&      state,
                                   const std::string&              outputPath,
                                   EAutomationScreenshotTarget     target)
{
    if (!render || outputPath.empty()) {
        return false;
    }
    if (render->getAPI() != ERenderAPI::Vulkan) {
        YA_CORE_WARN("Screenshot automation currently supports Vulkan only");
        return false;
    }
    if (state.pendingJob || state.bCompleted || state.bPendingPresentationCapture || state.bPresentationCopyRecorded) {
        return false;
    }

    const std::shared_ptr<RenderImage> selectedRenderImage =
        target == EAutomationScreenshotTarget::Presentation
            ? presentationSourceImage
            : (postprocessSourceImage ? postprocessSourceImage : viewportSourceImage);
    const ScreenshotSourceInfo source = target == EAutomationScreenshotTarget::Presentation
        ? makeScreenshotSourceInfo(presentationSourceImage.get())
        : (postprocessSourceImage ? makeScreenshotSourceInfo(postprocessSourceImage.get())
                                  : makeScreenshotSourceInfo(viewportSourceImage.get()));
    if (!source.isValid()) {
        return false;
    }

    const Extent2D extent = source.extent;
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }
    if (!isSupportedScreenshotFormat(source.format)) {
        YA_CORE_WARN("Unsupported screenshot source format {}", static_cast<int>(source.format));
        state.bFailed = true;
        return false;
    }

    auto readbackBuffer = createReadbackBuffer(render, extent.width, extent.height, source.format);
    if (!readbackBuffer) {
        YA_CORE_ERROR("Failed to create screenshot readback buffer");
        state.bFailed = true;
        return false;
    }

    state.outputPath                   = outputPath;
    state.readbackBuffer               = std::move(readbackBuffer);
    state.copyExecutor                 = std::make_shared<RenderGraphExecutor>(*render->getResourceFactory());
    state.presentationSourceImage      = nullptr;
    state.width                        = extent.width;
    state.height                       = extent.height;
    state.sourceFormat                 = source.format;
    state.target                       = target;
    state.recordedFrameIndex           = 0;
    state.bCompleted                   = false;
    state.bFailed                      = false;
    state.bPendingPresentationCapture  = false;
    state.bPresentationCopyRecorded    = false;

    if (target == EAutomationScreenshotTarget::Presentation) {
        state.presentationSourceImage      = std::move(presentationSourceImage);
        state.bPendingPresentationCapture = true;
        return true;
    }

    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = "AutomationScreenshotCapture";
    job->executeFn = [capturedSource = selectedRenderImage,
                      copyExecutor = state.copyExecutor,
                      readbackBuffer = state.readbackBuffer](ICommandBuffer* cmdBuf, RenderImage*) -> bool
    {
        if (!cmdBuf || !capturedSource || !copyExecutor || !readbackBuffer) {
            return false;
        }
        return executeScreenshotCopyGraph(*copyExecutor,
                                          *cmdBuf,
                                          *capturedSource,
                                          EImageLayout::ShaderReadOnlyOptimal,
                                          EImageLayout::ShaderReadOnlyOptimal,
                                          readbackBuffer,
                                          "AutomationScreenshot.ViewportCopy");
    };

    state.pendingJob = job;
    queueOffscreenJob(offscreenQueueService, render, state.pendingJob);
    return true;
}

bool AppScreenshotCapture::recordPresentationCapture(uint64_t frameIndex,
                                                     AppScreenshotCaptureState& state,
                                                     ICommandBuffer* cmdBuf)
{
    if (!cmdBuf || !state.bPendingPresentationCapture || state.target != EAutomationScreenshotTarget::Presentation || !state.readbackBuffer) {
        return false;
    }

    if (!state.presentationSourceImage || !state.presentationSourceImage->getImage()) {
        state.bFailed                     = true;
        state.bPendingPresentationCapture = false;
        return false;
    }

    const auto& sourceRenderImage = state.presentationSourceImage;
    if (!sourceRenderImage->getImageShared()) {
        state.bFailed                     = true;
        state.bPendingPresentationCapture = false;
        return false;
    }

    const Extent2D extent = sourceRenderImage->getExtent();
    if (extent.width != state.width || extent.height != state.height || sourceRenderImage->getFormat() != state.sourceFormat) {
        YA_CORE_ERROR("Presentation screenshot source changed before capture recording");
        state.bFailed                     = true;
        state.bPendingPresentationCapture = false;
        return false;
    }

    if (!state.copyExecutor ||
        !executeScreenshotCopyGraph(*state.copyExecutor,
                                    *cmdBuf,
                                    *sourceRenderImage,
                                    EImageLayout::PresentSrcKHR,
                                    EImageLayout::PresentSrcKHR,
                                    state.readbackBuffer,
                                    "AutomationScreenshot.PresentationCopy")) {
        state.bFailed                     = true;
        state.bPendingPresentationCapture = false;
        return false;
    }

    state.width                       = extent.width;
    state.height                      = extent.height;
    state.sourceFormat                = sourceRenderImage->getFormat();
    state.recordedFrameIndex          = frameIndex + 1;
    state.presentationSourceImage.reset();
    state.bPendingPresentationCapture = false;
    state.bPresentationCopyRecorded   = true;
    return true;
}

bool AppScreenshotCapture::tryFinalize(uint64_t currentFrameIndex, AppScreenshotCaptureState& state)
{
    if (state.bPresentationCopyRecorded) {
        if (currentFrameIndex <= state.recordedFrameIndex) {
            return false;
        }

        if (auto* vkBuffer = state.readbackBuffer ? dynamic_cast<VulkanBuffer*>(state.readbackBuffer.get()) : nullptr) {
            vmaInvalidateAllocation(vkBuffer->_render->getVmaAllocator(), vkBuffer->_allocation, 0, VK_WHOLE_SIZE);
        }

        state.bCompleted = writePngFromReadback(state);
        state.bFailed    = !state.bCompleted;
        state.readbackBuffer.reset();
        state.copyExecutor.reset();
        state.presentationSourceImage.reset();
        state.bPresentationCopyRecorded = false;
        return true;
    }

    if (!state.pendingJob) {
        return false;
    }
    if (state.pendingJob->phase == EOffscreenJobPhase::Pending ||
        state.pendingJob->phase == EOffscreenJobPhase::Queued ||
        state.pendingJob->phase == EOffscreenJobPhase::Recorded) {
        return false;
    }

    if (state.pendingJob->hasFailed() || !state.pendingJob->isGpuCompleted()) {
        YA_CORE_ERROR("Screenshot capture job did not complete successfully (phase={})",
                      static_cast<int>(state.pendingJob->phase));
        if (state.pendingJob->result) {
            state.pendingJob->result->outputImage = nullptr;
        }
        state.bFailed       = true;
        state.presentationSourceImage.reset();
        state.readbackBuffer.reset();
        state.copyExecutor.reset();
        state.pendingJob    = nullptr;
        return true;
    }

    if (auto* vkBuffer = state.readbackBuffer ? dynamic_cast<VulkanBuffer*>(state.readbackBuffer.get()) : nullptr) {
        vmaInvalidateAllocation(vkBuffer->_render->getVmaAllocator(), vkBuffer->_allocation, 0, VK_WHOLE_SIZE);
    }

    state.bCompleted = writePngFromReadback(state);
    state.bFailed    = !state.bCompleted;
    if (state.pendingJob->result) {
        state.pendingJob->result->outputImage = nullptr;
    }
    state.presentationSourceImage.reset();
    state.readbackBuffer.reset();
    state.copyExecutor.reset();
    state.pendingJob = nullptr;
    return true;
}

void AppScreenshotCapture::reset(AppScreenshotCaptureState& state)
{
    if (state.pendingJob) {
        cancelOffscreenJob(state.pendingJob);
    }
    state = {};
}

} // namespace ya
