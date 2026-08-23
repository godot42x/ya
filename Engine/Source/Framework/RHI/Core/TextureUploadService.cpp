#include "TextureUploadService.h"

#include "Core/Log.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/Image.h"
#include "RHI/Render.h"

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

bool TextureUploadService::updateRegion(IRender& render, const TextureRegionUpdateRequest& request)
{
    if (!request.image || !request.image->getHandle()) {
        YA_CORE_ERROR("TextureUploadService: updateRegion for '{}' has no valid image", request.label);
        return false;
    }
    if (!request.staging) {
        YA_CORE_ERROR("TextureUploadService: updateRegion for '{}' has no staging buffer", request.label);
        return false;
    }

    ICommandBuffer* cmdBuf = render.beginIsolateCommands(
        request.label.empty() ? "TextureUpdateRegion" : std::format("TextureUpdateRegion:{}", request.label));
    if (!cmdBuf) {
        YA_CORE_ERROR("TextureUploadService: failed to open isolate command scope for '{}'", request.label);
        return false;
    }

    // ShaderReadOnlyOptimal -> TransferDst -> ShaderReadOnlyOptimal. No mipmaps.
    cmdBuf->transitionImageLayout(request.image.get(), request.currentLayout, EImageLayout::TransferDst);
    cmdBuf->copyBufferToImage(request.staging.get(), request.image.get(), EImageLayout::TransferDst, {request.region});
    cmdBuf->transitionImageLayout(request.image.get(), EImageLayout::TransferDst, EImageLayout::ShaderReadOnlyOptimal);

    render.endIsolateCommands(cmdBuf);
    return true;
}

bool TextureUploadService::updateRegions(IRender& render, const TextureRegionUpdateBatch& batch)
{
    if (!batch.image || !batch.image->getHandle()) {
        YA_CORE_ERROR("TextureUploadService: updateRegions for '{}' has no valid image", batch.label);
        return false;
    }
    if (!batch.staging) {
        YA_CORE_ERROR("TextureUploadService: updateRegions for '{}' has no staging buffer", batch.label);
        return false;
    }
    if (batch.regions.empty()) {
        return false; // nothing to do; not an error
    }

    ICommandBuffer* cmdBuf = render.beginIsolateCommands(
        batch.label.empty() ? "TextureUpdateRegions" : std::format("TextureUpdateRegions:{}", batch.label));
    if (!cmdBuf) {
        YA_CORE_ERROR("TextureUploadService: failed to open isolate command scope for '{}'", batch.label);
        return false;
    }

    // One layout transition for the whole batch, then all copies in a single
    // submit. Callers that know they changed many cells should always batch
    // instead of calling updateRegion() per cell.
    cmdBuf->transitionImageLayout(batch.image.get(), batch.currentLayout, EImageLayout::TransferDst);
    cmdBuf->copyBufferToImage(batch.staging.get(), batch.image.get(), EImageLayout::TransferDst, batch.regions);
    cmdBuf->transitionImageLayout(batch.image.get(), EImageLayout::TransferDst, EImageLayout::ShaderReadOnlyOptimal);

    render.endIsolateCommands(cmdBuf);
    return true;
}

} // namespace ya
