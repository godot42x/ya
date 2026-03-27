#include "DeferredRenderPipeline.h"

#include "DeferredRenderPath_Phong.h"
#include "DeferredRenderPath_PBR.h"
#include "IDeferredRenderPath.h"
#include "Render/Core/TextureFactory.h"
#include "Scene/SceneManager.h"

namespace ya
{

DeferredRenderPipeline::~DeferredRenderPipeline()
{
    shutdown();
}

void DeferredRenderPipeline::init(const InitDesc& desc)
{
    shutdown();

    _render = desc.render;
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");

    initSharedResources(desc);
    recreatePath(_pathModel);
    _pendingPathModel = _pathModel;

    _render->waitIdle();
}

void DeferredRenderPipeline::tick(const TickDesc& desc)
{
    YA_CORE_ASSERT(desc.cmdBuf, "DeferredRenderPipeline requires a command buffer");

    desc.cmdBuf->debugBeginLabel("Deferred Pipeline");

    const bool bViewportRectValid = desc.viewportRect.extent.x > 0 && desc.viewportRect.extent.y > 0;
    if (!bViewportRectValid) {
        desc.cmdBuf->debugEndLabel();
        return;
    }

    applyPendingPathSwitch();

    if (_skyboxSystem) {
        _skyboxSystem->beginFrame();
    }
    _postProcessStage.beginFrame();

    _viewportRT->flushDirty();
    _gBufferRT->flushDirty();

    auto* sceneManager = desc.sceneManager;
    auto* scene        = sceneManager ? sceneManager->getActiveScene() : nullptr;
    if (!scene) {
        desc.cmdBuf->debugEndLabel();
        return;
    }

    YA_CORE_ASSERT(_path, "DeferredRenderPipeline path was not initialized");
    _path->beginFrame(*this);
    _path->tick(*this, desc, scene);

    desc.cmdBuf->debugEndLabel();
}

void DeferredRenderPipeline::shutdown()
{
    if (_path) {
        _path->shutdown(*this);
        _path.reset();
    }

    shutdownSharedResources();
    _render = nullptr;
}

void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::TreeNode("Deferrer Pipeline")) {
        return;
    }

    static const char* items[] = {"Phong", "PBR"};
    int                pending = static_cast<int>(_pendingPathModel);
    if (ImGui::Combo("Deferred Path", &pending, items, IM_ARRAYSIZE(items))) {
        _pendingPathModel = static_cast<EDeferredPathModel>(pending);
    }
    if (_pendingPathModel != _pathModel) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "(switch pending)");
    }

    ImGui::Checkbox("Reverse Viewport Y", &_bReverseViewportY);

    if (_path) {
        ImGui::Text("Active Path: %s", _path->name());
        _path->renderGUI(*this);
    }

    if (_skyboxSystem) {
        _skyboxSystem->renderGUI();
    }
    _postProcessStage.renderGUI();

    ImGui::TreePop();
}

void DeferredRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    cmdBuf->endRendering(_viewportRI);

    auto* inputTexture = _viewportRT->getCurFrameBuffer()->getColorTexture(0);
    viewportTexture    = _postProcessStage.execute(
        cmdBuf,
        inputTexture,
        _lastTickDesc.viewportRect.extent,
        _lastTickDesc.dt,
        &_lastTickCtx);
}

void DeferredRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    if (_gBufferRT) {
        _gBufferRT->setExtent(newExtent);
    }
    if (_viewportRT) {
        _viewportRT->setExtent(newExtent);
    }

    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();

    _postProcessStage.onViewportResized(newExtent);
}

void DeferredRenderPipeline::ensureDebugSwizzledViews()
{
    auto gBuf2 = _gBufferRT ? _gBufferRT->getCurFrameBuffer()->getColorTexture(2) : nullptr;
    if (!gBuf2) {
        return;
    }

    auto* iv = gBuf2->getImageView();
    if (!iv) {
        return;
    }

    if (_cachedAlbedoSpecImageViewHandle == iv->getHandle() &&
        _debugAlbedoRGBView &&
        _debugSpecularAlphaView) {
        return;
    }

    auto* factory = ITextureFactory::get();
    if (!factory) {
        return;
    }

    auto image = gBuf2->getImageShared();

    {
        ImageViewCreateInfo ci{
            .label       = "GBuffer AlbedoSpec RGB View",
            .viewType    = EImageViewType::View2D,
            .aspectFlags = EImageAspect::Color,
            .components  = ComponentMapping::RGBOnly(),
        };
        _debugAlbedoRGBView = factory->createImageView(image, ci);
        if (_debugAlbedoRGBView) {
            _debugAlbedoRGBView->setDebugName("GBuffer_AlbedoSpec_RGB");
        }
    }

    {
        ImageViewCreateInfo ci{
            .label       = "GBuffer AlbedoSpec Alpha View",
            .viewType    = EImageViewType::View2D,
            .aspectFlags = EImageAspect::Color,
            .components  = ComponentMapping::AlphaToGrayscale(),
        };
        _debugSpecularAlphaView = factory->createImageView(image, ci);
        if (_debugSpecularAlphaView) {
            _debugSpecularAlphaView->setDebugName("GBuffer_AlbedoSpec_Alpha");
        }
    }

    _cachedAlbedoSpecImageViewHandle = iv->getHandle();
    YA_CORE_INFO("Created debug swizzled image views for GBuffer albedoSpecular");
}

void DeferredRenderPipeline::initSharedResources(const InitDesc& desc)
{
    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.windowW),
        .height = static_cast<uint32_t>(desc.windowH),
    };

    _gBufferRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "GBuffer RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = extent,
        .frameBufferCount = 1,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index         = 0,
                    .format        = COLOR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 1,
                    .format        = COLOR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 2,
                    .format        = COLOR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index         = 3,
                .format        = DEPTH_FORMAT,
                .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout   = EImageLayout::DepthStencilAttachmentOptimal,
                .usage         = EImageUsage::DepthStencilAttachment,
            },
        },
    });

    _viewportRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "Deferred Viewport RT",
        .bSwapChainTarget = false,
        .extent           = extent,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index          = 0,
                    .format         = COLOR_FORMAT,
                    .samples        = ESampleCount::Sample_1,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                    .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
        },
    });

    _skyboxSystem = ya::makeShared<SkyBoxSystem>();
    _skyboxSystem->init(IRenderSystem::InitParams{
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "Deferred Skybox Pipeline",
            .viewMask                = 0,
            .colorAttachmentFormats  = {COLOR_FORMAT},
            .depthAttachmentFormat   = DEPTH_FORMAT,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });
    _skyboxSystem->bReverseViewportY = true;

    _postProcessStage.init(PostProcessStage::InitDesc{
        .render      = _render,
        .colorFormat = COLOR_FORMAT,
        .width       = extent.width,
        .height      = extent.height,
    });
}

void DeferredRenderPipeline::shutdownSharedResources()
{
    _postProcessStage.shutdown();
    viewportTexture = nullptr;

    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _cachedAlbedoSpecImageViewHandle = nullptr;

    if (_skyboxSystem) {
        _skyboxSystem->onDestroy();
        _skyboxSystem.reset();
    }

    _viewportRT.reset();
    _gBufferRT.reset();
}

void DeferredRenderPipeline::recreatePath(EDeferredPathModel model)
{
    if (_path) {
        _path->shutdown(*this);
        _path.reset();
    }

    switch (model) {
    case EDeferredPathModel::Phong:
        _path = std::make_unique<DeferredPhongRenderPath>();
        break;
    case EDeferredPathModel::PBR:
        _path = std::make_unique<DeferredPBRRenderPath>();
        break;
    default:
        YA_CORE_ASSERT(false, "Unknown deferred path model");
        return;
    }

    _path->init(*this);
    _pathModel = model;
}

void DeferredRenderPipeline::applyPendingPathSwitch()
{
    if (_pendingPathModel == _pathModel) {
        return;
    }

    YA_CORE_INFO("Switch deferred path: {} -> {}",
                 _pathModel == EDeferredPathModel::Phong ? "Phong" : "PBR",
                 _pendingPathModel == EDeferredPathModel::Phong ? "Phong" : "PBR");

    _render->waitIdle();
    recreatePath(_pendingPathModel);
}

} // namespace ya
