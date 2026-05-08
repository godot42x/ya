#include "Runtime/App/Utility/AppScreenshotCapture.h"

#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/Utility/OffscreenJobRunner.h"

#include "Core/Log.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanMemoryAllocator.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/OffscreenJob.h"
#include "Render/Core/Texture.h"

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

Texture* resolveScreenshotSourceTexture(App& app)
{
    if (Texture* texture = app.getPostprocessOutputTexture()) {
        return texture;
    }

    auto* renderRuntime = app.getRenderRuntime();
    return renderRuntime ? renderRuntime->getActiveViewportTexture() : nullptr;
}

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

bool AppScreenshotCapture::request(App& app, AppScreenshotCaptureState& state, const std::string& outputPath)
{
    auto* renderRuntime = app.getRenderRuntime();
    auto* render        = app.getRender();
    if (!renderRuntime || !render || outputPath.empty()) {
        return false;
    }
    if (render->getAPI() != ERenderAPI::Vulkan) {
        YA_CORE_WARN("Screenshot automation currently supports Vulkan only");
        return false;
    }
    if (state.pendingJob || state.bCompleted) {
        return false;
    }

    Texture* sourceTexture = resolveScreenshotSourceTexture(app);
    if (!sourceTexture || !sourceTexture->getImage()) {
        return false;
    }

    const std::shared_ptr<IImage> sourceImage = sourceTexture->getImageShared();
    if (!sourceImage) {
        return false;
    }

    const Extent2D extent = sourceTexture->getExtent();
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }
    if (!isSupportedScreenshotFormat(sourceTexture->getFormat())) {
        YA_CORE_WARN("Unsupported screenshot source format {}", static_cast<int>(sourceTexture->getFormat()));
        state.bFailed = true;
        return false;
    }

    const uint32_t bytesPerPixel = sourceTexture->getFormat() == EFormat::R16G16B16A16_SFLOAT ? 8u : 4u;
    const uint32_t bufferSize    = extent.width * extent.height * bytesPerPixel;
    auto readbackBuffer          = IBuffer::create(render, BufferCreateInfo{
        .label       = "AutomationScreenshotReadback",
        .usage       = EBufferUsage::TransferDst,
        .size        = bufferSize,
        .memoryUsage = EMemoryUsage::GpuToCpu,
    });
    if (!readbackBuffer) {
        YA_CORE_ERROR("Failed to create screenshot readback buffer");
        state.bFailed = true;
        return false;
    }

    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = "AutomationScreenshotCapture";
    job->createOutputFn = [](IRender*) -> stdptr<Texture>
    {
        return Texture::createRenderTexture(RenderTextureCreateInfo{
            .label   = "AutomationScreenshotScratch",
            .width   = 1,
            .height  = 1,
            .format  = EFormat::R8G8B8A8_UNORM,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
            .isDepth = false,
        });
    };
    job->executeFn = [sourceImage, readbackBuffer, extent](ICommandBuffer* cmdBuf, Texture*) -> bool
    {
        if (!cmdBuf || !sourceImage || !readbackBuffer) {
            return false;
        }

        cmdBuf->transitionImageLayoutAuto(sourceImage.get(), EImageLayout::TransferSrc);
        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = {0, 0, 0};
        region.imageExtent                     = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(cmdBuf->getHandleAs<VkCommandBuffer>(),
                               sourceImage->getHandle().as<VkImage>(),
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readbackBuffer->getHandleAs<VkBuffer>(),
                               1,
                               &region);
        cmdBuf->bufferMemoryBarrier(readbackBuffer.get(),
                                    EPipelineStage::Transfer,
                                    EPipelineStage::AllCommands,
                                    EResourceAccess::TransferWrite,
                                    EResourceAccess::TransferRead,
                                    0,
                                    readbackBuffer->getSize());
        cmdBuf->transitionImageLayoutAuto(sourceImage.get(), EImageLayout::ShaderReadOnlyOptimal);
        return true;
    };

    state.outputPath     = outputPath;
    state.readbackBuffer = std::move(readbackBuffer);
    state.pendingJob     = job;
    state.width          = extent.width;
    state.height         = extent.height;
    state.sourceFormat   = sourceTexture->getFormat();
    state.bCompleted     = false;
    state.bFailed        = false;

    queueOffscreenJob(&app, render, state.pendingJob);
    return true;
}

bool AppScreenshotCapture::tryFinalize(AppScreenshotCaptureState& state)
{
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
            state.pendingJob->result->outputTexture = nullptr;
        }
        state.bFailed       = true;
        state.readbackBuffer.reset();
        state.pendingJob    = nullptr;
        return true;
    }

    if (auto* vkBuffer = state.readbackBuffer ? dynamic_cast<VulkanBuffer*>(state.readbackBuffer.get()) : nullptr) {
        vmaInvalidateAllocation(vkBuffer->_render->getVmaAllocator(), vkBuffer->_allocation, 0, VK_WHOLE_SIZE);
    }

    state.bCompleted = writePngFromReadback(state);
    state.bFailed    = !state.bCompleted;
    if (state.pendingJob->result) {
        state.pendingJob->result->outputTexture = nullptr;
    }
    state.readbackBuffer.reset();
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
