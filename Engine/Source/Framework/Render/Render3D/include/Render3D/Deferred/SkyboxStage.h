#pragma once

#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Common/ViewOverlayInputs.h"
#include "Render3D/Stage/IRenderStage.h"
#include "Render3D/Material/UnlitMaterial.h"

#include "Skybox.slang.h"

namespace ya
{

struct RenderFrameData;

/// The skybox pass's device object: its pipeline, both layouts and the draw.
/// SkyboxFeature owns it; the frame UBO is the feature's prepared binding and
/// the packet (cubemap set, mesh, availability) arrives through
/// `ViewOverlayFrameInputs::SkyboxInput`.
struct YA_RENDER_3D_API SkyboxStage : public IRenderStage
{
    using FrameUBO = slang_types::Skybox::FrameUBO;

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T DEPTH_FORMAT  = EFormat::D32_SFLOAT;

    IRender* _render = nullptr;

    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _resourceDSL;

    bool bReverseViewportY = true;

    SkyboxStage() : IRenderStage("Skybox") {}

    void init(IRender* render) override;
    void destroy() override;
    void execute(const RenderStageContext& ctx) override;
    /// Submission-scoped PSO warmup; the stage holds no per-View state.
    void beginFrame();
    void refreshPipelineFormats(const DeferredAttachmentFormats& formats);
    void executeSkybox(const RenderStageContext& ctx,
                       const ViewOverlayFrameInputs::SkyboxInput& skyboxInput,
                       DescriptorSetHandle frameDescriptorSet);

    /// The skybox frame UBO payload: view without translation, full projection.
    [[nodiscard]] static FrameUBO buildFrameData(const RenderFrameData& frameData);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameDSL() const { return _frameDSL; }

  private:
    void drawSkybox(const RenderStageContext& ctx,
                    const ViewOverlayFrameInputs::SkyboxInput& skyboxInput,
                    DescriptorSetHandle frameDescriptorSet);
};

} // namespace ya
