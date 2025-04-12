#pragma once

#include "../GraphicsPipeline.h"

#include "SDL3/SDL_gpu.h"

#include "SDLShader.h"

namespace SDL {

struct SDLGraphicsPipeLine : public GraphicsPipeline
{

    SDL_GPUDevice                              *device          = nullptr; // owning device?
    SDL_GPUGraphicsPipeline                    *pipeline        = nullptr;
    std::size_t                                 vertexInputSize = 0;
    std::vector<SDL_GPUVertexBufferDescription> vertexBufferDescs;
    std::vector<SDL_GPUVertexAttribute>         vertexAttributes;
    GraphicsPipelineCreateInfo                  pipelineCreateInfo;

    bool create(SDL_GPUDevice *device, SDL_Window *window, const GraphicsPipelineCreateInfo &pipelineCI)
    {
        this->device       = device;
        pipelineCreateInfo = pipelineCI;
        SDLShader shader   = SDLShader()
                               .preCreate(pipelineCI.shaderCreateInfo) // prepare spir code and reflection info
                               .create(device);                        // sdl api create

        auto vertexShader   = shader.vertexShader;
        auto fragmentShader = shader.fragmentShader;
        NE_CORE_ASSERT(vertexShader && fragmentShader, "Failed to create shader: {}", SDL_GetError());
        auto &shaderResources = shader.shaderResources;

        this->prepareVertexInfo(pipelineCI, shaderResources);

        // this format is the final screen surface's format
        // if you want other format, create texture yourself
        SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(device, window);
        if (format == SDL_GPU_TEXTUREFORMAT_INVALID) {
            NE_CORE_ERROR("Failed to get swapchain texture format: {}", SDL_GetError());
            return false;
        }
        NE_CORE_INFO("current gpu texture format: {}", (int)format);



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
            // clang-format off
        .rasterizer_state = SDL_GPURasterizerState{
            .fill_mode  = SDL_GPU_FILLMODE_FILL,
            .cull_mode  = SDL_GPU_CULLMODE_BACK, // cull back/front face
            .front_face = pipelineCI.frontFaceType == GraphicsPipelineCreateInfo::ClockWise ?
                            SDL_GPU_FRONTFACE_CLOCKWISE :
                           SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        },
            // clang-format on
            .multisample_state = SDL_GPUMultisampleState{
                .sample_count = SDL_GPU_SAMPLECOUNT_1,
                .enable_mask  = false,
            },
            .depth_stencil_state = SDL_GPUDepthStencilState{
                .compare_op = SDL_GPU_COMPAREOP_GREATER, // -z forward
                // .back_stencil_state = SDL_GPUStencilOpState{
                //     .fail_op       = SDL_GPU_STENCILOP_ZERO,
                //     .pass_op       = SDL_GPU_STENCILOP_KEEP,
                //     .depth_fail_op = SDL_GPU_STENCILOP_ZERO,
                //     .compare_op    = SDL_GPU_COMPAREOP_NEVER,
                // },
                // .front_stencil_state = SDL_GPUStencilOpState{
                //     .fail_op       = SDL_GPU_STENCILOP_KEEP,
                //     .pass_op       = SDL_GPU_STENCILOP_KEEP,
                //     .depth_fail_op = SDL_GPU_STENCILOP_KEEP,
                //     .compare_op    = SDL_GPU_COMPAREOP_LESS,
                // },
                // .compare_mask        = 0xFF,
                // .write_mask          = 0xFF,
                .enable_depth_test   = true,
                .enable_depth_write  = true,
                .enable_stencil_test = false,
            },
            .target_info = SDL_GPUGraphicsPipelineTargetInfo{
                .color_target_descriptions = &colorTargetDesc,
                .num_color_targets         = 1,
                .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                .has_depth_stencil_target  = false,
            },
        };
        switch (pipelineCI.primitiveType) {
        case EGraphicPipeLinePrimitiveType::TriangleList:
            // WTF? it should be SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, so ambiguous
            // sdlGPUCreateInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
            sdlGPUCreateInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            break;
        default:
            NE_CORE_ASSERT(false, "Invalid primitive type {}", int(pipelineCI.primitiveType));
            break;
        }

        pipeline = SDL_CreateGPUGraphicsPipeline(device, &sdlGPUCreateInfo);

        SDL_ReleaseGPUShader(device, vertexShader);
        SDL_ReleaseGPUShader(device, fragmentShader);


        return pipeline != nullptr;
    }

    void clean()
    {
        if (pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
            pipeline = nullptr;
        }
    }


  private:

    void prepareVertexInfo(const GraphicsPipelineCreateInfo &pipelineCI, const std::unordered_map<EShaderStage::T, ShaderReflection::ShaderResources> &shaderResources)
    {
        // prepare vertex buffer description and vertex attributes
        if (pipelineCI.bDeriveInfoFromShader)
        {
            NE_CORE_INFO("Deriving vertex info from shader reflection");

            // Get the reflected shader resources for the vertex stage
            const auto &vertexResources = shaderResources.find(EShaderStage::Vertex)->second;

            // Initialize our vertex inputs based on the reflected data
            for (const auto &input : vertexResources.inputs) {
                SDL_GPUVertexAttribute sdlVertAttr{
                    .location    = input.location,
                    .buffer_slot = 0,
                    .format      = input.format, // We already converted to SDL format in reflection
                    .offset      = input.offset, // We already calculated aligned offset in reflection
                };

                if (sdlVertAttr.format == SDL_GPU_VERTEXELEMENTFORMAT_INVALID) {
                    NE_CORE_ERROR("Unsupported vertex attribute format for input: {}", input.name);
                    continue;
                }

                vertexAttributes.push_back(sdlVertAttr);

                NE_CORE_INFO("Added vertex attribute: {} location={}, format={}, offset={}, size={}", input.name, input.location, (int)sdlVertAttr.format, input.offset, input.size);
            }

            // Calculate the total size of all vertex attributes
            uint32_t totalSize = 0;
            if (!vertexResources.inputs.empty()) {
                const auto &lastInput = vertexResources.inputs.back();
                totalSize             = lastInput.offset + lastInput.size;
            }

            this->vertexInputSize = totalSize;

            // Create the vertex buffer description
            vertexBufferDescs.push_back(SDL_GPUVertexBufferDescription{
                .slot               = 0,
                .pitch              = static_cast<Uint32>(this->vertexInputSize),
                .input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                .instance_step_rate = 0,
            });

            NE_CORE_INFO("Created vertex buffer with {} attributes, total aligned size: {} bytes", vertexAttributes.size(), this->vertexInputSize);
        }
        else {
            for (int i = 0; i < pipelineCI.vertexBufferDescs.size(); ++i) {
                vertexBufferDescs.push_back(SDL_GPUVertexBufferDescription{
                    .slot               = pipelineCI.vertexBufferDescs[i].slot,
                    .pitch              = pipelineCI.vertexBufferDescs[i].pitch,
                    .input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                    .instance_step_rate = 0,
                });
            }
            for (int i = 0; i < pipelineCI.vertexAttributes.size(); ++i) {
                SDL_GPUVertexAttribute sdlVertAttr{
                    .location    = pipelineCI.vertexAttributes[i].location,
                    .buffer_slot = pipelineCI.vertexAttributes[i].bufferSlot,
                    .format      = SDL_GPU_VERTEXELEMENTFORMAT_INVALID,
                    .offset      = pipelineCI.vertexAttributes[i].offset,
                };

                switch (pipelineCI.vertexAttributes[i].format) {
                case EVertexAttributeFormat::Float2:
                    sdlVertAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
                    break;
                case EVertexAttributeFormat::Float3:
                    sdlVertAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
                    break;
                case EVertexAttributeFormat::Float4:
                    sdlVertAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
                    break;
                default:
                    NE_CORE_ASSERT(false, "Invalid vertex attribute format {}", int(pipelineCI.vertexAttributes[i].format));
                    break;
                }
                vertexAttributes.emplace_back(std::move(sdlVertAttr));
            }

            const auto &last = pipelineCI.vertexAttributes.end() - 1;
            vertexInputSize  = last->offset + EVertexAttributeFormat::T2Size(last->format);
        }
    }
};

} // namespace SDL