#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderAttachmentFormats.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PBRMaterial.h"
#include "Render/Material/PhongMaterial.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowRuntimeState.h"

#include "PBRForward.slang.h"
#include "PhongLit.slang.h"

#include <array>
#include <functional>
#include <glm/glm.hpp>

namespace ya
{

struct RenderStageContext;
struct RenderFrameData;
class ICommandBuffer;
class IRenderPass;
class IBuffer;
class IDescriptorSetLayout;
class IDescriptorPool;
class IRender;

class ForwardViewportLitPasses
{
  public:
    using PBRPushConstant = slang_types::PBRForward::PushConstants;
    using PBRFrameUBO     = slang_types::PBRForward::FrameData;
    using PBRLightUBO     = slang_types::PBRForward::LightData;
    using PBRParamUBO     = slang_types::PBRForward::PBRParamsData;

    using PhongFrameUBO   = slang_types::PhongLit::FrameData;
    using PhongLightUBO   = slang_types::PhongLit::LightData;
    using PhongDebugUBO   = slang_types::PhongLit::DebugData;

    struct PhongModelPC
    {
        glm::mat4 modelMat;
        int32_t   skinningPaletteIndex = -1;
    };

    struct ShadingPipelineVariant
    {
        stdptr<IPipelineLayout>    pipelineLayout;
        stdptr<IGraphicsPipeline>  pipeline;
        GraphicsPipelineCreateInfo pipelineCI{};
    };

    struct InitDesc
    {
        IRender*                   render = nullptr;
        IRenderPass*               renderPass = nullptr;
        PipelineRenderingInfo      pipelineRenderingInfo = {};
        ShadowRuntimeState         shadowState = {};
        stdptr<IDescriptorSetLayout> skinningDSL;
        std::function<uint64_t()>  getFrameIndex;
        std::function<double()>    getElapsedTimeSeconds;
    };

    struct DrawContext
    {
        const RenderStageContext& stageCtx;
        DescriptorSetHandle       environmentLightingDescriptorSet = nullptr;
        DescriptorSetHandle       skyboxDescriptorSet = nullptr;
        DescriptorSetHandle       depthBufferShadowDS = nullptr;
        DescriptorSetHandle       skinningDS = nullptr;
        std::function<void(ICommandBuffer*, uint32_t, uint32_t)> setViewportAndScissor;
    };

    void init(const InitDesc& desc);
    void destroy();
    void beginFrame();
    void prepare(const RenderStageContext& ctx);
    void refreshPipelineFormats(const RenderAttachmentFormats& formats);
    void applyShadowState(const ShadowRuntimeState& shadowState);

    void drawPBR(const DrawContext& ctx);
    void drawPhong(const DrawContext& ctx);

    [[nodiscard]] PhongDebugUBO& phongDebug() { return _phongDebug; }
    [[nodiscard]] const PhongDebugUBO& phongDebug() const { return _phongDebug; }
    [[nodiscard]] const ShadingPipelineVariant& getPhongStaticVariant() const { return _phongStatic; }
    [[nodiscard]] const ShadingPipelineVariant& getPhongSkinnedVariant() const { return _phongSkinned; }
    [[nodiscard]] const ShadingPipelineVariant& getPBRStaticVariant() const { return _pbrStatic; }
    [[nodiscard]] const ShadingPipelineVariant& getPBRSkinnedVariant() const { return _pbrSkinned; }

  private:
    void initPBR(const InitDesc& desc);
    void initPhong(const InitDesc& desc);
    void preparePBR(const RenderStageContext& ctx);
    void preparePhong(const RenderStageContext& ctx);
    void preparePBRMaterials(const RenderFrameData& fd);
    void preparePhongMaterials(const RenderFrameData& fd);
    void fillPBRLightFromFrameData(const RenderFrameData& fd);
    void fillPhongLightFromFrameData(const RenderFrameData& fd);

    [[nodiscard]] DescriptorImageInfo getDescriptorImageInfo(const TextureBinding& tb) const;

    IRender* _render = nullptr;

    stdptr<IDescriptorSetLayout> _pbrFrameDSL;
    stdptr<IDescriptorSetLayout> _pbrResourceDSL;
    stdptr<IDescriptorSetLayout> _pbrParamDSL;
    ShadingPipelineVariant       _pbrStatic;
    ShadingPipelineVariant       _pbrSkinned;
    stdptr<IDescriptorPool>      _pbrFrameDSP;
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _pbrFrameDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _pbrFrameUBO{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _pbrLightUBO{};
    MaterialDescPool<PBRMaterial, PBRParamUBO>             _pbrMatPool;
    bool                                                   _pbrPoolRecreated = false;
    PBRLightUBO                                            _pbrLight{};
    bool                                                   _bEnablePBRDiffuseIBL = true;
    bool                                                   _bEnablePBRSpecularIBL = true;

    stdptr<IDescriptorSetLayout> _phongFrameDSL;
    stdptr<IDescriptorSetLayout> _phongResourceDSL;
    stdptr<IDescriptorSetLayout> _phongParamDSL;
    ShadingPipelineVariant       _phongStatic;
    ShadingPipelineVariant       _phongSkinned;
    stdptr<IDescriptorPool>      _phongFrameDSP;
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _phongFrameDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _phongFrameUBO{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _phongLightUBO{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _phongDebugUBO{};
    MaterialDescPool<PhongMaterial, PhongMaterial::ParamUBO> _phongMatPool;
    bool                                                     _phongPoolRecreated = false;
    PhongLightUBO                                            _phongLight{};
    PhongDebugUBO                                            _phongDebug{};

    ShadowRuntimeState      _shadowState{};
    stdptr<IDescriptorSetLayout> _skinningDSL;
    std::function<uint64_t()> _getFrameIndex;
    std::function<double()>   _getElapsedTimeSeconds;
};

} // namespace ya
