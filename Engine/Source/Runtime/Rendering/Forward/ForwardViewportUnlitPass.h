#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderAttachmentFormats.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/UnlitMaterial.h"
#include "Runtime/Rendering/Common/IRenderRuntimeServices.h"
#include "Runtime/Rendering/Forward/ForwardViewportLitPasses.h"

#include "Test.Unlit.glsl.h"

#include <array>
#include <functional>
#include <glm/glm.hpp>

namespace ya
{

struct RenderStageContext;
struct RenderFrameData;
class ForwardFrameResourceSet;
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
        stdptr<IDescriptorSetLayout> unlitFrameDSL;
        IRenderRuntimeServices*      runtimeServices = nullptr;
    };

    struct DrawContext
    {
        const RenderStageContext& stageCtx;
        DescriptorSetHandle       skinningDS = nullptr;
        DescriptorSetHandle       unlitFrameDescriptorSet = nullptr;
        std::function<void(ICommandBuffer*, uint32_t, uint32_t)> setViewportAndScissor;
    };

    void init(const InitDesc& desc);
    void destroy();
    void beginFrame();
    /// Build the current frame's Unlit CPU payload; the pipeline uploads it
    /// through ForwardFrameResourceSet (FG-701).
    void prepare(const RenderStageContext& ctx, UnlitFrameUBO& outFrame);
    void refreshPipelineFormats(const RenderAttachmentFormats& formats);
    void draw(const DrawContext& ctx);
    [[nodiscard]] const ShadingPipelineVariant& getStaticVariant() const { return _unlitStatic; }
    [[nodiscard]] const ShadingPipelineVariant& getSkinnedVariant() const { return _unlitSkinned; }
  private:
    void initUnlit(const InitDesc& desc);
    void prepareUnlit(const RenderStageContext& ctx, UnlitFrameUBO& outFrame);
    void prepareUnlitMaterials(const RenderFrameData& fd);

    IRender* _render = nullptr;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    IRenderRuntimeServices* _runtimeServices = nullptr;

    stdptr<IDescriptorSetLayout> _unlitFrameDSL;
    stdptr<IDescriptorSetLayout> _unlitParamDSL;
    stdptr<IDescriptorSetLayout> _unlitResourceDSL;
    ShadingPipelineVariant       _unlitStatic;
    ShadingPipelineVariant       _unlitSkinned;

    MaterialDescPool<UnlitMaterial, UnlitMaterial::ParamUBO> _unlitMatPool;
    bool _unlitPoolRecreated = false;
};

} // namespace ya
