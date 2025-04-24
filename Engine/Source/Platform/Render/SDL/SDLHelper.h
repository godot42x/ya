#pragma once

#include "Render/CommandBuffer.h"
#include "Render/Device.h"
#include "SDL3/SDL_gpu.h"
#include "SDLBuffers.h"



struct SDLHelper
{

    static void uploadTexture(SDL_GPUDevice *sdlDevice, SDL_GPUCommandBuffer *sdlCommandBUffer, SDL_GPUTexture *sdlTexture, void *data, uint32_t w, uint32_t h)
    {
        auto textureTransferBuffer = SDLGPUTransferBuffer::Create(sdlDevice,
                                                                  "Temp transferBuffer for texture upload",
                                                                  SDLGPUTransferBuffer::Usage::Upload,
                                                                  w * h * 4);

        // mmap
        void *mmapPtr = SDL_MapGPUTransferBuffer(sdlDevice, textureTransferBuffer->getBuffer(), false);
        std::memcpy(mmapPtr, data, textureTransferBuffer->getSize());
        SDL_UnmapGPUTransferBuffer(sdlDevice, textureTransferBuffer->getBuffer());

        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(sdlCommandBUffer);
        // transfer texture
        {
            SDL_GPUTextureTransferInfo srcTransferInfo = {
                .transfer_buffer = textureTransferBuffer->getBuffer(),
                .offset          = 0,
            };
            SDL_GPUTextureRegion destGPUTextureRegion = {
                .texture   = sdlTexture,
                .mip_level = 0,
                .layer     = 0,
                .x         = 0,
                .y         = 0,
                .z         = 0,
                .w         = w,
                .h         = h,
                .d         = 1,
            };

            SDL_UploadToGPUTexture(copyPass, &srcTransferInfo, &destGPUTextureRegion, false);
        }
        SDL_EndGPUCopyPass(copyPass);
    }


    static void uploadVertexBuffers(SDL_GPUDevice *sdlDevice, SDL_GPUCommandBuffer *sdlCommandBuffer, std::shared_ptr<SDLGPUBuffer> buffer, uint32_t offset, const void *vertexData, uint32_t vertexDataSize)
    {
        auto vertexTransferBuffer = SDLGPUTransferBuffer::Create(sdlDevice,
                                                                 "Temp transferBuffer for vertex upload",
                                                                 SDLGPUTransferBuffer::Usage::Upload,
                                                                 vertexDataSize);
        NE_ASSERT(vertexTransferBuffer->getBuffer(), "Failed to create vertex transfer buffer {}", SDL_GetError());
        {

            void *mmapData = SDL_MapGPUTransferBuffer(sdlDevice, vertexTransferBuffer->getBuffer(), false);
            NE_ASSERT(mmapData, "Failed to map vertex transfer buffer {}", SDL_GetError());
            std::memcpy(mmapData, vertexData, vertexDataSize);
            SDL_UnmapGPUTransferBuffer(sdlDevice, vertexTransferBuffer->getBuffer());
        }

        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(sdlCommandBuffer);
        NE_ASSERT(copyPass, "Failed to begin copy pass {}", SDL_GetError());

        SDL_GPUTransferBufferLocation sourceLoc = {
            .transfer_buffer = vertexTransferBuffer->getBuffer(),
            .offset          = 0,
        };
        SDL_GPUBufferRegion destRegion = {
            .buffer = buffer->getBuffer(),
            .offset = offset,
            .size   = vertexDataSize,
        };
        SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);

        SDL_EndGPUCopyPass(copyPass);
        // vertexTransferBuffer.clean();
    }

    void uploadIndexBuffers(SDL_GPUDevice *sdlDevice, SDL_GPUCommandBuffer *sdlCommandBuffer, std::shared_ptr<SDLGPUBuffer> buffer, uint32_t offset, const void *indexData, uint32_t indexDataSize)
    {


        auto indexTransferBuffer = SDLGPUTransferBuffer::Create(sdlDevice,
                                                                "Temp transferBuffer for index upload",
                                                                SDLGPUTransferBuffer::Usage::Upload,
                                                                indexDataSize);


        void *mmapData = SDL_MapGPUTransferBuffer(sdlDevice, indexTransferBuffer->getBuffer(), false);
        NE_ASSERT(mmapData, "Failed to map index transfer buffer {}", SDL_GetError());
        std::memcpy(mmapData, indexData, indexDataSize);
        SDL_UnmapGPUTransferBuffer(sdlDevice, indexTransferBuffer->getBuffer());

        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(sdlCommandBuffer);
        NE_ASSERT(copyPass, "Failed to begin copy pass {}", SDL_GetError());

        SDL_GPUTransferBufferLocation sourceLoc = {
            .transfer_buffer = indexTransferBuffer->getBuffer(),
            .offset          = 0,
        };
        SDL_GPUBufferRegion destRegion = {
            .buffer = buffer->getBuffer(),
            .offset = offset,
            .size   = indexDataSize,
        };
        SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);

        SDL_EndGPUCopyPass(copyPass);
        // indexTransferBuffer.clean();
    }
};