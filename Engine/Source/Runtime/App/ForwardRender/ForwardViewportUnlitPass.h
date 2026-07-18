#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderAttachmentFormats.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/UnlitMaterial.h"
#include "Runtime/App/ForwardRender/ForwardViewportLitPasses.h"

#include "Test.Unlit.glsl.h"

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

class ForwardViewportUnlitPass
{
  public:
    using UnlitFrameUBO = glsl_types::Test::Unlit::FrameUBO;
    using ShadingPipelineVariant = ForwardViewportLitPasses::ShadingPipelineVariant;

    struct UnlitPC
    {
        alignas(16) glm::mat4 modelMatrix{1.0f};
        alignas(4) int32_t    skinningPaletteIndex = -1;
    };

    struct InitDesc
    {
        IRender*                     render = nullptr;
        IRenderPass*                 renderPass = nullptr;
        PipelineRenderingInfo        pipelineRenderingInfo = {};
        stdptr<IDescriptorSetLayout> skinningDSL;
        std::function<uint64_t()>    getFrameIndex;
        std::function<double()>      getElapsedTimeSeconds;
    };

    struct DrawContext
    {
        const RenderStageContext& stageCtx;
        DescriptorSetHandle       skinningDS = nullptr;
        std::function<void(ICommandBuffer*, uint32_t, uint32_t)> setViewportAndScissor;
    };

    void init(const InitDesc& desc);
    void destroy();
    void beginFrame();
    void prepare(const RenderStageContext& ctx);
    void refreshPipelineFormats(const RenderAttachmentFormats& formats);
    void draw(const DrawContext& ctx);
  private:
    void initUnlit(const InitDesc& desc);
    void prepareUnlit(const RenderStageContext& ctx);
    void prepareUnlitMaterials(const RenderFrameData& fd);

    IRender* _render = nullptr;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    std::function<uint64_t()> _getFrameIndex;
    std::function<double()>   _getElapsedTimeSeconds;

    stdptr<IDescriptorSetLayout> _unlitFrameDSL;
    stdptr<IDescriptorSetLayout> _unlitParamDSL;
    stdptr<IDescriptorSetLayout> _unlitResourceDSL;
    ShadingPipelineVariant       _unlitStatic;
    ShadingPipelineVariant       _unlitSkinned;

    static constexpr uint32_t UNLIT_FRAME_SLOTS = 8;
    uint32_t                  _unlitFrameSlot = 0;
    stdptr<IDescriptorPool>   _unlitFrameDSP;
    DescriptorSetHandle       _unlitFrameDSs[UNLIT_FRAME_SLOTS]{};
    stdptr<IBuffer>           _unlitFrameUBOs[UNLIT_FRAME_SLOTS]{};

    MaterialDescPool<UnlitMaterial, UnlitMaterial::ParamUBO> _unlitMatPool;
    bool _unlitPoolRecreated = false;
};

} // namespace ya
