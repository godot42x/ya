#pragma once

#include "Core/Base.h"
#include "Core/Camera/Camera.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/RenderPass.h"

namespace ya
{

struct SkyBoxSystem
{

    PipelineLayoutDesc _pipelineLayoutDesc = {
        .label                = "SkyboxPipelineLayout",
        .pushConstants        = {},
        .descriptorSetLayouts = {
            {
                .label    = "PerFrame",
                .set      = 0,
                .bindings = {
                    // projection + view matrix
                    {
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex,
                    },
                },
            },
            {
                .label    = "Resource",
                .set      = 1,
                .bindings = {
                    {
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },

    };
    // UBO structures
    struct SkyboxFrameUBO
    {
        glm::mat4 projection;
        glm::mat4 view;
    };

    stdptr<IDescriptorSetLayout> _dslPerFrame;
    stdptr<IDescriptorSetLayout> _dslResource;

    stdptr<IDescriptorPool> _DSP;
    DescriptorSetHandle     _dsPerFrame;
    DescriptorSetHandle     _dsResource;

    stdptr<IPipelineLayout>   _pipelineLayout = nullptr;
    stdptr<IGraphicsPipeline> _pipeline       = nullptr;
    stdptr<Sampler>           _sampler3D      = nullptr;



    stdptr<IBuffer> _frameUBO;

    void onInit(IRenderPass *renderPass, const PipelineRenderingInfo &pipelineRenderingInfo);
    void tick(ICommandBuffer *cmdBuf, float deltaTime, const FrameContext &ctx);
    void onDestroy();
};

} // namespace ya