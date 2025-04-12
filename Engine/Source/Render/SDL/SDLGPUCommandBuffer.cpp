#include "SDLGPUCommandBuffer.h"


#include "SDLGPURender.h"

#include "Render/SDL/SDLTexture.h"

#include "Core/FileSystem/FileSystem.h"

namespace SDL {

GPUCommandBuffer_SDL::GPUCommandBuffer_SDL(GPURender_SDL *render, std::source_location &&loc) : CommandBuffer(std::move(loc))
{
    r             = render;
    commandBuffer = SDL_AcquireGPUCommandBuffer(render->device);
    NE_ASSERT(commandBuffer, "Failed to create command buffer {}", SDL_GetError());
}

std::shared_ptr<Texture> GPUCommandBuffer_SDL::createTexture(std::string_view filepath)
{
    auto         path    = FileSystem::get()->getProjectRoot() / filepath;
    SDL_Surface *surface = nullptr;

    // ut::file::ImageInfo imageInfo = ut::file::ImageInfo::detect(path);

    surface = IMG_Load(path.string().c_str());
    if (!surface) {
        NE_CORE_ERROR("Failed to load image: {}", SDL_GetError());
        return nullptr;
    }

    SDL_GPUTexture *outTexture = nullptr;

    SDL_GPUTextureCreateInfo info{
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = static_cast<Uint32>(surface->w),
        .height               = static_cast<Uint32>(surface->h),
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };
    outTexture = SDL_CreateGPUTexture(r->device, &info);
    if (!outTexture) {
        NE_CORE_ERROR("Failed to create texture: {}", SDL_GetError());
        return nullptr;
    }
    auto filename = std::format("{} 😜", path.stem().filename().string());
    SDL_SetGPUTextureName(r->device, outTexture, filename.c_str());
    NE_CORE_INFO("Texture name: {}", filename.c_str());

    uploadTexture(outTexture, surface->pixels, surface->w, surface->h);

    SDL_DestroySurface(surface);

    return std::make_shared<SDLTexture>(
        outTexture,
        info.width,
        info.height,
        SDLTexture::ConvertFromSDLFormat(info.format),
        SDLTexture::ConvertFromSDLType(info.type),
        filename);
}

std::shared_ptr<Texture> GPUCommandBuffer_SDL::createTextureFromBuffer(const void *data, Uint32 width, Uint32 height, const char *name)
{
    SDL_GPUTexture *outTexture = nullptr;

    SDL_GPUTextureCreateInfo info{
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = width,
        .height               = height,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };
    outTexture = SDL_CreateGPUTexture(r->device, &info);
    if (!outTexture) {
        NE_CORE_ERROR("Failed to create texture: {}", SDL_GetError());
        return nullptr;
    }

    if (name) {
        SDL_SetGPUTextureName(r->device, outTexture, name);
        NE_CORE_INFO("Texture name: {}", name);
    }

    uploadTexture(outTexture, (void *)data, width, height);

    return std::make_shared<SDLTexture>(
        outTexture,
        info.width,
        info.height,
        SDLTexture::ConvertFromSDLFormat(info.format),
        SDLTexture::ConvertFromSDLType(info.type),
        name ? name : "Unnamed Texture");
}

void GPUCommandBuffer_SDL::uploadTexture(SDL_GPUTexture *texture, void *data, uint32_t w, uint32_t h)
{

    SDL_GPUTransferBuffer *textureTransferBuffer = nullptr;
    {
        SDL_GPUTransferBufferCreateInfo textureTransferBufferInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = static_cast<Uint32>(w * h * 4), // 4 bytes per pixel (R8G8B8A8)
            .props = 0,
        };
        textureTransferBuffer = SDL_CreateGPUTransferBuffer(r->device, &textureTransferBufferInfo);

        // mmap
        void *mmapPtr = SDL_MapGPUTransferBuffer(r->device, textureTransferBuffer, false);
        std::memcpy(mmapPtr, data, textureTransferBufferInfo.size);
        SDL_UnmapGPUTransferBuffer(r->device, textureTransferBuffer);
    }

    // [upload] copy pass
    {
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commandBuffer);

        // transfer texture
        {
            SDL_GPUTextureTransferInfo srcTransferInfo = {
                .transfer_buffer = textureTransferBuffer,
                .offset          = 0,
            };
            SDL_GPUTextureRegion destGPUTextureRegion = {
                .texture   = texture,
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
        SDL_ReleaseGPUTransferBuffer(r->device, textureTransferBuffer);
    }
}

void GPUCommandBuffer_SDL::setVertexUniforms(uint32_t slot_index, void *data, uint32_t dataSize)
{
    SDL_PushGPUVertexUniformData(commandBuffer, slot_index, data, dataSize);
}


void GPUCommandBuffer_SDL::setFragmentUniforms(uint32_t slot_index, void *data, uint32_t dataSize)
{
    SDL_PushGPUFragmentUniformData(commandBuffer, slot_index, data, dataSize);
}



void GPUCommandBuffer_SDL::uploadVertexBuffers(const void *vertexData, uint32_t vertexDataSize)
{
    SDL_GPUTransferBuffer *vertexTransferBuffer;
    {
        SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = vertexDataSize,
            .props = 0,
        };
        vertexTransferBuffer = SDL_CreateGPUTransferBuffer(r->device, &transferBufferInfo);
        NE_ASSERT(vertexTransferBuffer, "Failed to create vertex transfer buffer {}", SDL_GetError());

        void *mmapData = SDL_MapGPUTransferBuffer(r->device, vertexTransferBuffer, false);
        NE_ASSERT(mmapData, "Failed to map vertex transfer buffer {}", SDL_GetError());
        std::memcpy(mmapData, vertexData, vertexDataSize);
        SDL_UnmapGPUTransferBuffer(r->device, vertexTransferBuffer);
    }

    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    NE_ASSERT(copyPass, "Failed to begin copy pass {}", SDL_GetError());

    SDL_GPUTransferBufferLocation sourceLoc = {
        .transfer_buffer = vertexTransferBuffer,
        .offset          = 0,
    };
    SDL_GPUBufferRegion destRegion = {
        .buffer = r->vertexBuffer,
        .offset = 0,
        .size   = vertexDataSize,
    };
    SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_ReleaseGPUTransferBuffer(r->device, vertexTransferBuffer);
}

void GPUCommandBuffer_SDL::uploadIndexBuffers(const void *indexData, uint32_t indexDataSize)
{
    SDL_GPUTransferBuffer *indexTransferBuffer;
    {
        SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = indexDataSize,
            .props = 0,
        };
        indexTransferBuffer = SDL_CreateGPUTransferBuffer(r->device, &transferBufferInfo);
        NE_ASSERT(indexTransferBuffer, "Failed to create index transfer buffer {}", SDL_GetError());

        void *mmapData = SDL_MapGPUTransferBuffer(r->device, indexTransferBuffer, false);
        NE_ASSERT(mmapData, "Failed to map index transfer buffer {}", SDL_GetError());
        std::memcpy(mmapData, indexData, indexDataSize);
        SDL_UnmapGPUTransferBuffer(r->device, indexTransferBuffer);
    }

    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    NE_ASSERT(copyPass, "Failed to begin copy pass {}", SDL_GetError());

    SDL_GPUTransferBufferLocation sourceLoc = {
        .transfer_buffer = indexTransferBuffer,
        .offset          = 0,
    };
    SDL_GPUBufferRegion destRegion = {
        .buffer = r->indexBuffer,
        .offset = 0,
        .size   = indexDataSize,
    };
    SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_ReleaseGPUTransferBuffer(r->device, indexTransferBuffer);
}

} // namespace SDL
