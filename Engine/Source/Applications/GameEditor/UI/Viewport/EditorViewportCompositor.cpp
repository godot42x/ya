#include "GameEditor/UI/Viewport/EditorViewportCompositor.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Viewport/EditorViewportOverlayRecord.h"
#include "GameRuntime/HostViewState.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "RHI/Render.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include "Scene/Core/Scene.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>

namespace ya
{

namespace
{

std::shared_ptr<RenderTexture> createEditorViewportImage(IRender& render, const Extent2D& extent)
{
    if (extent.width == 0 || extent.height == 0) {
        return nullptr;
    }
    return RenderTexture::create(
        *render.getResourceFactory(),
        RenderTextureCreateInfo{
            .label   = "EditorViewportComposed",
            .width   = extent.width,
            .height  = extent.height,
            .format  = kEditorViewportComposeColorFormat,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
        });
}

FRender2DComposePassDesc worldComposeDesc(const HostViewState& hostView)
{
    return FRender2DComposePassDesc{
        .kind   = ERender2DComposePassKind::EditorViewportCompose,
        .camera = {
            .position       = hostView.cameraPos,
            .view           = hostView.view,
            .projection     = hostView.projection,
            .viewProjection = hostView.projection * hostView.view,
        },
    };
}

} // namespace

void EditorViewportCompositor::shutdown()
{
    _composedViewportImage.reset();
    _sourceViewportTexture.reset();
    _sourceViewportImage.reset();
    _sourceViewportImageView.reset();
    _scenePreviewErrors.clear();
}

void EditorViewportCompositor::compose(IRender&                      render,
                                       ICommandBuffer&               commandBuffer,
                                       const RenderViewportSnapshot& snapshot,
                                       EditorLayer&                  layer,
                                       const HostViewState&          hostView,
                                       const Extent2D&               canvasTargetExtent)
{
    // 2D always takes the canvas path: the world graph is disabled, so a
    // world-sourced compose would leave the viewport empty during startup.
    if (layer.isViewportMode2D()) {
        composeCanvasPreview(render, commandBuffer, layer, canvasTargetExtent);
        return;
    }

    auto source = snapshot.viewportImageOwner;
    if (!source || !source->getImageShared() || !source->getImageView()) {
        composeWorldFallback(render, commandBuffer, layer, hostView, canvasTargetExtent);
        return;
    }
    composeWorldFromScene(render, commandBuffer, snapshot, layer, hostView);
}

void EditorViewportCompositor::composeCanvasPreview(IRender&        render,
                                                    ICommandBuffer& commandBuffer,
                                                    EditorLayer&    layer,
                                                    const Extent2D& canvasTargetExtent)
{
    ensureCanvasTarget(render, canvasTargetExtent);
    if (!_composedViewportImage || !_composedViewportImage->isValid()) {
        return;
    }

    const glm::vec2 logicalViewport = layer.getViewportSize();
    const Extent2D  logicalExtent{
        .width  = static_cast<uint32_t>(std::max(logicalViewport.x, 0.0f)),
        .height = static_cast<uint32_t>(std::max(logicalViewport.y, 0.0f)),
    };
    const glm::vec2 targetScale{
        static_cast<float>(_composedViewportImage->getExtent().width) /
            std::max(static_cast<float>(logicalExtent.width), 1.0f),
        static_cast<float>(_composedViewportImage->getExtent().height) /
            std::max(static_cast<float>(logicalExtent.height), 1.0f),
    };
    // Tree-local logical px -> canvas target px (framebuffer scale), then
    // canvas pan/zoom. viewportToCanvas applies the inverse mapping.
    const glm::vec2 uiScale = targetScale * layer.getCanvasZoom();
    const glm::vec2 offset  = layer.getCanvasPan() * targetScale;

    UIFrameSnapshot        uiPreviewSnapshot;
    const UIFrameSnapshot* pUiPreviewSnapshot = nullptr;
    const Rect2D*          pSelectionRect     = nullptr;
    if (layer.getEditorUIDesignerSession().hasDocument()) {
        uiPreviewSnapshot  = layer.getEditorUIDesignerSession().buildPreviewSnapshot(uiScale, offset);
        pUiPreviewSnapshot = &uiPreviewSnapshot;
        pSelectionRect = layer.getEditorUIDesignerSession().getSelectedLayoutRect();
    }
    else if (Scene* scene = layer.getViewportInteractionScene()) {
        WidgetTree previewTree(logicalExtent);
        previewTree.setTextureSource(&gameUITextureSource());
        std::string errors;
        (void)mountSceneAutoMountEntries(*scene, previewTree,
                                         layer.uiDocumentStore(),
                                         [&errors](std::string_view message) {
                                             errors.append(message);
                                             errors.push_back('\n');
                                         });
        if (errors != _scenePreviewErrors) {
            _scenePreviewErrors = std::move(errors);
            if (!_scenePreviewErrors.empty()) {
                YA_CORE_WARN("Editor scene UI preview mount errors:\n{}", _scenePreviewErrors);
            }
        }
        uiPreviewSnapshot = previewTree.buildSnapshot(UIFrameBuildContext{
            .uiScale         = uiScale,
            .offset          = offset,
            .textureResolver = &resolveGameUITexture,
        });
        pUiPreviewSnapshot = &uiPreviewSnapshot;
    }

    recordRender2DComposePass(&commandBuffer,
                              *_composedViewportImage,
                              nullptr,
                              pUiPreviewSnapshot,
                              FRender2DComposePassDesc{
                                  .kind = ERender2DComposePassKind::EditorCanvasPreview,
                                  .logicalViewportExtent = logicalExtent,
                                  .canvasPan  = layer.getCanvasPan(),
                                  .canvasZoom = layer.getCanvasZoom(),
                              },
                              [&]() {
                                  if (pSelectionRect) {
                                      recordEditorCanvasSelectionOverlay(*pSelectionRect, uiScale, offset);
                                  }
                              });
}

void EditorViewportCompositor::composeWorldFallback(IRender&                   render,
                                                    ICommandBuffer&            commandBuffer,
                                                    EditorLayer&               layer,
                                                    const HostViewState&       hostView,
                                                    const Extent2D&            canvasTargetExtent)
{
    Extent2D fallback = canvasTargetExtent;
    if (fallback.width == 0 || fallback.height == 0) {
        fallback = Extent2D::fromVec2(layer.getViewportSize());
    }
    if (fallback.width == 0 || fallback.height == 0) {
        fallback = {.width = 1280, .height = 720};
    }
    ensureCanvasTarget(render, fallback);
    if (!_composedViewportImage || !_composedViewportImage->isValid()) {
        return;
    }
    commandBuffer.retireResource(_composedViewportImage->getImageShared());
    commandBuffer.retireResource(_composedViewportImage->getImageViewShared());
    commandBuffer.transitionImageLayoutAuto(_composedViewportImage->getImage(),
                                            EImageLayout::ColorAttachmentOptimal);
    FRender2DComposePassDesc desc = worldComposeDesc(hostView);
    recordRender2DComposePass(
        &commandBuffer,
        *_composedViewportImage,
        nullptr,
        nullptr,
        desc,
        [&]() { recordEditorWorldViewportOverlays(layer, /*bDepthTestedWorld=*/false); });
}

void EditorViewportCompositor::composeWorldFromScene(IRender&                      render,
                                                     ICommandBuffer&               commandBuffer,
                                                     const RenderViewportSnapshot& snapshot,
                                                     EditorLayer&                  layer,
                                                     const HostViewState&          hostView)
{
    auto source = snapshot.viewportImageOwner;
    ensureTarget(render, *source);
    if (!_composedViewportImage || !_composedViewportImage->isValid()) {
        return;
    }

    commandBuffer.retireResource(source->getImageShared());
    commandBuffer.retireResource(source->getImageViewShared());
    commandBuffer.retireResources(source->getRetainedResources());
    commandBuffer.retireResource(_composedViewportImage->getImageShared());
    commandBuffer.retireResource(_composedViewportImage->getImageViewShared());

    commandBuffer.transitionImageLayoutAuto(source->getImage(), EImageLayout::ShaderReadOnlyOptimal);
    commandBuffer.transitionImageLayoutAuto(_composedViewportImage->getImage(),
                                            EImageLayout::ColorAttachmentOptimal);

    const auto depthOwner   = snapshot.viewportDepthOwner;
    const bool bAttachDepth = depthOwner && depthOwner->isValid() &&
                              depthOwner->getExtent() == _composedViewportImage->getExtent();
    if (bAttachDepth) {
        commandBuffer.retireResource(depthOwner->getImageShared());
        commandBuffer.retireResource(depthOwner->getImageViewShared());
        commandBuffer.retireResources(depthOwner->getRetainedResources());
        commandBuffer.transitionImageLayoutAuto(depthOwner->getImage(),
                                                EImageLayout::DepthStencilAttachmentOptimal);
    }

    FRender2DComposePassDesc desc = worldComposeDesc(hostView);
    desc.sceneSourceTexture = resolveSourceTexture(*source);
    recordRender2DComposePass(
        &commandBuffer,
        *_composedViewportImage,
        bAttachDepth ? depthOwner.get() : nullptr,
        nullptr,
        desc,
        [&]() { recordEditorWorldViewportOverlays(layer, bAttachDepth); });
}

std::shared_ptr<Texture> EditorViewportCompositor::resolveSourceTexture(const RenderTexture& source)
{
    auto sourceImage     = source.getImageShared();
    auto sourceImageView = source.getImageViewShared();
    if (!sourceImage || !sourceImageView) {
        _sourceViewportTexture.reset();
        _sourceViewportImage.reset();
        _sourceViewportImageView.reset();
        return nullptr;
    }

    if (_sourceViewportTexture &&
        _sourceViewportImage == sourceImage &&
        _sourceViewportImageView == sourceImageView) {
        return _sourceViewportTexture;
    }

    _sourceViewportImage     = std::move(sourceImage);
    _sourceViewportImageView = std::move(sourceImageView);
    _sourceViewportTexture   = Texture::wrap(_sourceViewportImage,
                                           _sourceViewportImageView,
                                           "EditorViewportCompositionSource");
    return _sourceViewportTexture;
}

void EditorViewportCompositor::ensureTarget(IRender& render, const RenderTexture& source)
{
    const Extent2D sourceExtent = source.getExtent();
    if (_composedViewportImage &&
        _composedViewportImage->getWidth() == sourceExtent.width &&
        _composedViewportImage->getHeight() == sourceExtent.height &&
        _composedViewportImage->getFormat() == kEditorViewportComposeColorFormat) {
        return;
    }
    _composedViewportImage = createEditorViewportImage(render, sourceExtent);
}

void EditorViewportCompositor::ensureCanvasTarget(IRender& render, const Extent2D& extent)
{
    if (_composedViewportImage &&
        _composedViewportImage->getWidth() == extent.width &&
        _composedViewportImage->getHeight() == extent.height &&
        _composedViewportImage->getFormat() == kEditorViewportComposeColorFormat) {
        return;
    }
    _composedViewportImage = createEditorViewportImage(render, extent);
}

} // namespace ya
