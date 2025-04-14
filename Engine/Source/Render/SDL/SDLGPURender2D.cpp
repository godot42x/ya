#include "SDLGPURender2D.h"

void SDL::SDLRender2D::initQuadIndexBuffer()
{
    // Ensure the index buffer is large enough using tryExtendSize
    if (!indexBufferPtr) {
        indexBufferPtr = SDLGPUBuffer::Create(*device, "IndexBuffer", 
                                             SDLGPUBuffer::Usage::IndexBuffer, 
                                             maxBatchIndexBufferElemSize * sizeof(uint32_t));
    } else {
        indexBufferPtr->tryExtendSize("IndexBuffer", SDLGPUBuffer::Usage::IndexBuffer,
                                     maxBatchIndexBufferElemSize * sizeof(uint32_t));
    }

    // Create a transfer buffer to upload the index data
    auto indexTransferBufferPtr = SDLGPUTransferBuffer::Create(
        *device,
        "IndexTransferBuffer",
        SDLGPUTransferBuffer::Usage::Upload,
        maxBatchIndexBufferElemSize * sizeof(uint32_t)
    );
    
    if (!indexTransferBufferPtr || !indexBufferPtr) {
        NE_CORE_ERROR("Failed to create buffers for quad index initialization");
        return;
    }
    
    // Map the transfer buffer
    Uint32* indicesPtr = (Uint32*)SDL_MapGPUTransferBuffer(device, indexTransferBufferPtr->getBuffer(), true);
    
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

    SDL_UnmapGPUTransferBuffer(device, indexTransferBufferPtr->getBuffer());

    auto commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    auto copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    {
        SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = indexTransferBufferPtr->getBuffer(),
            .offset = 0,
        };
        SDL_GPUBufferRegion destination = {
            .buffer = indexBufferPtr->getBuffer(),
            .offset = 0,
        };
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    }
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    
    // indexTransferBufferPtr will be automatically released when it goes out of scope
}
