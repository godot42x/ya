#pragma once

#include "Core/Base.h"
#include "ECS/System/Render/IRenderSystem.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"

#include "Shadow.CombinedShadowMappingGenerate.glsl.h"

namespace ya
{

struct ShadowMapping : public IRenderSystem
{
    using Super                                    = IRenderSystem;
    static constexpr uint32_t SHADOW_PER_FRAME_SET = 4;

    // struct FrameUBO
    // {
    //     glm::mat4 directionalLightMatrix;
    //     glm::mat4 pointLightMatrices[6 * MAX_POINT_LIGHTS]; // uses engine's MAX_POINT_LIGHTS (from Common/Limits.glsl)
    //     uint32_t  numPointLights      = 0;
    //     uint32_t  hasDirectionalLight = 0;
    //     float     pointLightFarPlane  = 0.0f;
    //     uint32_t  _pad                = 0;
    // };
    using FrameUBO = glsl_types::Shadow::CombinedShadowMappingGenerate::FrameData;

    // struct ModelPushConstant
    // {
    //     glm::mat4 model{1.0f};
    // };
    using ModelPushConstant = glsl_types::Shadow::CombinedShadowMappingGenerate::PushConstant;


    stdptr<IRenderTarget> _shadowMapRT  = nullptr;
    Extent2D              _shadowExtent = {.width = 1024, .height = 1024};

    bool  _bAutoBindViewportScissor = true;
    float _bias                     = 0.005f;
    float _normalBias               = 0.01f;

    uint32_t _index = 0;

    PipelineLayoutDesc _pipelineLayoutDesc = {
        .label         = "ShadowMapping_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(ModelPushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "ShadowMapping_Frame_DSL",
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

    stdptr<IDescriptorSetLayout>                          _dslPerFrame    = nullptr;
    stdptr<IDescriptorPool>                               _DSP            = nullptr;
    std::array<DescriptorSetHandle, SHADOW_PER_FRAME_SET> _dsPerFrame     = {};
    std::array<stdptr<IBuffer>, SHADOW_PER_FRAME_SET>     _frameUBO       = {};
    stdptr<IPipelineLayout>                               _pipelineLayout = nullptr;

    ShadowMapping() : IRenderSystem("ShadowMappingSystem") {}

    void onInitImpl(const InitParams& initParams) override;
    void onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx) override;
    void onDestroy() override;
    void resetFrameSlot() override { _index = 0; }

    void setRenderTarget(const stdptr<IRenderTarget>& rt)
    {
        _shadowMapRT = rt;
        if (_shadowMapRT) {
            _shadowExtent = _shadowMapRT->getExtent();
        }
    }

    IRenderTarget* getRenderTarget() const { return _shadowMapRT.get(); }

  protected:
    void onRenderGUI() override;

  private:
    void advance() { _index = (_index + 1) % SHADOW_PER_FRAME_SET; }

  public:
};

} // namespace ya