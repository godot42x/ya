#include "Render3D/Deferred/Features/EntityIdFeature.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/EntityIdPass.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Deferred/DeferredPassNames.h"
#include "Render3D/RenderFrameData.h"

namespace ya
{

EntityIdFeature::Resolved EntityIdFeature::resolve(const Settings& settings,
                                                   const Facts&    facts,
                                                   const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    resolved.target = ViewTargetRequirement{
        .role   = EViewAttachment::EntityId,
        .format = EFormat::R32_UINT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::TransferSrc,
    };
    return resolved;
}

Result<EntityIdFeature::Prepared, FeatureError> EntityIdFeature::prepareBindings(const Resolved&       resolved,
                                                                                 const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.submission == nullptr || ctx.stage == nullptr || ctx.frameData == nullptr) {
        return FeatureError::MissingContext;
    }

    EntityIdPass::FrameUBO ubo{};
    ubo.viewProj = ctx.frameData->viewProjection;
    ubo.view     = ctx.frameData->view;
    if (!writeUniformPassBinding(*ctx.submission,
                                 ctx.render,
                                 ctx.stage->getFrameDSL(),
                                 ctx.alignment,
                                 &ubo,
                                 sizeof(ubo),
                                 prepared.bindings.frame)) {
        return FeatureError::AllocationFailed;
    }
    return prepared;
}

Result<EntityIdFeature::GraphOutputs, FeatureError> EntityIdFeature::append(RenderGraph&               graph,
                                                                            EntityIdPass*              stage,
                                                                            const FeatureGraphContext& ctx,
                                                                            const Facts&               facts,
                                                                            const Resolved&            resolved,
                                                                            const Prepared&            prepared,
                                                                            const Inputs&              inputs) const
{
    GraphOutputs outputs;
    if (!resolved.bEnabled) {
        return outputs;
    }
    if (stage == nullptr || ctx.viewLease == nullptr || ctx.stageCtx == nullptr) {
        return FeatureError::MissingContext;
    }

    const std::shared_ptr<RenderTexture> target = ctx.viewLease->find(resolved.target.role);
    if (target == nullptr) {
        return FeatureError::MissingDependency;
    }
    outputs.entityId = graph.importTexture(
        makeImportedTextureDesc(*target, "DeferredView.EntityId", EImageLayout::ColorAttachmentOptimal, resolved.target.usage));

    // Billboards are drawn by the overlay pass, which runs before this one, so
    // carry their camera-facing quads into the id pass to keep the id target
    // aligned with what is visible.
    std::vector<EntityIdBillboard> billboards;
    if (facts.overlayInputs != nullptr) {
        billboards.reserve(facts.overlayInputs->billboards.size());
        for (const auto& billboard : facts.overlayInputs->billboards) {
            billboards.push_back(EntityIdBillboard{
                .worldCenter = billboard.worldCenter,
                .worldSize   = billboard.worldSize,
                .entityId    = billboard.entityId,
            });
        }
    }

    const Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kEntityId, ctx.viewId),
        [outputs, renderArea, inputs](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = renderArea,
                .layerCount = 1,
                .colors = {{
                    .color       = outputs.entityId,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ColorAttachmentOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = inputs.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx = *ctx.stageCtx, stage, inputs, prepared, billboards = std::move(billboards)](RGRenderContext& rgCtx) {
            const auto viewExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            if (stageCtx.frameData != nullptr) {
                stage->execute(&rgCtx.getCommandBuffer(),
                               viewExtent.width,
                               viewExtent.height,
                               stageCtx.frameData->projection * stageCtx.frameData->view,
                               stageCtx.frameData->view,
                               *stageCtx.frameData,
                               inputs.skinningDS,
                               prepared.bindings,
                               billboards);
            }
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
