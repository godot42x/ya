#pragma once
#include "Core/Log.h"
#include "SDL3/SDL_gpu.h"
#include <string>


struct SDLBuffer
{

    struct BufferCreateInfo
    {
        enum EUsage
        {
            VertexBuffer,
            IndexBuffer,
            // TODO: support more usages...
        };

        std::string name;
        EUsage      usage;
        std::size_t size;
    };

    static SDL_GPUBuffer *createBuffer(SDL_GPUDevice *device, const BufferCreateInfo &bufferInfo)
    {
        SDL_GPUBufferCreateInfo bufferCreateInfo = {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size  = static_cast<Uint32>(bufferInfo.size),
            .props = 0, // by comment
        };

        if (bufferInfo.usage == BufferCreateInfo::IndexBuffer) {
            bufferCreateInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        }

        auto buffer = SDL_CreateGPUBuffer(device, &bufferCreateInfo);
        NE_CORE_ASSERT(buffer, "Failed to create buffer {}", SDL_GetError());

        SDL_SetGPUBufferName(device, buffer, bufferInfo.name.c_str());

        return buffer;
    }


    struct TransferBufferCreateInfo
    {
        enum EUsage
        {
            Upload,
            Download,
        };

        std::string name;
        EUsage      usage;
        std::size_t size;
    };

    static SDL_GPUTransferBuffer *createTransferBuffer(SDL_GPUDevice *device, const TransferBufferCreateInfo &transferBCI)
    {
        SDL_GPUTransferBufferCreateInfo sdlTransferBCI = {
            .size  = static_cast<Uint32>(transferBCI.size),
            .props = 0, // by comment
        };

        switch (transferBCI.usage) {
        case TransferBufferCreateInfo::Upload:
            sdlTransferBCI.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            break;
        case TransferBufferCreateInfo::Download:
            sdlTransferBCI.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
            break;
        default:
            NE_CORE_ASSERT(false, "Invalid transfer buffer usage {}", (int)transferBCI.usage);
        }

        auto buffer = SDL_CreateGPUTransferBuffer(device, &sdlTransferBCI);
        NE_ASSERT(buffer, "Failed to create transfer buffer {}", SDL_GetError());

        // no supported, so it's a buffer at cpu side?
        // SDL_SetGPUBufferName(device, buffer, transferBCI.name.c_str());

        return buffer;
    }
};