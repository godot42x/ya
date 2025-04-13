#include "SDLGPURender2D.h"

void SDL::SDLRender2D::initQuadIndexBuffer()
{

    SDL_GPUTransferBuffer *indexTransferBuffer = SDLBuffer::createTransferBuffer(
        device,
        SDLBuffer::TransferBufferCreateInfo{
            .name  = "IndexTransferBuffer",
            .usage = SDLBuffer::TransferBufferCreateInfo::Upload,
            .size  = maxBatchIndexBufferElemSize * sizeof(uint32_t),
        });
    Uint32 *indicesPtr = (Uint32 *)SDL_MapGPUTransferBuffer(device, indexTransferBuffer, indexBuffer);

    if (pipeline.pipelineCreateInfo.frontFaceType == EFrontFaceType::ClockWise) {
        for (uint32_t i = 0; i < maxBatchIndexBufferElemSize / 6; i++) {
            indicesPtr[i * 6 + 0] = i * 4 + 0; // left top
            indicesPtr[i * 6 + 1] = i * 4 + 1; // right top
            indicesPtr[i * 6 + 2] = i * 4 + 2; // right bottom

            indicesPtr[i * 6 + 3] = i * 4 + 0; // left top
            indicesPtr[i * 6 + 4] = i * 4 + 2; // right bottom
            indicesPtr[i * 6 + 5] = i * 4 + 3; // left bottom
        }
    }
    else {
        for (uint32_t i = 0; i < maxBatchIndexBufferElemSize / 6; i++) {
            indicesPtr[i * 6 + 0] = i * 4 + 0; // left top
            indicesPtr[i * 6 + 1] = i * 4 + 2; // right bottom
            indicesPtr[i * 6 + 2] = i * 4 + 1; // right top

            indicesPtr[i * 6 + 3] = i * 4 + 0; // left top
            indicesPtr[i * 6 + 4] = i * 4 + 3; // left bottom
            indicesPtr[i * 6 + 5] = i * 4 + 2; // right bottom
        }
    }

    SDL_UnmapGPUTransferBuffer(device, indexTransferBuffer);

    auto commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    auto copyPass      = SDL_BeginGPUCopyPass(commandBuffer);
    {
        SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = indexTransferBuffer,
            .offset          = 0,
        };
        SDL_GPUBufferRegion destination = {
            .buffer = indexBuffer,
            .offset = 0,
        };
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    }
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    SDL_ReleaseGPUTransferBuffer(device, indexTransferBuffer);
}
