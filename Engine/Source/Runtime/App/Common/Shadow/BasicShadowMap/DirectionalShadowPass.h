#pragma once

#include "BasicShadowPayload.h"
#include "Runtime/App/Common/Shadow/ShadowTypes.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Image.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Stage/IRenderStage.h"

#include "CombineShadowMappingGenerate.slang.h"

namespace ya
{

struct IRender;
struct ICommandBuffer;
struct RenderFrameData;
struct RenderShadingDrawBuckets;
struct RenderDrawItem;

// ═══════════════════════════════════════════════════════════════════════════
// DirectionalShadowPass
// Renders the single directional-light depth map using direct draw calls.
// ═══════════════════════════════════════════════════════════════════════════

class DirectionalShadowPass
{
  public:
    using FrameUBO          = slang_types::CombineShadowMappingGenerate::FrameData;
    using ModelPushConstant = slang_types::CombineShadowMappingGenerate::PushConstants;

    void init(IRender* render, Extent2D shadowExtent);
    void destroy();

    void prepare(const BasicShadowFramePayload& payload);

    void execute(ICommandBuffer* cmdBuf, const BasicShadowFramePayload& payload);
    [[nodiscard]] std::optional<RGPassHandle> appendGraphPass(
        RenderGraph& graph,
        const BasicShadowFramePayload& payload,
        std::optional<RGPassHandle> dependency = std::nullopt);

    void setShadowExtent(Extent2D extent) { _shadowExtent = extent; }
    void refreshPipeline(EFormat::T depthFormat);
    void setDepthAttachment(stdptr<IImage> image, stdptr<IImageView> view);
    [[nodiscard]] IImageView* getDepthView() const { return _depthView.get(); }

  private:
    struct ShadowPipelineVariant
    {
        stdptr<IGraphicsPipeline> pipeline;
        stdptr<IPipelineLayout>   pipelineLayout;
    };

    struct PerFlightResources
    {
        stdptr<IBuffer>     frameUBO;
        DescriptorSetHandle frameDS       = nullptr;
        stdptr<IBuffer>     skinningSSBO;
        DescriptorSetHandle skinningDS    = nullptr;
    };

    void ensureSkinningCapacity(uint32_t paletteCount);

    IRender* _render       = nullptr;
    Extent2D _shadowExtent = {.width = 1024, .height = 1024};

    ShadowPipelineVariant        _staticVariant;
    ShadowPipelineVariant        _skinnedVariant;
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    stdptr<IDescriptorPool>      _dsp;
    GraphicsPipelineCreateInfo   _pipelineCI{};

    stdptr<IImage>     _depthImage;
    stdptr<IImageView> _depthView;
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    std::array<PerFlightResources, MAX_FLIGHTS_IN_FLIGHT> _perFlight{};
    uint32_t _skinningCapacity = 0;
};

} // namespace ya
