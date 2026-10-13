#include "Render3D/Deferred/Features/GBufferFeature.h"

#include "RHI/Render.h"

#include "Core/Log.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Deferred/DeferredPassNames.h"
#include "Render3D/Deferred/GBufferStage.h"

namespace ya
{

void GBufferFeature::init(IRender* render,
                          stdptr<IDescriptorSetLayout> frameAndLightDSL,
                          stdptr<IDescriptorSetLayout> skinningDSL)
{
    if (_stage == nullptr) {
        _stage = ya::makeShared<GBufferStage>();
    }
    _stage->init(render, std::move(frameAndLightDSL), std::move(skinningDSL));
}

void GBufferFeature::shutdown()
{
    if (_stage) {
        _stage->destroy();
        _stage.reset();
    }
}

void GBufferFeature::beginFrame()
{
    if (_stage) {
        _stage->beginFrame();
    }
}

void GBufferFeature::prepare(const RenderStageContext& ctx)
{
    if (_stage) {
        _stage->prepare(ctx);
    }
}

void GBufferFeature::refreshDeviceFormats(const DeferredAttachmentFormats& formats)
{
    if (_stage) {
        _stage->refreshPipelineFormats(formats);
    }
}

GBufferFeature::Resolved GBufferFeature::resolve(const Settings& settings,
                                                 const Facts&    facts,
                                                 const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    if (!resolved.bEnabled) {
        return resolved;
    }

    resolved.outputs.albedo = ViewAttachmentDesc{
        .role    = EViewAttachment::GBuffer0,
        .format  = settings.albedoFormat,
        .usage   = settings.colorUsage,
        .samples = ESampleCount::Sample_1,
    };
    resolved.outputs.normal = ViewAttachmentDesc{
        .role    = EViewAttachment::GBuffer1,
        .format  = settings.normalFormat,
        .usage   = settings.colorUsage,
        .samples = ESampleCount::Sample_1,
    };
    resolved.outputs.orm = ViewAttachmentDesc{
        .role    = EViewAttachment::GBuffer2,
        .format  = settings.ormFormat,
        .usage   = settings.colorUsage,
        .samples = ESampleCount::Sample_1,
    };
    resolved.outputs.shading = ViewAttachmentDesc{
        .role    = EViewAttachment::GBuffer3,
        .format  = settings.shadingFormat,
        .usage   = settings.colorUsage,
        .samples = ESampleCount::Sample_1,
    };
    resolved.outputs.depth = ViewAttachmentDesc{
        .role    = EViewAttachment::SceneDepth,
        .format  = settings.depthFormat,
        .usage   = settings.depthUsage,
        .samples = ESampleCount::Sample_1,
        .isDepth = true,
    };
    return resolved;
}

Result<GBufferFeature::Prepared, FeatureError> GBufferFeature::prepareBindings(const Resolved&       resolved,
                                                                               const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }

    prepared.frameAndLightDS = ctx.frameAndLightDS;
    prepared.skinningDS      = ctx.skinningDS;
    return prepared;
}

Result<GBufferFeature::GraphOutputs, FeatureError> GBufferFeature::append(RenderGraph&               graph,
                                                                         Stage*                     stage,
                                                                         const FeatureGraphContext& ctx,
                                                                         const Facts&               facts,
                                                                         const Resolved&            resolved,
                                                                         const Prepared&            prepared,
                                                                         const Inputs&              inputs) const
{
    GraphOutputs outputs;
    if (!resolved.bEnabled || stage == nullptr || ctx.viewLease == nullptr || ctx.stageCtx == nullptr) {
        return outputs;
    }

    const auto importTarget = [&](const ViewAttachmentDesc& attachment, std::string_view label) {
        const std::shared_ptr<RenderTexture> target = ctx.viewLease->find(attachment.role);
        YA_CORE_ASSERT(target != nullptr, "GBuffer target missing role {}", static_cast<uint32_t>(attachment.role));
        return graph.importTexture(
            makeImportedTextureDesc(*target, label, EImageLayout::ShaderReadOnlyOptimal, attachment.usage));
    };

    outputs.albedo  = importTarget(resolved.outputs.albedo, "DeferredGBuffer.Color0");
    outputs.normal  = importTarget(resolved.outputs.normal, "DeferredGBuffer.Color1");
    outputs.orm     = importTarget(resolved.outputs.orm, "DeferredGBuffer.Color2");
    outputs.shading = importTarget(resolved.outputs.shading, "DeferredGBuffer.Color3");
    outputs.depth   = importTarget(resolved.outputs.depth, "DeferredGBuffer.Depth");

    const RenderStageContext& stageCtx   = *ctx.stageCtx;
    const Rect2D              renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};

    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kGBuffer, ctx.viewId),
        [outputs, inputs, predecessor = ctx.predecessor, renderArea](RGPassBuilder& passBuilder) {
            if (predecessor.has_value()) {
                passBuilder.dependsOn(*predecessor);
            }
            passBuilder.uniformRead(inputs.frame, inputs.frameRange);
            passBuilder.uniformRead(inputs.light, inputs.lightRange);
            passBuilder.storageRead(inputs.skinning);
            passBuilder.declareRaster({
                .renderArea = renderArea,
                .layerCount = 1,
                .colors = {
                    {.color = outputs.albedo, .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = outputs.normal, .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = outputs.orm, .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = outputs.shading, .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                },
                .depth = RGDepthAttachmentDesc{
                    .depth       = outputs.depth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx, prepared, inputs, bReverseViewportY = ctx.bReverseViewportY, stage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            [[maybe_unused]] IBuffer* const frameBuffer    = rgCtx.resolveBuffer(inputs.frame);
            [[maybe_unused]] IBuffer* const lightBuffer    = rgCtx.resolveBuffer(inputs.light);
            [[maybe_unused]] IBuffer* const skinningBuffer = rgCtx.resolveBuffer(inputs.skinning);

            rgCtx.beginDeclaredRasterRendering();

            const auto  gbufferExtent = rasterParams.getRenderExtent();
            const auto  vpW           = gbufferExtent.width;
            const auto  vpH           = gbufferExtent.height;
            const float gbVpY         = bReverseViewportY ? static_cast<float>(vpH) : 0.0f;
            const float gbVpH         = bReverseViewportY ? -static_cast<float>(vpH) : static_cast<float>(vpH);
            rgCtx.getCommandBuffer().setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
            rgCtx.getCommandBuffer().setScissor(0, 0, vpW, vpH);

            stage->execute(stageCtx, GBufferStage::FrameInputs{
                .frameAndLightDescriptorSet = prepared.frameAndLightDS,
                .skinningDescriptorSet      = prepared.skinningDS,
            });
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
