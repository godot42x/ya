#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"
#include "Runtime/App/App.h"

#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

void ForwardRenderPipeline::rebuildShadowViews()
{
    YA_CORE_ASSERT(_render, "ForwardRenderPipeline requires render device");
    YA_CORE_ASSERT(depthRT, "ForwardRenderPipeline requires shadow render target");

    auto* frameBuffer  = depthRT->getCurFrameBuffer();
    auto* depthTexture = frameBuffer ? frameBuffer->getDepthTexture() : nullptr;
    YA_CORE_ASSERT(depthTexture, "Shadow render target depth texture is null");

    auto tf        = _render->getTextureFactory();
    auto shadowImg = depthTexture->getImageShared();
    YA_CORE_ASSERT(shadowImg, "Shadow render target image is null");

    shadowDirectionalDepthIV = tf->createImageView(shadowImg, ImageViewCreateInfo{
        .label          = "Shadow Map Directional Depth IV",
        .viewType       = EImageViewType::View2D,
        .aspectFlags    = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    });

    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; ++i) {
        shadowPointCubeIVs[i] = tf->createImageView(shadowImg, ImageViewCreateInfo{
            .label          = std::format("Shadow Point[{}] CubeIV", i),
            .viewType       = EImageViewType::ViewCube,
            .aspectFlags    = EImageAspect::Depth,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 1 + i * 6,
            .layerCount     = 6,
        });

        for (uint32_t f = 0; f < 6; ++f) {
            shadowPointFaceIVs[i][f] = tf->createImageView(shadowImg, ImageViewCreateInfo{
                .label          = std::format("Shadow Point[{}] Face[{}]", i, f),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Depth,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 1 + i * 6 + f,
                .layerCount     = 1,
            });
        }
    }

    std::vector<DescriptorImageInfo> pointInfos(MAX_POINT_LIGHTS);
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; ++i) {
        pointInfos[i] = DescriptorImageInfo{
            .imageView   = shadowPointCubeIVs[i] ? shadowPointCubeIVs[i]->getHandle() : ImageViewHandle{},
            .sampler     = shadowSampler ? shadowSampler->getHandle() : SamplerHandle{},
            .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
        };
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(depthBufferShadowDS, 0, shadowDirectionalDepthIV.get(), shadowSampler.get()),
        WriteDescriptorSet{
            .dstSet           = depthBufferShadowDS,
            .dstBinding       = 1,
            .dstArrayElement  = 0,
            .descriptorType   = EPipelineDescriptorType::CombinedImageSampler,
            .descriptorCount  = MAX_POINT_LIGHTS,
            .imageInfos       = pointInfos,
        },
    });
}

void ForwardRenderPipeline::init(const InitDesc& desc)
{
    _render = desc.render;
    _bViewportPassOpen = false;

    // ── Viewport RT ──────────────────────────────────────────────
    viewportRT = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Viewport RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = {.width = static_cast<uint32_t>(desc.windowW), .height = static_cast<uint32_t>(desc.windowH)},
        .frameBufferCount = 1,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index = 0, .format = LINEAR_FORMAT, .samples = ESampleCount::Sample_1,
                    .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                    .usage = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index = 1, .format = DEPTH_FORMAT, .samples = ESampleCount::Sample_1,
                .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store,
                .stencilLoadOp = EAttachmentLoadOp::Clear, .stencilStoreOp = EAttachmentStoreOp::Store,
                .initialLayout = EImageLayout::DepthStencilAttachmentOptimal, .finalLayout = EImageLayout::DepthStencilAttachmentOptimal,
                .usage = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
            },
        },
    });
    YA_CORE_ASSERT(viewportRT, "Failed to create viewport render target");

    // ── PostProcess ──────────────────────────────────────────────
    _postProcessStage.init(PostProcessingStage::InitDesc{
        .render = _render, .colorFormat = LINEAR_FORMAT,
        .width = static_cast<uint32_t>(desc.windowW), .height = static_cast<uint32_t>(desc.windowH),
    });
    _deleter.push("PostProcessStage", [this](void*) { _postProcessStage.shutdown(); });

    // ── Shadow Depth RT ──────────────────────────────────────────
    depthRT = ya::createRenderTarget(RenderTargetCreateInfo{
        .label = "Shadow Map RenderTarget", .renderingMode = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false, .extent = {.width = 512, .height = 512},
        .frameBufferCount = 1, .layerCount = 1 + MAX_POINT_LIGHTS * 6,
        .attachments = {
            .depthAttach = AttachmentDescription{
                .index = 0, .format = SHADOW_MAPPING_DEPTH_BUFFER_FORMAT, .samples = ESampleCount::Sample_1,
                .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store,
                .initialLayout = EImageLayout::DepthStencilAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                .usage = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
                .imageCreateFlags = EImageCreateFlag::CubeCompatible,
            },
        },
    });
    _deleter.push("DepthRT", [this](void*) { depthRT.reset(); });

    // ── Shadow DS + Sampler ──────────────────────────────────────
    _descriptorPool = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label = "ForwardPipeline Descriptor Pool", .maxSets = 200,
        .poolSizes = {{.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = (1 + MAX_POINT_LIGHTS)}},
    });
    _deleter.push("OwnedDescriptorPool", [this](void*) { _descriptorPool.reset(); });

    depthBufferDSL = IDescriptorSetLayout::create(_render, DescriptorSetLayoutDesc{
        .label = "DepthBuffer_DSL",
        .bindings = {
            {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = MAX_POINT_LIGHTS, .stageFlags = EShaderStage::Fragment},
        },
    });
    depthBufferShadowDS = _descriptorPool->allocateDescriptorSets(depthBufferDSL);
    _render->as<VulkanRender>()->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, depthBufferShadowDS.ptr, "DepthBuffer_Shadow_DS");
    _deleter.push("DepthBufferDSL", [this](void*) { depthBufferDSL.reset(); });

    shadowSampler = Sampler::create(SamplerDesc{
        .label = "shadow", .minFilter = EFilter::Linear, .magFilter = EFilter::Linear,
        .mipmapMode = ESamplerMipmapMode::Linear,
        .addressModeU = ESamplerAddressMode::ClampToBorder, .addressModeV = ESamplerAddressMode::ClampToBorder,
        .addressModeW = ESamplerAddressMode::ClampToBorder,
        .borderColor = SamplerDesc::BorderColor{.type = SamplerDesc::EBorderColor::FloatOpaqueWhite, .color = {1, 1, 1, 1}},
    });
    _deleter.push("ShadowSampler", [this](void*) { shadowSampler.reset(); });

    _render->waitIdle();
    rebuildShadowViews();

    _deleter.push("Shadow ImageViews", [this](void*) {
        shadowDirectionalDepthIV.reset();
        for (auto& iv : shadowPointCubeIVs) iv.reset();
        for (auto& faces : shadowPointFaceIVs) for (auto& iv : faces) iv.reset();
    });

    // ── ShadowStage ──────────────────────────────────────────────
    _shadowStage = ya::makeShared<ShadowStage>();
    _shadowStage->setRenderTarget(depthRT);
    _shadowStage->init(_render);

    // ── ViewportStage ────────────────────────────────────────────
    PipelineRenderingInfo viewportPRI{
        .label = "Forward Viewport",
        .colorAttachmentFormats = {LINEAR_FORMAT},
        .depthAttachmentFormat = DEPTH_FORMAT,
    };
    _viewportStage = ya::makeShared<ForwardViewportStage>();
    _viewportStage->initWithDesc(ForwardViewportStage::InitDesc{
        .render                = _render,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = viewportPRI,
        .depthBufferShadowDS   = depthBufferShadowDS,
        .bShadowMapping        = bShadowMapping,
    });

    _deleter.push("Stages", [this](void*) {
        if (_viewportStage) { _viewportStage->destroy(); _viewportStage.reset(); }
        if (_shadowStage) { _shadowStage->destroy(); _shadowStage.reset(); }
    });

    _render->waitIdle();
}

void ForwardRenderPipeline::tick(const TickDesc& desc)
{
    YA_CORE_ASSERT(desc.cmdBuf, "ForwardRenderPipeline requires command buffer");

    if (desc.viewportRect.extent.x <= 0 || desc.viewportRect.extent.y <= 0) {
        return;
    }

    const uint32_t vpW = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH = static_cast<uint32_t>(desc.viewportRect.extent.y);

    // Build stage context
    RenderStageContext stageCtx{
        .cmdBuf         = desc.cmdBuf,
        .frameData      = desc.frameData,
        .flightIndex    = 0,
        .deltaTime      = desc.dt,
        .viewportExtent = {.width = vpW, .height = vpH},
    };

    _postProcessStage.beginFrame();

    const bool bViewportDirty = viewportRT && viewportRT->bDirty;
    const bool bShadowDirty   = depthRT && depthRT->bDirty;

    if (viewportRT) {
        viewportRT->flushDirty();
    }
    if (depthRT) {
        depthRT->flushDirty();
    }

    if (bViewportDirty && _viewportStage) {
        _viewportStage->refreshPipelineFormats(viewportRT.get());
    }
    if (bShadowDirty) {
        rebuildShadowViews();
        if (_shadowStage) {
            _shadowStage->refreshPipelineFromRenderTarget();
        }
    }

    // ── Shadow Pass ──────────────────────────────────────────────
    if (bShadowMapping && _shadowStage) {
        _shadowStage->prepare(stageCtx);

        RenderingInfo shadowMapRI{
            .label = "Shadow Map Pass",
            .renderArea = Rect2D{.pos = {0, 0}, .extent = depthRT->getExtent().toVec2()},
            .depthClearValue = ClearValue(1.0f, 0),
            .renderTarget = depthRT.get(),
        };
        desc.cmdBuf->beginRendering(shadowMapRI);
        _shadowStage->execute(stageCtx);
        desc.cmdBuf->endRendering(shadowMapRI);
    }

    // ── Viewport Pass ────────────────────────────────────────────
    _viewportStage->prepare(stageCtx);

    auto extent = Extent2D::fromVec2(desc.viewportRect.extent / desc.viewportFrameBufferScale);
    viewportRT->setExtent(extent);

    RenderingInfo ri{
        .label = "ViewPort",
        .renderArea = Rect2D{.pos = {0, 0}, .extent = viewportRT->getExtent().toVec2()},
        .layerCount = 1,
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
        .depthClearValue = ClearValue(1.0f, 0),
        .renderTarget = viewportRT.get(),
    };

    desc.cmdBuf->beginRendering(ri);
    _bViewportPassOpen = true;

    // Update stage context with actual viewport extent
    stageCtx.viewportExtent = viewportRT->getExtent();
    _viewportStage->execute(stageCtx);

    // Viewport pass left open for App-level 2D rendering; App calls endViewportPass().
    _viewportRI   = ri;
    _lastTickCtx  = desc.frameData ? desc.frameData->toFrameContext() : FrameContext{
        .view = desc.view, .projection = desc.projection, .cameraPos = desc.cameraPos,
    };
    _lastTickCtx.extent = viewportRT->getExtent();
    _lastTickDesc       = desc;
}

void ForwardRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    if (!_bViewportPassOpen) return;

    cmdBuf->endRendering(_viewportRI);
    _bViewportPassOpen = false;

    auto  fb           = viewportRT->getCurFrameBuffer();
    auto* inputTexture = bMSAA ? fb->getResolveTexture() : fb->getColorTexture(0);

    viewportTexture = _postProcessStage.execute(
        cmdBuf, inputTexture, _lastTickDesc.viewportRect.extent, &_lastTickCtx);

    YA_CORE_ASSERT(viewportTexture, "Failed to get viewport texture for postprocessing");
}

void ForwardRenderPipeline::shutdown()
{
    _bViewportPassOpen = false;
    _deleter.clear();
}

void ForwardRenderPipeline::renderGUI()
{
    if (!ImGui::TreeNode("Forward Render Pipeline")) return;

    if (_shadowStage) _shadowStage->renderGUI();
    if (_viewportStage) _viewportStage->renderGUI();
    _postProcessStage.renderGUI();

    if (ImGui::Checkbox("Shadow Mapping", &bShadowMapping)) {
        if (_viewportStage) _viewportStage->setShadowMappingEnabled(bShadowMapping);
    }
    ImGui::TreePop();
}

void ForwardRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };
    if (viewportRT) viewportRT->setExtent(newExtent);
    _postProcessStage.onViewportResized(newExtent);
}

Extent2D ForwardRenderPipeline::getViewportExtent() const
{
    return viewportRT ? viewportRT->getExtent() : Extent2D{};
}

IImageView* ForwardRenderPipeline::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) return nullptr;
    return shadowPointFaceIVs[pointLightIndex][faceIndex].get();
}

} // namespace ya
