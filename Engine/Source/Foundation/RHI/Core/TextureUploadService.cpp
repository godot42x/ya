#include "TextureUploadService.h"

#include "Foundation/Core/Log.h"
#include "Foundation/RHI/Core/Buffer.h"
#include "Foundation/RHI/Core/CommandBuffer.h"
#include "Foundation/RHI/Core/Image.h"
#include "Foundation/RHI/Render.h"

#include <format>

namespace ya
{

bool TextureUploadService::upload(IRender& render, const TextureUploadRequest& request, uint32_t* outMipLevels)
{
    if (!request.image || !request.image->getHandle()) {
        YA_CORE_ERROR("TextureUploadService: upload for '{}' has no valid image", request.label);
        return false;
    }
    if (!request.staging) {
        YA_CORE_ERROR("TextureUploadService: upload for '{}' has no staging buffer", request.label);
        return false;
    }
    if (request.regions.empty()) {
        YA_CORE_ERROR("TextureUploadService: upload for '{}' has no copy regions", request.label);
        return false;
    }

    ICommandBuffer* cmdBuf = render.beginIsolateCommands(
        request.label.empty() ? "TextureUpload" : std::format("TextureUpload:{}", request.label));
    if (!cmdBuf) {
        YA_CORE_ERROR("TextureUploadService: failed to open isolate command scope for '{}'", request.label);
        return false;
    }

    const ImageSubresourceRange* uploadRange = request.uploadRange.has_value() ? &*request.uploadRange : nullptr;
    cmdBuf->transitionImageLayout(request.image.get(), EImageLayout::Undefined, EImageLayout::TransferDst, uploadRange);

    for (const auto& region : request.regions) {
        cmdBuf->copyBufferToImage(request.staging.get(), request.image.get(), EImageLayout::TransferDst, {region});
    }

    uint32_t uploadedMipLevels = request.image->getMipLevels();
    if (request.bGenerateMipmaps) {
        if (!cmdBuf->generateMipmaps(request.image.get(), EImageLayout::TransferDst, request.finalLayout)) {
            YA_CORE_ERROR("TextureUploadService: GPU mip generation failed for '{}'; keeping base level only", request.label);
            const ImageSubresourceRange baseLevelRange{
                .aspectMask     = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            };
            cmdBuf->transitionImageLayout(request.image.get(), EImageLayout::TransferDst, request.finalLayout, &baseLevelRange);
            uploadedMipLevels = 1;
        }
    }
    else {
        const ImageSubresourceRange* finalizeRange = request.finalizeRange.has_value() ? &*request.finalizeRange : nullptr;
        cmdBuf->transitionImageLayout(request.image.get(), EImageLayout::TransferDst, request.finalLayout, finalizeRange);
    }

    render.endIsolateCommands(cmdBuf);

    if (outMipLevels) {
        *outMipLevels = uploadedMipLevels;
    }
    return true;
}

} // namespace ya
