#include "RenderDeviceState.h"
#include "Render3D/Common/RenderRuntimeHostServices.h"

#include "Render3D/Services/DebugRenderSystem.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Common/RenderOverlay.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Backend/Vulkan/VulkanRender.h"
#include "Render2D/Render2D.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "Render3D/Services/PipelineCoordinator.h"

#include <format>
#include <limits>
#include <vector>

namespace ya
{

void RenderDeviceState::applyViewportResize(Rect2D rect)
{
    if (rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
        return;
    }
    if (_pipelineViewportRect.extent.x == rect.extent.x &&
        _pipelineViewportRect.extent.y == rect.extent.y &&
        _pipelineViewportRect.pos.x == rect.pos.x &&
        _pipelineViewportRect.pos.y == rect.pos.y) {
        return;
    }
    _pipelineViewportRect = rect;
    if (auto* pipeline = getActivePipeline()) {
        pipeline->onViewportResized(rect);
    }
}

void RenderDeviceState::applyPendingMutations()
{
    _pipelineCoordinator.applyPendingChanges();
}

void RenderDeviceState::prepareDerivedState(Scene* scene, float dt)
{
    auto provider = [scene]() -> Scene* { return scene; };
    if (_environmentLightingProcessor) {
        _environmentLightingProcessor->setActiveSceneProvider(provider);
        _environmentLightingProcessor->onUpdate(dt);
    }
    if (_terrainProcessor) {
        _terrainProcessor->setActiveSceneProvider(provider);
        _terrainProcessor->onUpdate(dt);
    }
    if (_gameplayResourceBinding) {
        _gameplayResourceBinding->setActiveSceneProvider(provider);
        _gameplayResourceBinding->onUpdate(dt);
    }
    if (scene) {
        (void)_sharedResourceProvider.getSceneSkyboxDescriptorSet(scene);
        (void)_sharedResourceProvider.getSceneEnvironmentLightingDescriptorSet(scene);
    }
}

void RenderDeviceState::prepareComposePipelines()
{
    if (auto* pipeline = getActivePipeline()) {
        prepareRenderViewportOverlayPipeline(pipeline->getViewportColorFormat(),
                                             pipeline->getViewportDepthFormat());
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::EditorViewportCompose,
            },
            kEditorViewportComposeColorFormat,
            pipeline->getViewportDepthFormat());
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::EditorCanvasPreview,
            },
            kEditorViewportComposeColorFormat);
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::RuntimeUIComposite,
            },
            getViewportDisplayImageFormat());
    }
}

IRenderPipeline* RenderDeviceState::getActivePipeline() const
{
    return _pipelineCoordinator.getActivePipeline();
}

uint64_t RenderDeviceState::getHostTick() const
{
    return _clockState ? _clockState->hostTick : 0;
}

double RenderDeviceState::getElapsedTimeSeconds() const
{
    return _clockState ? static_cast<double>(_clockState->elapsedTimeMS) / 1000.0 : 0.0;
}

GameplayResourceBinding* RenderDeviceState::getGameplayResourceBinding() const
{
    return _gameplayResourceBinding.get();
}

EnvironmentLightingProcessor* RenderDeviceState::getEnvironmentLightingProcessor() const
{
    return _environmentLightingProcessor.get();
}

bool RenderDeviceState::isShadowMappingEnabled() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->isShadowMappingEnabled();
    }
    return false;
}

std::shared_ptr<ImageResource> RenderDeviceState::getShadowDirectionalDepthResource() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getShadowDirectionalDepthResource();
    }
    return nullptr;
}

std::shared_ptr<ImageResource> RenderDeviceState::getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getShadowPointFaceDepthResource(pointLightIndex, faceIndex);
    }
    return nullptr;
}

std::shared_ptr<RenderTexture> RenderDeviceState::getPostprocessOutputImageShared() const
{
    if (const auto* output = publishedViewOutput()) {
        if (output->display && output->display != output->color) {
            return output->display;
        }
    }
    return nullptr;
}

std::shared_ptr<RenderTexture> RenderDeviceState::getPresentationImageShared() const
{
    return _presentationGraphService.getCurrentPresentationImageShared();
}

bool RenderDeviceState::isPostprocessingEnabled() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->isPostprocessingEnabled();
    }
    return false;
}

RenderPipelineDebugOutputCatalog RenderDeviceState::buildPipelineDebugOutputCatalog() const
{
    RenderPipelineDebugOutputCatalog catalog{};
    auto* pipeline = getActivePipeline();
    if (!pipeline) {
        return catalog;
    }

    catalog.bShadowMappingEnabled   = pipeline->isShadowMappingEnabled();
    catalog.shadowDirectionalDepthResource = pipeline->getShadowDirectionalDepthResource();
    catalog.viewportDepthImageOwner = nullptr;
    catalog.bPostprocessingEnabled = pipeline->isPostprocessingEnabled();

    if (const auto* output = publishedViewOutput()) {
        catalog.viewportOutputImageOwner    = output->color;
        catalog.viewportDepthImageOwner     = output->depth;
        catalog.postprocessOutputImageOwner = (output->display && output->display != output->color)
            ? output->display
            : nullptr;
        catalog.bloomExtractOwner           = output->bloomExtract;
        catalog.bloomBlurOwner              = output->bloomBlur;
        catalog.bloomCompositeOwner         = output->bloomComposite;
        return catalog;
    }

    return catalog;
}

Extent2D RenderDeviceState::getViewportExtent() const
{
    if (const auto* output = publishedViewOutput()) {
        if (output->desc.hasExtent()) {
            return output->desc.extent;
        }
        if (auto image = output->displayImage()) {
            return image->getExtent();
        }
    }
    if (_pipelineViewportRect.extent.x > 0 && _pipelineViewportRect.extent.y > 0) {
        return Extent2D::fromVec2(_pipelineViewportRect.extent);
    }
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getViewportExtent();
    }
    return {};
}

DeferredPipelineDebugViews RenderDeviceState::getDeferredPipelineDebugViews() const
{
    if (auto* pipeline = _pipelineCoordinator.getSelectedDeferredPipeline()) {
        return pipeline->buildDebugViews();
    }
    return {};
}

RenderTargetCatalog RenderDeviceState::buildRenderTargetCatalog() const
{
    RenderTargetCatalog catalog{};

    if (auto presentationImage = _presentationGraphService.getCurrentPresentationImageShared()) {
        auto* swapchain = _presentationGraphService.getSwapchain();
        catalog.entries.push_back({
            .label            = "Presentation",
            .owner            = RenderTargetCatalog::Entry::EOwner::Presentation,
            .colorFormats     = {presentationImage->getFormat()},
            .colorAttachments = {presentationImage},
            .extent           = presentationImage->getExtent(),
            .frameBufferCount = swapchain ? swapchain->getImageCount() : 1,
            .bSwapChainTarget = true,
            .bEditable        = false,
        });
    }
    if (auto* pipeline = getActivePipeline()) {
        pipeline->appendRenderTargetEntries(catalog);
    }

    return catalog;
}

void RenderDeviceState::requestRenderTargetFormat(const RenderTargetFormatCommand& command)
{
    _pipelineCoordinator.requestRenderTargetFormat(command);
}

DebugRenderSystem& RenderDeviceState::getDebugRenderSystem() const
{
    return DebugRenderSystem::get();
}

} // namespace ya
