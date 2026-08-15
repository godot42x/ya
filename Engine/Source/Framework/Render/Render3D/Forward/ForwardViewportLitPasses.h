#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/RenderAttachmentFormats.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Material/MaterialDescPool.h"
#include "Render3D/Material/PBRMaterial.h"
#include "Render3D/Material/PhongMaterial.h"
#include "Render3D/Common/IRenderRuntimeServices.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"

#include "PBRForward.slang.h"
#include "PhongLit.slang.h"

#include <array>
#include <glm/glm.hpp>

namespace ya
{

struct RenderStageContext;
struct RenderFrameData;
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
        stdptr<IDescriptorSetLayout> pbrFrameDSL;
        stdptr<IDescriptorSetLayout> phongFrameDSL;
        IRenderRuntimeServices*    runtimeServices = nullptr;
    };

    struct DrawContext
    {
        const RenderStageContext& stageCtx;
        DescriptorSetHandle       environmentLightingDescriptorSet = nullptr;
        DescriptorSetHandle       skyboxDescriptorSet = nullptr;
        DescriptorSetHandle       depthBufferShadowDS = nullptr;
        DescriptorSetHandle       skinningDS = nullptr;
        DescriptorSetHandle       pbrFrameDescriptorSet = nullptr;
        DescriptorSetHandle       phongFrameDescriptorSet = nullptr;
        bool                      bReverseViewportY = true;
    };

    void init(const InitDesc& desc);
    void destroy();
    void beginFrame();
    /// Build the current frame's PBR/Phong CPU payloads; the pipeline uploads
    /// them through ForwardFrameResourceSet (FG-701).
    void prepare(const RenderStageContext& ctx, ForwardFrameResourceSet::FramePayloads& outPayloads);
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
    void preparePBR(const RenderStageContext& ctx, PBRFrameUBO& outFrame, PBRLightUBO& outLight);
    void preparePhong(const RenderStageContext& ctx, PhongFrameUBO& outFrame, PhongLightUBO& outLight, PhongDebugUBO& outDebug);
    void preparePBRMaterials(const RenderFrameData& fd);
    void preparePhongMaterials(const RenderFrameData& fd);
    void fillPBRLightFromFrameData(const RenderFrameData& fd, PBRLightUBO& outLight);
    void fillPhongLightFromFrameData(const RenderFrameData& fd, PhongLightUBO& outLight);

    [[nodiscard]] DescriptorImageInfo getDescriptorImageInfo(const TextureBinding& tb) const;

    IRender* _render = nullptr;

    stdptr<IDescriptorSetLayout> _pbrFrameDSL;
    stdptr<IDescriptorSetLayout> _pbrResourceDSL;
    stdptr<IDescriptorSetLayout> _pbrParamDSL;
    ShadingPipelineVariant       _pbrStatic;
    ShadingPipelineVariant       _pbrSkinned;
    MaterialDescPool<PBRMaterial, PBRParamUBO>             _pbrMatPool;
    bool                                                   _pbrPoolRecreated = false;
    bool                                                   _bEnablePBRDiffuseIBL = true;
    bool                                                   _bEnablePBRSpecularIBL = true;

    stdptr<IDescriptorSetLayout> _phongFrameDSL;
    stdptr<IDescriptorSetLayout> _phongResourceDSL;
    stdptr<IDescriptorSetLayout> _phongParamDSL;
    ShadingPipelineVariant       _phongStatic;
    ShadingPipelineVariant       _phongSkinned;
    MaterialDescPool<PhongMaterial, PhongMaterial::ParamUBO> _phongMatPool;
    bool                                                     _phongPoolRecreated = false;
    PhongDebugUBO                                            _phongDebug{};

    ShadowRuntimeState      _shadowState{};
    stdptr<IDescriptorSetLayout> _skinningDSL;
    IRenderRuntimeServices* _runtimeServices = nullptr;
};

} // namespace ya
