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

    static constexpr uint32_t SKYBOX_PER_FRAME_SET = 4;

    uint32_t _index = 0;

    stdptr<IDescriptorSetLayout> _dslPerFrame;
    stdptr<IDescriptorSetLayout> _dslResource;

    stdptr<IDescriptorPool> _DSP;

    std::array<DescriptorSetHandle, SKYBOX_PER_FRAME_SET> _dsPerFrame;
    std::array<stdptr<IBuffer>, SKYBOX_PER_FRAME_SET>     _frameUBO;

    DescriptorSetHandle _dsResource;

    stdptr<IPipelineLayout>   _pipelineLayout = nullptr;
    stdptr<IGraphicsPipeline> _pipeline       = nullptr;
    stdptr<Sampler>           _sampler3D      = nullptr;



    void onInit(IRenderPass *renderPass, const PipelineRenderingInfo &pipelineRenderingInfo);
    void tick(ICommandBuffer *cmdBuf, float deltaTime, const FrameContext &ctx);
    void onDestroy();

    void advance() { _index = (_index + 1) % SKYBOX_PER_FRAME_SET; }
    void beginFrame() { _index = 0; }
};

} // namespace ya