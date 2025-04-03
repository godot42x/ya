#include "Render.h"

#include "utility/file_utils.h"

// shaders is high related with pipeline, we split it temporarily
// TODO: export shader info -> use reflection do this
std::optional<std::tuple<SDL_GPUShader *, SDL_GPUShader *>> SDLGPURender::createShaders(std::string_view shaderName)
{
    SDL_GPUShader *vertexShader   = nullptr;
    SDL_GPUShader *fragmentShader = nullptr;
    {
        ShaderScriptProcessorFactory factory;
        factory.withProcessorType(ShaderScriptProcessorFactory::EProcessorType::GLSL)
            .withShaderStoragePath("Engine/Shader/GLSL")
            .withCachedStoragePath("Engine/Intermediate/Shader/GLSL");

        std::shared_ptr<ShaderScriptProcessor> processor = factory.FactoryNew();

        auto ret = processor->process(shaderName);
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
            .num_uniform_buffers  = 1,
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
    }


    return {{vertexShader, fragmentShader}};
}

// a Pipeline: 1 vertex shader + 1 fragment shader + 1 render pass + 1 vertex buffer + 1 index buffer
// their format should be compatible with each other
// so we put them together with initialization
bool SDLGPURender::createGraphicsPipeline(const GraphicsPipelineCreateInfo &info)
{

    // create global big size buffer for batch draw call
    {
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

        // // uniform
        // {
        //     SDL_GPUBufferCreateInfo bufferInfo = {
        //         .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
        //         .size  = indexBufferSize, // TODO: make a big size buffer for batch draw call
        //         .props = 0,               // by comment
        //     };
        //     // SDL_CreateGPUBuffer()
        // }
    }

    // SHADER is high related with pipeline!!!
    auto shaders = createShaders(info.shaderName);
    NE_ASSERT(shaders.has_value(), "Failed to create shader {}", SDL_GetError());
    auto &[vertexShader, fragmentShader] = shaders.value();

    std::vector<SDL_GPUVertexBufferDescription> vertexBufferDescs;
    std::vector<SDL_GPUVertexAttribute>         vertexAttributes;
    {

        for (int i = 0; i < info.vertexBufferDescs.size(); ++i) {
            vertexBufferDescs.push_back(SDL_GPUVertexBufferDescription{
                .slot               = info.vertexBufferDescs[i].slot,
                .pitch              = info.vertexBufferDescs[i].pitch,
                .input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                .instance_step_rate = 0,
            });
        }
        for (int i = 0; i < info.vertexAttributes.size(); ++i) {
            SDL_GPUVertexAttribute sdlVertAttr{
                .location    = info.vertexAttributes[i].location,
                .buffer_slot = info.vertexAttributes[i].bufferSlot,
                .format      = SDL_GPU_VERTEXELEMENTFORMAT_INVALID,
                .offset      = info.vertexAttributes[i].offset,
            };

            switch (info.vertexAttributes[i].format) {
            case Float2:
                sdlVertAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
                break;
            case Float3:
                sdlVertAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
                break;
            case Float4:
                sdlVertAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
                break;
            default:
                NE_CORE_ASSERT(false, "Invalid vertex attribute format {}", int(info.vertexAttributes[i].format));
                break;
            }
            vertexAttributes.emplace_back(std::move(sdlVertAttr));
        }
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
    SDL_GPUGraphicsPipelineCreateInfo sdlGPUCreateInfo = {
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
    pipeline = SDL_CreateGPUGraphicsPipeline(device, &sdlGPUCreateInfo);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);


    return pipeline != nullptr;
}

void SDLGPURender::createSamplers()
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

SDL_GPUTexture *SDLGPURender::createTexture(std::string_view filepath)
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

// Q: when input vertex and index size is smaller than previous batch, how to clear the old data exceed current size that on gpu?
// A: we use draw index or draw vertex API, which will specify the vertices and indices count to draw. so we don't need to clear the old data.
// TODO: we need to do draw call to consume data when buffer is about to full, then to draw the rest of the data
void SDLGPURender::uploadBuffers(SDL_GPUCommandBuffer *commandBuffer, void *vertexData, Uint32 inputVerticesSize, void *indexData, Uint32 inputIndicesSize)
{
    bool bNeedSubmit = false;
    if (!commandBuffer) {
        NE_CORE_WARN("Command buffer is null, acquire a new one");
        commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        NE_ASSERT(commandBuffer, "Failed to acquire command buffer {}", SDL_GetError());
        bNeedSubmit = true;
    }

    // create transfer buffer, buffer on cpu
    SDL_GPUTransferBuffer *vertexTransferBuffer;
    SDL_GPUTransferBuffer *indexTransferBuffer;
    {
        SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = inputVerticesSize,
            .props = 0, // by comment
        };
        vertexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &transferBufferInfo);
        NE_ASSERT(vertexTransferBuffer, "Failed to create transfer buffer {}", SDL_GetError());

        void *mmapData = SDL_MapGPUTransferBuffer(device, vertexTransferBuffer, false);
        NE_ASSERT(mmapData, "Failed to map transfer buffer {}", SDL_GetError());
        std::memcpy(mmapData, vertexData, inputVerticesSize);
        SDL_UnmapGPUTransferBuffer(device, vertexTransferBuffer);
    }
    {
        SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = inputIndicesSize,
            .props = 0, // by comment
        };
        indexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &transferBufferInfo);
        NE_ASSERT(indexTransferBuffer, "Failed to create transfer buffer {}", SDL_GetError());

        void *mmapData = SDL_MapGPUTransferBuffer(device, indexTransferBuffer, false);
        NE_ASSERT(mmapData, "Failed to map transfer buffer {}", SDL_GetError());
        std::memcpy(mmapData, indexData, inputIndicesSize);
        SDL_UnmapGPUTransferBuffer(device, indexTransferBuffer);
    }


    // upload
    {
        {
            SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commandBuffer);
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
                    .size   = inputVerticesSize,
                };
                SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);
            }

            // transfer indices
            {
                SDL_GPUTransferBufferLocation sourceLoc = {
                    .transfer_buffer = indexTransferBuffer,
                    .offset          = 0,
                };
                SDL_GPUBufferRegion destRegion = {
                    .buffer = indexBuffer,
                    .offset = 0,
                    .size   = inputIndicesSize,
                };
                SDL_UploadToGPUBuffer(copyPass, &sourceLoc, &destRegion, false);
            }

            SDL_EndGPUCopyPass(copyPass);
        }
    }

    if (bNeedSubmit) {
        SDL_SubmitGPUCommandBuffer(commandBuffer);
    }

    // clean
    SDL_ReleaseGPUTransferBuffer(device, vertexTransferBuffer);
    SDL_ReleaseGPUTransferBuffer(device, indexTransferBuffer);
}
