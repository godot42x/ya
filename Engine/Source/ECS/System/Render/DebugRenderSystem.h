#pragma once

#include "Core/Base.h"
#include "ECS/System/Render/IRenderSystem.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"


namespace ya
{

struct DebugRenderSystem : public IRenderSystem
{
    enum EMode
    {
        None = 0,
        NormalColor,
        NormalDir,
        Depth,
        UV,
    };

    struct DebugUBO
    {
        glm::mat4 projection{1.f};
        glm::mat4 view{1.f};
        alignas(8) glm::ivec2 resolution{0, 0};
        alignas(4) int mode   = 0; // 0:none, 1:normal, 2:depth, 3:uv
        alignas(4) float time = 0.f;
        glm::vec4 floatParam  = glm::vec4(0.0f);
    };

    struct ModelPushConstant
    {
        glm::mat4 modelMat;
    };

    DebugUBO uDebug;
    EMode    _mode;

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

    GraphicsPipelineCreateInfo _pipelineCI;

    stdptr<IDescriptorSetLayout> _dsl;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorPool>      _dsp;
    DescriptorSetHandle          _uboDS = nullptr;
    stdptr<IBuffer>              _ubo;

  public:
    DebugRenderSystem() : IRenderSystem("DebugRenderSystem") {}

    void onInitImpl(const InitParams& initParams) override;
    void onDestroy() override;
    void onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx) override;

  protected:
    void onRenderGUI() override;

  private:
    void updateUBO(const FrameContext* ctx);
};


} // namespace ya

YA_REFLECT_ENUM_BEGIN(::ya::DebugRenderSystem::EMode)
YA_REFLECT_ENUM_VALUE(None)
YA_REFLECT_ENUM_VALUE(NormalColor)
YA_REFLECT_ENUM_VALUE(NormalDir)
YA_REFLECT_ENUM_VALUE(Depth)
YA_REFLECT_ENUM_VALUE(UV)
YA_REFLECT_ENUM_END()