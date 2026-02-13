#pragma once

#include "Core/Base.h"
#include "ECS/System/IRenderSystem.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"

#include "glm/gtc/type_ptr.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "imgui.h"

namespace ya
{

struct DebugRenderSystem : public IRenderSystem
{
    struct DebugUBO
    {
        glm::mat4 projection{1.f};
        glm::mat4 view{1.f};
        alignas(8) glm::ivec2 resolution{0, 0};
        alignas(4) int32_t mode = 0; // 0:none, 1:normal, 2:depth, 3:uv
        alignas(4) float time   = 0.f;
        glm::vec4 floatParam    = glm::vec4(0.0f);
    };

    struct ModelPushConstant
    {
        glm::mat4 modelMat;
    };

    DebugUBO uDebug;

    PipelineLayoutDesc _pipelineLayoutDesc{
        .label         = "DebugRenderSystem_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(ModelPushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "DebugRender_DSL",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment,
                    },
                },
            },
        },
    };

    GraphicsPipelineCreateInfo _pipelineDesc;

    stdptr<IDescriptorSetLayout> _dsl;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorPool>      _dsp;
    DescriptorSetHandle          _uboDS = nullptr;
    stdptr<IBuffer>              _ubo;

  public:
    DebugRenderSystem() : IRenderSystem("DebugRenderSystem") {}

    void onInit(IRenderPass* renderPass, const PipelineRenderingInfo& pipelineRenderingInfo) override;
    void onDestroy() override;
    void onRender(ICommandBuffer* cmdBuf, FrameContext* ctx) override;

  protected:
    void onRenderGUI() override;

  private:
    void updateUBO(FrameContext* ctx);
};

} // namespace ya
