#include <array>
#include <cmath>
#include <optional>


#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>


#include "Core/FileSystem/FileSystem.h"
#include "Render/Shader.h"

#include "reflect.cc/enum.h"
#include "utility/file_utils.h"



SDL_GPUGraphicsPipeline *pipeline;
SDL_GPUDevice           *device;
SDL_Window              *window;
SDL_GPUBuffer           *vertexBuffer;
SDL_GPUBuffer           *indexBuffer;
SDL_GPUTexture          *faceTexture;



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


std::unordered_map<ESamplerType, SDL_GPUSampler *> samplers;

// TODO: reflect this and auto generate VertexBufferDescription and VertexAttribute
struct VertexInput
{
    glm::vec3 position;
    glm::vec4 color;
    glm::vec2 uv; // aka texcoord
};
// triangle
struct IndexInput
{
    uint32_t a, b, c;
};


// quad vertices
std::vector<VertexInput> vertices = {
    // lt
    VertexInput{
        {-0.5f, 0.5f, 0.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f},
    },
    // rt
    VertexInput{
        {0.5f, 0.5f, 0.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f},
    },
    // lb
    VertexInput{
        .position = {-0.5, -0.5f, 0.f},
        .color    = {1.0f, 1.0f, 1.0f, 1.0f},
        .uv       = {0.0f, 1.0f},
    },
    // rb
    VertexInput{
        .position = {0.5f, -0.5f, 0.0f},
        .color    = {1.0f, 1.0f, 1.0f, 1.0f},
        .uv       = {1.0f, 1.0f},
    },
};
// quad indices
std::vector<IndexInput> indices = {
    {0, 1, 3},
    {0, 3, 2},
};


SDL_GPUTexture *createTexture(std::string_view filepath)
{
    auto         path    = FileSystem::get()->getProjectRoot() / filepath;
    SDL_Surface *surface = nullptr;

    ut::file::ImageInfo imageInfo = ut::file::ImageInfo::detect(path);

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
    outTexture = SDL_CreateGPUTexture(device, &info);
    if (!outTexture) {
        NE_CORE_ERROR("Failed to create texture: {}", SDL_GetError());
        return nullptr;
    }
    auto filename = std::format("{} 😜", path.stem().filename().string());
    SDL_SetGPUTextureName(device, outTexture, filename.c_str());
    NE_CORE_INFO("Texture name: {}", filename.c_str());


    SDL_GPUTransferBuffer *textureTransferBuffer = nullptr;
    {
        SDL_GPUTransferBufferCreateInfo textureTransferBufferInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = static_cast<Uint32>(surface->w * surface->h * 4), // 4 bytes per pixel
            .props = 0,                                                // by comment
        };
        textureTransferBuffer = SDL_CreateGPUTransferBuffer(device, &textureTransferBufferInfo);

        // mmap
        void *mmapPtr = SDL_MapGPUTransferBuffer(device, textureTransferBuffer, false);
        std::memcpy(mmapPtr, surface->pixels, textureTransferBufferInfo.size);
        SDL_UnmapGPUTransferBuffer(device, textureTransferBuffer);
    }


    // [upload] copy pass
    {
        SDL_GPUCommandBuffer *commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        NE_ASSERT(commandBuffer, "Failed to acquire command buffer {}", SDL_GetError());

        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commandBuffer);

        // transfer texture
        {
            SDL_GPUTextureTransferInfo srcTransferInfo = {
                .transfer_buffer = textureTransferBuffer,
                .offset          = 0,
            };
            SDL_GPUTextureRegion destGPUTextureRegion = {
                .texture   = outTexture,
                .mip_level = 0,
                .layer     = 0,
                .x         = 0,
                .y         = 0,
                .z         = 0,
                .w         = static_cast<Uint32>(surface->w),
                .h         = static_cast<Uint32>(surface->h),
                .d         = 1, // depth
            };

            SDL_UploadToGPUTexture(copyPass, &srcTransferInfo, &destGPUTextureRegion, false);
        }

        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(commandBuffer);
        SDL_ReleaseGPUTransferBuffer(device, textureTransferBuffer);
    }

    SDL_DestroySurface(surface);

    return outTexture;
}


void initImGui(SDL_GPUDevice *device, SDL_Window *window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO *io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo info{
        .Device            = device,
        .ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window),
        .MSAASamples       = SDL_GPU_SAMPLECOUNT_1,
    };
    ImGui_ImplSDLGPU3_Init(&info);
}

bool initSDL3GPU()
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

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
        return false;
    }

    return true;
}


void createSamplers()
{
    // DefaultLinear
    SDL_GPUSamplerCreateInfo defaultLinearInfo = {
        .min_filter        = SDL_GPU_FILTER_LINEAR, // 缩小采样
        .mag_filter        = SDL_GPU_FILTER_LINEAR, // 放大采样
        .mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .mip_lod_bias      = 0.0,
        .compare_op        = SDL_GPU_COMPAREOP_ALWAYS,
        .min_lod           = 1.0,
        .max_lod           = 1.0,
        .enable_anisotropy = false,
        .enable_compare    = false,
    };
    samplers[ESamplerType::DefaultLinear] = SDL_CreateGPUSampler(device, &defaultLinearInfo);

    // DefaultNearest
    SDL_GPUSamplerCreateInfo defaultNearestInfo = defaultLinearInfo;
    defaultNearestInfo.min_filter               = SDL_GPU_FILTER_NEAREST;
    defaultNearestInfo.mag_filter               = SDL_GPU_FILTER_NEAREST;
    defaultNearestInfo.mipmap_mode              = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplers[ESamplerType::DefaultNearest]      = SDL_CreateGPUSampler(device, &defaultNearestInfo);

    // PointClamp
    SDL_GPUSamplerCreateInfo pointClampInfo = {
        .min_filter     = SDL_GPU_FILTER_NEAREST,
        .mag_filter     = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
    samplers[ESamplerType::PointClamp] = SDL_CreateGPUSampler(device, &pointClampInfo);

    // PointWrap
    SDL_GPUSamplerCreateInfo pointWrapInfo = {
        .min_filter     = SDL_GPU_FILTER_NEAREST,
        .mag_filter     = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    };
    samplers[ESamplerType::PointWrap] = SDL_CreateGPUSampler(device, &pointWrapInfo);

    // LinearClamp
    SDL_GPUSamplerCreateInfo linearClampInfo = {
        .min_filter     = SDL_GPU_FILTER_LINEAR,
        .mag_filter     = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
    samplers[ESamplerType::LinearClamp] = SDL_CreateGPUSampler(device, &linearClampInfo);

    // LinearWrap
    SDL_GPUSamplerCreateInfo linearWrapInfo = {
        .min_filter     = SDL_GPU_FILTER_LINEAR,
        .mag_filter     = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    };
    samplers[ESamplerType::LinearWrap] = SDL_CreateGPUSampler(device, &linearWrapInfo);

    // AnisotropicClamp
    SDL_GPUSamplerCreateInfo anisotropicClampInfo = {
        .min_filter        = SDL_GPU_FILTER_LINEAR,
        .mag_filter        = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .max_anisotropy    = 4,
        .enable_anisotropy = true,
    };
    samplers[ESamplerType::AnisotropicClamp] = SDL_CreateGPUSampler(device, &anisotropicClampInfo);


    // AnisotropicWrap
    SDL_GPUSamplerCreateInfo anisotropicWrapInfo = {
        .min_filter        = SDL_GPU_FILTER_LINEAR,
        .mag_filter        = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .max_anisotropy    = 4,
        .enable_anisotropy = true,
    };
    samplers[ESamplerType::AnisotropicWrap] = SDL_CreateGPUSampler(device, &anisotropicWrapInfo);
    // validate samplers
    for (auto &[key, sampler] : samplers) {
        NE_CORE_ASSERT(sampler, "Failed to create sampler {} {}", (int)key, SDL_GetError());
    }
}


void uploadBuffers()
{
    // TODO: position use buffer , color and textcoord  use instance draw -> WTF?


    const uint32_t vertexBufferSize = sizeof(VertexInput) * vertices.size();
    const uint32_t indexBufferSize  = sizeof(IndexInput) * indices.size();


    // create transfer buffer, buffer on cpu
    SDL_GPUTransferBuffer *transferBuffer = nullptr;
    {
        SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = vertexBufferSize + indexBufferSize,
            .props = 0, // by comment
        };
        transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferBufferInfo);
        NE_ASSERT(transferBuffer, "Failed to create transfer buffer {}", SDL_GetError());

        void *mmapData = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        NE_ASSERT(mmapData, "Failed to map transfer buffer {}", SDL_GetError());
        std::memcpy(mmapData, vertices.data(), vertexBufferSize);
        std::memcpy((uint8_t *)mmapData + vertexBufferSize, indices.data(), indexBufferSize);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);
    }


    // upload
    {
        SDL_GPUCommandBuffer *commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        NE_ASSERT(commandBuffer, "Failed to acquire command buffer {}", SDL_GetError());
        {
            SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commandBuffer);
            NE_ASSERT(copyPass, "Failed to begin copy pass {}", SDL_GetError());

            // transfer vertices
            {
                SDL_GPUTransferBufferLocation sourceLoc = {
                    .transfer_buffer = transferBuffer,
                    .offset          = 0,
                };
                SDL_GPUBufferRegion destRegion = {
                    .buffer = vertexBuffer,
                    .offset = 0,
                    .size   = vertexBufferSize,
                };
                SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);
            }

            // transfer indices
            {
                SDL_GPUTransferBufferLocation sourceLoc = {
                    .transfer_buffer = transferBuffer,
                    .offset          = vertexBufferSize,
                };
                SDL_GPUBufferRegion destRegion = {
                    .buffer = indexBuffer,
                    .offset = 0,
                    .size   = indexBufferSize,
                };
                SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);
            }


            SDL_EndGPUCopyPass(copyPass);
        }
        SDL_SubmitGPUCommandBuffer(commandBuffer);
    }

    // clean
    SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
}

// shaders is high related with pipeline, we split it temporarily
std::optional<std::tuple<SDL_GPUShader *, SDL_GPUShader *>> createShaders()
{
    SDL_GPUShader *vertexShader   = nullptr;
    SDL_GPUShader *fragmentShader = nullptr;
    {
        ShaderScriptProcessorFactory factory;
        factory.withProcessorType(ShaderScriptProcessorFactory::EProcessorType::GLSL)
            .withShaderStoragePath("Engine/Shader/GLSL")
            .withCachedStoragePath("Engine/Intermediate/Shader/GLSL");

        std::shared_ptr<ShaderScriptProcessor> processor = factory.FactoryNew();

        auto ret = processor->process("Test.glsl");
        if (!ret) {
            NE_CORE_ERROR("Failed to process shader: {}", processor->tempProcessingPath);
            NE_CORE_ASSERT(false, "Failed to process shader: {}", processor->tempProcessingPath);
        }

        ShaderScriptProcessor::stage2spirv_t &codes = ret.value();


        SDL_GPUShaderCreateInfo vertexCrateInfo = {
            .code_size            = codes[EShaderStage::Vertex].size() * sizeof(uint32_t) / sizeof(uint8_t),
            .code                 = (uint8_t *)codes[EShaderStage::Vertex].data(),
            .entrypoint           = "main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers         = 0,
            .num_storage_textures = 0,
            .num_storage_buffers  = 0,
            .num_uniform_buffers  = 0,

        };
        SDL_GPUShaderCreateInfo fragmentCreateInfo = {
            .code_size            = codes[EShaderStage::Fragment].size() * sizeof(uint32_t) / sizeof(uint8_t),
            .code                 = (uint8_t *)codes[EShaderStage::Fragment].data(),
            .entrypoint           = "main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_FRAGMENT,
            .num_samplers         = 1,
            .num_storage_textures = 0,
            .num_storage_buffers  = 0,
            .num_uniform_buffers  = 0,
        };

        vertexShader = SDL_CreateGPUShader(device, &vertexCrateInfo);
        if (!vertexShader) {
            NE_CORE_ERROR("Failed to create vertex shader");
            return {};
        }
        fragmentShader = SDL_CreateGPUShader(device, &fragmentCreateInfo);
        if (!fragmentShader) {
            NE_CORE_ERROR("Failed to create fragment shader");
            return {};
        }

        // SDL_Storage *storage = openFileStorage("Engine/Content/Test/", true);
        // const char  *text    = "abc";
        // SDL_WriteStorageFile(storage, "abc", (void *)"abc", strlen(text));
        // SDL_CloseStorage(storage);
    }


    return {{vertexShader, fragmentShader}};
}

// basic pipeline can draw triangle with texture
bool createGraphicsPipeline()
{

    auto shaders = createShaders();
    NE_ASSERT(shaders.has_value(), "Failed to create shader {}", SDL_GetError());
    auto &[vertexShader, fragmentShader] = shaders.value();

    const uint32_t vertexBufferSize = sizeof(VertexInput) * vertices.size();
    const uint32_t indexBufferSize  = sizeof(IndexInput) * indices.size();

    // create vertexBuffer, buffer on gpu
    {
        SDL_GPUBufferCreateInfo bufferInfo = {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size  = vertexBufferSize, // TODO: make a big size buffer for batch draw call
            .props = 0,                // by comment
        };

        vertexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);
        NE_ASSERT(vertexBuffer, "Failed to create vertex buffer {}", SDL_GetError());

        SDL_SetGPUBufferName(device, vertexBuffer, "godot42 vertex buffer 😍");
    }

    // create indexBuffer
    {
        SDL_GPUBufferCreateInfo bufferInfo = {
            .usage = SDL_GPU_BUFFERUSAGE_INDEX,
            .size  = indexBufferSize, // TODO: make a big size buffer for batch draw call
            .props = 0,               // by comment
        };

        indexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);
        NE_ASSERT(indexBuffer, "Failed to create index buffer {}", SDL_GetError());

        SDL_SetGPUBufferName(device, indexBuffer, "godot42 index buffer 😁");
    }


    std::vector<SDL_GPUVertexBufferDescription> vertexBufferDescs;
    std::vector<SDL_GPUVertexAttribute>         vertexAttributes;
    {

        vertexBufferDescs = {
            // aPos vec4 aColor vec4
            SDL_GPUVertexBufferDescription{
                .slot               = 0,
                .pitch              = sizeof(VertexInput),
                .input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                .instance_step_rate = 0,
            },
        };
        vertexAttributes = {
            SDL_GPUVertexAttribute{
                .location    = 0,
                .buffer_slot = 0,
                .format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset      = offsetof(VertexInput, position),
            },
            SDL_GPUVertexAttribute{
                .location    = 1,
                .buffer_slot = 0,
                .format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                .offset      = offsetof(VertexInput, color),
            },
            SDL_GPUVertexAttribute{
                .location    = 2,
                .buffer_slot = 0,
                .format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset      = offsetof(VertexInput, uv),
            },
        };
    }


    // this format is the final screen surface's format
    // if you want other format, create texture yourself
    SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUColorTargetDescription colorTargetDesc{
        .format = format,
        // final_color = (src_color × src_color_blendfactor) color_blend_op (dst_color × dst_color_blendfactor)
        // final_alpha = (src_alpha × src_alpha_blendfactor) alpha_blend_op (dst_alpha × dst_alpha_blendfactor)
        .blend_state = SDL_GPUColorTargetBlendState{
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op        = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op        = SDL_GPU_BLENDOP_ADD,
            .color_write_mask      = SDL_GPU_COLORCOMPONENT_A | SDL_GPU_COLORCOMPONENT_B |
                                SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_R,
            .enable_blend            = true,
            .enable_color_write_mask = false,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader      = vertexShader,
        .fragment_shader    = fragmentShader,
        .vertex_input_state = SDL_GPUVertexInputState{
            .vertex_buffer_descriptions = vertexBufferDescs.data(),
            .num_vertex_buffers         = static_cast<Uint32>(vertexBufferDescs.size()),
            .vertex_attributes          = vertexAttributes.data(),
            .num_vertex_attributes      = static_cast<Uint32>(vertexAttributes.size()),
        },
        .primitive_type   = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = SDL_GPURasterizerState{
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_NONE, // cull back/front face
        },
        .multisample_state = SDL_GPUMultisampleState{
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .enable_mask  = false,
        },
        .target_info = SDL_GPUGraphicsPipelineTargetInfo{
            .color_target_descriptions = &colorTargetDesc,
            .num_color_targets         = 1,
            .has_depth_stencil_target  = false,
        },
    };
    pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    return true;
}



SDLMAIN_DECLSPEC SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    FileSystem::init();
    Logger::init();

    if (!initSDL3GPU()) {
        return SDL_APP_FAILURE;
    }
    ::initImGui(device, window);

    createGraphicsPipeline();
    createSamplers();
    uploadBuffers();
    faceTexture = createTexture("Engine/Content/TestTextures/face.png");

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    SDL_GPUTexture     *swapchainTexture = nullptr;
    Uint32              w, h;
    static ESamplerType selectedSampler = ESamplerType::PointClamp;

    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_Delay(100);
    }

    SDL_GPUCommandBuffer *commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer) {
        NE_CORE_ERROR("Failed to acquire command buffer {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer,
                                               window,
                                               &swapchainTexture,
                                               &w,
                                               &h)) {
        NE_CORE_ERROR("Failed to acquire swapchain texture {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // when window is minimized, the swapchainTexture will be null
    if (!swapchainTexture) {
        return SDL_APP_CONTINUE;
    }

    static glm::vec4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (ImGui::Begin("Debug"))
    {
        ImGui::DragFloat4("Clear Color", glm::value_ptr(clearColor), 0.01f, 0.0f, 1.0f);

        const std::string currentSamplerName = ESamplerType2Strings[selectedSampler];
        if (ImGui::BeginCombo("Sampler", currentSamplerName.c_str()))
        {
            for (int i = 0; i < static_cast<int>(ESamplerType::ENUM_MAX); i++)
            {
                bool              bSelected   = (static_cast<int>(selectedSampler) == i);
                const std::string samplerName = ESamplerType2Strings[static_cast<ESamplerType>(i)];
                if (ImGui::Selectable(samplerName.c_str(), &bSelected)) {
                    selectedSampler = static_cast<ESamplerType>(i);
                    NE_CORE_INFO("Selected sampler: {}", samplerName);
                }
                if (bSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        bool bVertexInputChanged = false;
        for (int i = 0; i < 4; ++i) {
            ImGui::Text("Vertex %d", i);
            char label[32];
            sprintf(label, "position##%d", i);
            if (ImGui::DragFloat3(label, glm::value_ptr(vertices[i].position))) {
                bVertexInputChanged = true;
            }
            sprintf(label, "color##%d", i);
            if (ImGui::DragFloat4(label, glm::value_ptr(vertices[i].color))) {
                bVertexInputChanged = true;
            }
            sprintf(label, "uv##%d", i);
            if (ImGui::DragFloat2(label, glm::value_ptr(vertices[i].uv))) {
                bVertexInputChanged = true;
            }
        }
        if (bVertexInputChanged) {
            NE_INFO("Vertex input changed, reuploading buffers");
            uploadBuffers();
        }
    }
    ImGui::End();
    ImGui::Render();
    ImDrawData *drawData   = ImGui::GetDrawData();
    const bool  bMinimized = drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f;


    if (swapchainTexture && !bMinimized)
    {
        Imgui_ImplSDLGPU3_PrepareDrawData(drawData, commandBuffer);

        SDL_GPURenderPass     *renderpass;
        SDL_GPUColorTargetInfo colorTargetInfo = {
            .texture               = swapchainTexture,
            .mip_level             = 0,
            .layer_or_depth_plane  = 0,
            .clear_color           = {clearColor.r, clearColor.g, clearColor.b, clearColor.a},
            .load_op               = SDL_GPU_LOADOP_CLEAR,
            .store_op              = SDL_GPU_STOREOP_STORE,
            .cycle                 = true, // wtf?
            .cycle_resolve_texture = false,
        };

        // target info can be multiple(use same pipeline?)
        renderpass = SDL_BeginGPURenderPass(commandBuffer, &colorTargetInfo, 1, nullptr);
        {
            SDL_BindGPUGraphicsPipeline(renderpass, pipeline);

            SDL_GPUBufferBinding vertexBufferBinding = {
                .buffer = vertexBuffer,
                .offset = 0,
            };
            SDL_BindGPUVertexBuffers(renderpass, 0, &vertexBufferBinding, 1);

            // TODO: use uint16  to optimize index buffer
            SDL_GPUBufferBinding indexBufferBinding = {
                .buffer = indexBuffer,
                .offset = 0,
            };
            SDL_BindGPUIndexBuffer(renderpass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            // sampler binding
            SDL_GPUTextureSamplerBinding textureBinding = {
                .texture = faceTexture,
                .sampler = samplers[selectedSampler],
            };
            SDL_BindGPUFragmentSamplers(renderpass, 0, &textureBinding, 1);

            int windowWidth, windowHeight;
            SDL_GetWindowSize(window, &windowWidth, &windowHeight);

            // TODO: these works should be done in camera matrix?
            // Calculate proper aspect-preserving dimensions
            float targetAspect = 1.0f; // 1:1 for square
            float windowAspect = (float)windowWidth / (float)windowHeight;
            float viewportWidth, viewportHeight;
            float offsetX = 0, offsetY = 0;

            if (windowAspect > targetAspect) {
                // Window is wider than needed
                viewportHeight = (float)windowHeight;
                viewportWidth  = viewportHeight * targetAspect;
                offsetX        = (windowWidth - viewportWidth) / 2.0f;
            }
            else {
                // Window is taller than needed
                viewportWidth  = (float)windowWidth;
                viewportHeight = viewportWidth / targetAspect;
                offsetY        = (windowHeight - viewportHeight) / 2.0f;
            }

            SDL_GPUViewport viewport = {
                .x         = offsetX,
                .y         = offsetY,
                .w         = viewportWidth,
                .h         = viewportHeight,
                .min_depth = 0.0f,
                .max_depth = 1.0f,
            };
            SDL_SetGPUViewport(renderpass, &viewport);

            // SDL_BindGPUVertexBuffer(renderpass, 0, vertexBuffer, 0);
            // SDL_DrawGPUPrimitives(renderpass, 3, 1, 0, 0);
            SDL_DrawGPUIndexedPrimitives(renderpass,
                                         indices.size() * 3, // 3 index for each triangle
                                         1,
                                         0,
                                         0,
                                         0);

            // after graphics pipeline draw, or make pipeline draw into a RT
            ImGui_ImplSDLGPU3_RenderDrawData(drawData, commandBuffer, renderpass);
        }
        SDL_EndGPURenderPass(renderpass);
    }

    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        NE_CORE_ERROR("Failed to submit command buffer {}", SDL_GetError());
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    // NE_CORE_TRACE("Event: {}", event->type);

    ImGui_ImplSDL3_ProcessEvent(event);

    switch ((SDL_EventType)event->type) {
    case SDL_EventType::SDL_EVENT_KEY_UP:
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Key up: %d", event->key.key);
        if (event->key.key == SDLK_Q)
        {
            return SDL_APP_SUCCESS;
        }
    } break;
    case SDL_EventType::SDL_EVENT_WINDOW_RESIZED:
    {
        if (event->window.windowID == SDL_GetWindowID(window))
        {
            SDL_WaitForGPUIdle(device);
            NE_CORE_INFO("Window resized to {}x{}", event->window.data1, event->window.data2);
        }
    } break;
    case SDL_EventType::SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        NE_CORE_INFO("SDL Window Close Requested {}", event->window.windowID);
        if (event->window.windowID == SDL_GetWindowID(window))
        {
            return SDL_APP_SUCCESS;
        }
    } break;
    case SDL_EventType::SDL_EVENT_QUIT:
    {
        NE_CORE_INFO("SDL Quit");
        return SDL_APP_SUCCESS;
    } break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "sdl quit with result: %u", result);
    SDL_WaitForGPUIdle(device);

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    // clear samplers
    for (auto &[key, sampler] : samplers) {
        if (!sampler) {
            continue;
        }
        SDL_ReleaseGPUSampler(device, sampler);
    }

    if (faceTexture) {
        SDL_ReleaseGPUTexture(device, faceTexture);
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
    SDL_Quit();
}
