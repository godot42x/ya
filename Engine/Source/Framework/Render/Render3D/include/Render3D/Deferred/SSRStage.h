#pragma once

#include "Graph/RenderGraph.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Stage/IRenderStage.h"

#include "DeferredRender.SSR.slang.h"

namespace ya
{

struct IFrameBuffer;

/// The SSR pass's device object: PSO and layouts -- no runtime settings and no
/// per-View state. `SSRFeature` owns this stage and drives
/// init/refresh/beginFrame/shutdown; the settings and the per-View frame UBO
/// live on the feature and its Prepared binding.
///
/// This is the template a new screen-space effect follows: a device object that
/// owns only GPU state, a feature that owns the settings and the per-View data,
/// and one wiring row in the recipe at the extension point where it runs.
struct YA_RENDER_3D_API SSRStage : public IRenderStage
{
    using FrameData = slang_types::DeferredRender::SSR::FrameData;

    /// The reflection pass writes back into the View's colour stream, so its
    /// target format is the View colour format the feature resolves.
    static constexpr EFormat::T OUTPUT_FORMAT = EFormat::R16G16B16A16_SFLOAT;

    IRender*                     _render = nullptr;
    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    // Frame and input layouts are owned by this stage; the per-View sets and
    // upload slices are owned by the SSR feature's prepared binding.
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _inputDSL;

    SSRStage() : IRenderStage("SSR") {}

    void init(IRender* render) override;
    void destroy() override;
    /// Submission-scoped PSO warmup; the stage holds no per-View state.
    void beginFrame();
    void execute(const RenderStageContext& ctx) override;
    void refreshPipelineFormat(EFormat::T colorFormat);

    /// The frame UBO payload from explicit values: the feature's resolve builds
    /// it from its resolved settings, so nothing here reads applied stage state.
    [[nodiscard]] static FrameData buildFrameData(const RenderFrameData& frameData,
                                                  Extent2D               viewExtent,
                                                  float                  maxDistance,
                                                  float                  thickness,
                                                  float                  intensity,
                                                  uint32_t               stepCount,
                                                  bool                   bReverseY);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameDSL() const { return _frameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getInputDSL() const { return _inputDSL; }
    [[nodiscard]] IPipelineLayout*             getPipelineLayout() const { return _pipelineLayout.get(); }
    [[nodiscard]] IGraphicsPipeline*           getPipeline() const { return _pipeline.get(); }

    void writeInputDescriptors(
        DescriptorSetHandle                          inputDS,
        const RGRenderContext::RGPassBindingContext& binding,
        RGTextureHandle                              sceneColor,
        RGTextureHandle                              normal,
        RGTextureHandle                              depth) const;
};

} // namespace ya
