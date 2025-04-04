#pragma once

#include <any>
#include <optional>
#include <vector>


#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>



#include "Core/FileSystem/FileSystem.h"
#include "Render/Shader.h"


#include "reflect.cc/enum.h"


struct RenderAPI
{
    enum EAPIType
    {
        None = 0,
        OpenGL,
        Vulkan,
        DirectX12,
        Metal,
        SDL3GPU, // SDL3 GPU backend
        ENUM_MAX,
    };
};

struct Render;

struct GPUCommandBuffer_SDL;



struct GPUCommandBuffer
{
    void                *commandBuffer = nullptr;
    std::source_location location;

    GPUCommandBuffer(std::source_location &&loc) : location(std::move(loc))
    {
    }

    // disable copy
    GPUCommandBuffer(const GPUCommandBuffer &)            = delete;
    GPUCommandBuffer &operator=(const GPUCommandBuffer &) = delete;

    virtual ~GPUCommandBuffer() {}

    void ensureSubmitted()
    {
        if (commandBuffer != nullptr)
        {
            NE_CORE_WARN("commandBuffer was not explicitly submitted, auto-submitting in destructor. Buffer acquired at {}:{}",
                         location.file_name(),
                         location.line());
            submit();
        }
    }


    virtual bool submit()
    {
        NE_CORE_ASSERT(false, "GPUCommandBuffer::submit() is not implemented!");
        return false;
    }

    // temp code there; FIX ME
    operator SDL_GPUCommandBuffer *()
    {
        NE_CORE_ASSERT(commandBuffer, "commandBuffer is nullptr!");
        return (SDL_GPUCommandBuffer *)commandBuffer;
    }

    virtual void setUniforms(Uint32 slot_index, void *data, Uint32 dataSize)
    {
        NE_CORE_ASSERT(false, "GPUCommandBuffer::setUniforms() is not implemented!");
    }
};

struct GPUCommandBuffer_SDL : public GPUCommandBuffer
{
    GPUCommandBuffer_SDL(SDL_GPUDevice *device, std::source_location &&loc) : GPUCommandBuffer(std::move(loc))
    {
        NE_ASSERT(device, "render and device is invalid");
        commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        NE_ASSERT(commandBuffer, "Failed to create command buffer {}", SDL_GetError());
    }


    bool submit() override
    {
        NE_CORE_ASSERT(commandBuffer != nullptr,
                       "commandBuffer is already submitted! buffer acquired at {}:{}",
                       location.file_name(),
                       location.line());

        if (!SDL_SubmitGPUCommandBuffer((SDL_GPUCommandBuffer *)commandBuffer)) {
            NE_CORE_ERROR("Failed to submit command buffer {}", SDL_GetError());
            return false;
        }
        commandBuffer = nullptr; // reset command buffer to null, so we can acquire a new one
        return true;
    }

    ~GPUCommandBuffer_SDL()
    {
        ensureSubmitted();
    }
};



enum class ESamplerType
{
    DefaultLinear = 0,
    DefaultNearest,
    PointClamp,
    PointWrap,
    LinearClamp,
    LinearWrap,
    AnisotropicClamp,
    AnisotropicWrap,
    ENUM_MAX,
};
GENERATED_ENUM_MISC(ESamplerType);


enum class EGraphicPipeLinePrimitiveType
{
    TriangleList,
    ENUM_MAX = 0,
};


struct VertexBufferDescription
{
    uint32_t slot;
    uint32_t pitch;
};


namespace EVertexAttributeFormat
{
enum T
{
    Float2 = 0,
    Float3,
    Float4,
    ENUM_MAX,
};

std::size_t T2Size(T type);

GENERATED_ENUM_MISC(T);
}; // namespace EVertexAttributeFormat



struct VertexAttribute
{
    uint32_t                  location;
    uint32_t                  bufferSlot;
    EVertexAttributeFormat::T format;
    uint32_t                  offset;
};


struct ShaderCreateInfo
{
    std::string shaderName; // we use single glsl now
    uint32_t    numUniformBuffers = 0;
    uint32_t    numSamplers       = 0;
};

struct GraphicsPipelineCreateInfo
{
    ShaderCreateInfo                     shaderCreateInfo;
    std::vector<VertexBufferDescription> vertexBufferDescs;
    std::vector<VertexAttribute>         vertexAttributes;
    EGraphicPipeLinePrimitiveType        primitiveType = EGraphicPipeLinePrimitiveType::TriangleList;
};

struct Render
{
    bool  bLastCommandBufferConsumed = true;
    void *device;

    // temp code there; REMOVE ME!!
    operator SDL_GPUDevice *()
    {
        NE_CORE_ASSERT(device, "device is nullptr!");
        return (SDL_GPUDevice *)device;
    }
};

struct SDLGPURender : public Render
{
    SDL_GPUDevice                                     *device;
    SDL_Window                                        *window;
    SDL_GPUGraphicsPipeline                           *pipeline;
    SDL_GPUBuffer                                     *vertexBuffer;
    SDL_GPUBuffer                                     *indexBuffer;
    std::unordered_map<ESamplerType, SDL_GPUSampler *> samplers;

    uint32_t maxVertexBufferElemSize = 10000;
    uint32_t maxIndexBufferElemSize  = 10000;
    uint32_t vertexInputSize         = -1;

    uint32_t getVertexBufferSize() const
    {
        return maxVertexBufferElemSize * vertexInputSize;
    }
    uint32_t getIndexBufferSize() const
    {
        return maxIndexBufferElemSize * sizeof(uint32_t);
    }

    bool init()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
            return false;
        }

        int n = SDL_GetNumGPUDrivers();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%d Available drivers: ", n);
        for (int i = 0; i < n; ++i) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetGPUDriver(i));
        }

        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                         SDL_GPU_SHADERFORMAT_DXIL |
                                         SDL_GPU_SHADERFORMAT_MSL,
                                     true,
                                     nullptr);
        if (!device) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create GPU device: %s", SDL_GetError());
            return false;
        }

        const char *driver = SDL_GetGPUDeviceDriver(device);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Choosen GPU Driver: %s", driver);

        window = SDL_CreateWindow("Neon", 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create window: %s", SDL_GetError());
            return false;
        }

        // swapchain just be created here, change its parameter after creation
        if (!SDL_ClaimWindowForGPUDevice(device, window)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
            return false;
        }


        // default is vsync, we can change it to immediate or mailbox
        // SDL_SetGPUSwapchainParameters(device,
        //                               window,
        //                               SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        //                               SDL_GPU_PRESENTMODE_VSYNC);

        createSamplers();
        return true;
    }


    bool createGraphicsPipeline(const GraphicsPipelineCreateInfo &info);

    void cleanContext()
    {
        // clear samplers
        for (auto &[key, sampler] : samplers) {
            if (!sampler) {
                continue;
            }
            SDL_ReleaseGPUSampler(device, sampler);
        }


        if (vertexBuffer) {
            SDL_ReleaseGPUBuffer(device, vertexBuffer);
        }
        if (indexBuffer) {
            SDL_ReleaseGPUBuffer(device, indexBuffer);
        }
        if (pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        }

        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(device);
    }

    std::optional<std::tuple<SDL_GPUShader *, SDL_GPUShader *>> createShaders(const ShaderCreateInfo &shaderCI);

    SDL_GPUTexture *createTexture(std::shared_ptr<GPUCommandBuffer> commandBuffer, std::string_view filepath);
    SDL_GPUTexture *createTextureByBuffer(std::shared_ptr<GPUCommandBuffer> commandBuffer, const void *data, Uint32 width, Uint32 height, const char *name);
    void            uploadTexture(std::shared_ptr<GPUCommandBuffer> commandBuffer, SDL_GPUTexture *texture, void *data, uint32_t w, uint32_t h);



    // Q: when input vertex and index size is smaller than previous batch, how to clear the old data exceed current size that on gpu?
    // A: we use draw index or draw vertex API, which will specify the vertices and indices count to draw. so we don't need to clear the old data.
    // TODO: we need to do draw call to consume data when buffer is about to full, then to draw the rest of the data
    void uploadBuffers(std::shared_ptr<GPUCommandBuffer> commandBuffer,
                       const void *vertexData, Uint32 inputVerticesSize,
                       const void *indexData, Uint32 inputIndicesSize)
    {
        uploadVertexBuffers(commandBuffer, vertexData, inputVerticesSize);
        uploadIndexBuffers(commandBuffer, indexData, inputIndicesSize);
    }

    std::shared_ptr<GPUCommandBuffer> acquireCommandBuffer(std::source_location location = std::source_location::current())
    {
        auto commandBuffer = acquireCommandBufferInternal(std::move(location));
        NE_ASSERT(commandBuffer, "Failed to acquire command buffer {}", SDL_GetError());
        return commandBuffer;
    }
    std::shared_ptr<GPUCommandBuffer> acquireCommandBufferInternal(std::source_location &&location)
    {
        // return std::make_shared<GPUCommandBuffer_SDL>(device);
        return std::make_shared<GPUCommandBuffer_SDL>(device, std::move(location));
    }

    void uploadVertexBuffers(std::shared_ptr<GPUCommandBuffer> commandBuffer, const void *vertexData, Uint32 vertexDataSize)
    {
        bool bNeedSubmit = false;
        if (!commandBuffer) {
            commandBuffer = acquireCommandBuffer();
            bNeedSubmit   = true;
        }

        // create transfer buffer, buffer on cpu
        SDL_GPUTransferBuffer *vertexTransferBuffer;
        {
            SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size  = vertexDataSize,
                .props = 0, // by comment
            };
            vertexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &transferBufferInfo);
            NE_ASSERT(vertexTransferBuffer, "Failed to create vertex transfer buffer {}", SDL_GetError());

            void *mmapData = SDL_MapGPUTransferBuffer(device, vertexTransferBuffer, false);
            NE_ASSERT(mmapData, "Failed to map vertex transfer buffer {}", SDL_GetError());
            std::memcpy(mmapData, vertexData, vertexDataSize);
            SDL_UnmapGPUTransferBuffer(device, vertexTransferBuffer);
        }

        // upload
        {
            SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(*commandBuffer);
            NE_ASSERT(copyPass, "Failed to begin copy pass {}", SDL_GetError());

            // transfer vertices
            {
                SDL_GPUTransferBufferLocation sourceLoc = {
                    .transfer_buffer = vertexTransferBuffer,
                    .offset          = 0,

                };
                SDL_GPUBufferRegion destRegion = {
                    .buffer = vertexBuffer,
                    .offset = 0,
                    .size   = vertexDataSize,
                };
                SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);
            }

            SDL_EndGPUCopyPass(copyPass);
        }

        if (bNeedSubmit) {
            SDL_SubmitGPUCommandBuffer(*commandBuffer);
        }

        // clean
        SDL_ReleaseGPUTransferBuffer(device, vertexTransferBuffer);
    }

    void uploadIndexBuffers(std::shared_ptr<GPUCommandBuffer> commandBuffer, const void *indexData, Uint32 indexDataSize)
    {
        bool bNeedSubmit = false;
        if (!commandBuffer) {
            commandBuffer = acquireCommandBuffer();
            bNeedSubmit   = true;
        }

        // create transfer buffer, buffer on cpu
        SDL_GPUTransferBuffer *indexTransferBuffer;
        {
            SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size  = indexDataSize,
                .props = 0, // by comment
            };
            indexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &transferBufferInfo);
            NE_ASSERT(indexTransferBuffer, "Failed to create index transfer buffer {}", SDL_GetError());

            void *mmapData = SDL_MapGPUTransferBuffer(device, indexTransferBuffer, false);
            NE_ASSERT(mmapData, "Failed to map index transfer buffer {}", SDL_GetError());
            std::memcpy(mmapData, indexData, indexDataSize);
            SDL_UnmapGPUTransferBuffer(device, indexTransferBuffer);
        }

        // upload
        {
            SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(*commandBuffer);
            NE_ASSERT(copyPass, "Failed to begin copy pass {}", SDL_GetError());

            // transfer indices
            {
                SDL_GPUTransferBufferLocation sourceLoc = {
                    .transfer_buffer = indexTransferBuffer,
                    .offset          = 0,
                };
                SDL_GPUBufferRegion destRegion = {
                    .buffer = indexBuffer,
                    .offset = 0,
                    .size   = indexDataSize,
                };
                SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);
            }

            SDL_EndGPUCopyPass(copyPass);
        }

        if (bNeedSubmit) {
            commandBuffer->submit();
        }

        // clean
        SDL_ReleaseGPUTransferBuffer(device, indexTransferBuffer);
    }

    void setVertexUnifroms(std::shared_ptr<GPUCommandBuffer> commandBuffer, Uint32 slot_index, void *data, Uint32 dataSize)
    {
        bool bNeedSubmit = false;
        if (!commandBuffer) {
            commandBuffer = acquireCommandBuffer();
            bNeedSubmit   = true;
        }
        SDL_PushGPUVertexUniformData(*commandBuffer, 0, data, dataSize);

        if (bNeedSubmit) {
            commandBuffer->submit();
        }
    }

    // call in createGraphicsPipeline
    void fillDefaultIndices(std::shared_ptr<GPUCommandBuffer> commandBuffer, EGraphicPipeLinePrimitiveType primitiveType);

  private:
    void createSamplers();
};
