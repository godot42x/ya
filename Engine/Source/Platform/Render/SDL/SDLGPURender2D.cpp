#include "SDLGPURender2D.h"

namespace SDL
{

void SDLRender2D::fillQuadIndicesToGPUBuffer(SDLGPUBufferPtr indexBuffer, std::size_t indicesSize, std::size_t bufferSize)
{
    NE_CORE_TRACE("Fill quad indices to GPU buffer: {0} bytes, {1} indices", bufferSize, indicesSize);
    NE_CORE_ASSERT(indicesSize > 0 && (indicesSize * sizeof(Uint32) == bufferSize || indicesSize * sizeof(Uint16) == bufferSize),
                   "Invalid index buffer size. Expected size is {0} or {1}, but got {2}",
                   indicesSize * sizeof(Uint32),
                   indicesSize * sizeof(Uint16),
                   bufferSize);

    indexBuffer->tryExtendSize(bufferSize);

    // Create a transfer buffer to upload the index data
    auto indexTransferBufferPtr = SDLGPUTransferBuffer::Create(
        device,
        "Render2D IndexTransferBuffer",
        SDLGPUTransferBuffer::Usage::Upload,
        bufferSize);

    if (!indexTransferBufferPtr || !indexBufferPtr) {
        NE_CORE_ERROR("Failed to create buffers for quad index initialization");
        return;
    }

    // Map the transfer buffer
    Uint32 *indicesPtr = (Uint32 *)SDL_MapGPUTransferBuffer(device, indexTransferBufferPtr->getBuffer(), true);

    if (pipeline.pipelineCreateInfo.frontFaceType == EFrontFaceType::ClockWise) {
        for (uint32_t i = 0; i < indicesSize / 6; i++) {
            indicesPtr[i * 6 + 0] = i * 4 + 0; // left top
            indicesPtr[i * 6 + 1] = i * 4 + 1; // right top
            indicesPtr[i * 6 + 2] = i * 4 + 3; // right bottom

            indicesPtr[i * 6 + 3] = i * 4 + 0; // left top
            indicesPtr[i * 6 + 4] = i * 4 + 3; // right bottom
            indicesPtr[i * 6 + 5] = i * 4 + 2; // left bottom
        }
    }
    else {
        for (uint32_t i = 0; i < indicesSize / 6; i++) {
            indicesPtr[i * 6 + 0] = i * 4 + 0; // left top
            indicesPtr[i * 6 + 1] = i * 4 + 3; // right bottom
            indicesPtr[i * 6 + 2] = i * 4 + 1; // right top

            indicesPtr[i * 6 + 3] = i * 4 + 0; // left top
            indicesPtr[i * 6 + 4] = i * 4 + 2; // left bottom
            indicesPtr[i * 6 + 5] = i * 4 + 3; // right bottom
        }
    }

    SDL_UnmapGPUTransferBuffer(device, indexTransferBufferPtr->getBuffer());

    auto commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    auto copyPass      = SDL_BeginGPUCopyPass(commandBuffer);
    {
        SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = indexTransferBufferPtr->getBuffer(),
            .offset          = 0,
        };
        SDL_GPUBufferRegion destination = {
            .buffer = indexBufferPtr->getBuffer(),
            .offset = 0,
            .size   = static_cast<Uint32>(bufferSize),
        };
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    }
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commandBuffer);


    // indexTransferBufferPtr will be automatically released when it goes out of scope
}
} // namespace SDL
