#include "GameEditor/UI/Viewport/EditorViewportCompositor.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Viewport/EditorViewportOverlayRecord.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Render.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include "Render3D/WorldDraw.h"
#include "Scene/Core/Scene.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>

namespace ya
{

namespace
{

std::shared_ptr<RenderTexture> createCanvasPreviewImage(IRender& render, const Extent2D& extent)
{
    if (extent.width == 0 || extent.height == 0) {
        return nullptr;
    }
    return RenderTexture::create(
        *render.getResourceFactory(),
        RenderTextureCreateInfo{
            .label   = "EditorCanvasPreview",
            .width   = extent.width,
            .height  = extent.height,
            .format  = kEditorCanvasPreviewColorFormat,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
        });
}

} // namespace

void EditorViewportCompositor::bindDraw(ScreenDrawPipelines& screen, WorldDrawPipelines& world)
{
    if (_bRecordersBound) {
        return;
    }
    _screenPipelines = &screen;
    _worldPipelines  = &world;
    _viewportScreen.init(screen);
    _canvasScreen.init(screen);
    _world.init(world);
    _bRecordersBound = true;
}

void EditorViewportCompositor::prepare(EFormat::T colorFormat, EFormat::T depthFormat, bool bCanvas)
{
    if (!_bRecordersBound) {
        return;
    }
    if (bCanvas) {
        _canvasScreen.prepare(colorFormat, EFormat::Undefined);
        return;
    }
    _overlayColorFormat = colorFormat;
    _worldDepthFormat   = depthFormat;
    _viewportScreen.prepare(colorFormat, depthFormat);
    _world.prepare(colorFormat, depthFormat);
}

void EditorViewportCompositor::shutdown()
{
    _viewportScreen.destroy();
    _canvasScreen.destroy();
    _world.destroy();
    _bRecordersBound = false;
    _screenPipelines = nullptr;
    _worldPipelines  = nullptr;
    _scenePreview.shutdown();
    _canvasImage.reset();
    _publishedOutput.reset();
}

void EditorViewportCompositor::compose(IRender&                      render,
                                       ICommandBuffer&               commandBuffer,
                                       const RenderViewportSnapshot& snapshot,
                                       EditorLayer&                  layer,
                                       const EditorComposeCamera&    worldCamera,
                                       const Extent2D&               canvasTargetExtent)
{
    // 2D always takes the canvas path: the world graph is disabled, so there
    // is no display image to draw into.
    if (layer.isViewportMode2D()) {
        composeCanvasPreview(render, commandBuffer, layer, canvasTargetExtent);
        return;
    }
    composeAuthoringView(commandBuffer, snapshot, layer, worldCamera);
}

void EditorViewportCompositor::composeCanvasPreview(IRender&        render,
                                                    ICommandBuffer& commandBuffer,
                                                    EditorLayer&    layer,
                                                    const Extent2D& canvasTargetExtent)
{
    ensureCanvasTarget(render, canvasTargetExtent);
    if (!_canvasImage || !_canvasImage->isValid()) {
        _publishedOutput.reset();
        return;
    }

    const glm::vec2 logicalViewport = layer.getViewportSize();
    const Extent2D  logicalExtent{
        .width  = static_cast<uint32_t>(std::max(logicalViewport.x, 0.0f)),
        .height = static_cast<uint32_t>(std::max(logicalViewport.y, 0.0f)),
    };
    const glm::vec2 targetScale{
        static_cast<float>(_canvasImage->getExtent().width) /
            std::max(static_cast<float>(logicalExtent.width), 1.0f),
        static_cast<float>(_canvasImage->getExtent().height) /
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
        // Persistent preview tree: rebuilt only when a mount input changes, not
        // every compose. The tree is the scene's mounts instantiated in
        // Authoring mode -- it lays out and paints, and does not tick or
        // dispatch input, so it can never show state the game does not have.
        uiPreviewSnapshot = _scenePreview.buildSnapshot(*scene,
                                                       layer.uiDocumentStore(),
                                                       logicalExtent,
                                                       uiScale,
                                                       offset);
        pUiPreviewSnapshot = &uiPreviewSnapshot;
    }

    recordRender2DComposePass(&commandBuffer,
                              *_canvasImage,
                              pUiPreviewSnapshot,
                              FRender2DComposePassDesc{
                                  .kind = ERender2DComposePassKind::EditorCanvasPreview,
                                  .logicalExtent = logicalExtent,
                                  .canvasPan  = layer.getCanvasPan(),
                                  .canvasZoom = layer.getCanvasZoom(),
                              },
                              _canvasScreen,
                              [&pSelectionRect, &uiScale, &offset](ScreenDrawList& composeList) {
                                  if (pSelectionRect) {
                                      recordEditorCanvasSelectionOverlay(composeList, *pSelectionRect, uiScale, offset);
                                  }
                              });
    _publishedOutput = _canvasImage;
}

void EditorViewportCompositor::composeAuthoringView(ICommandBuffer&               commandBuffer,
                                                    const RenderViewportSnapshot& snapshot,
                                                    EditorLayer&                  layer,
                                                    const EditorComposeCamera&    worldCamera)
{
    auto color = snapshot.viewportImageOwner;
    if (!color || !color->isValid() || !color->getImageView()) {
        _publishedOutput.reset();
        return;
    }

    auto depth = snapshot.viewDepthOwner;
    const bool bCanOverlay = depth && depth->isValid() && depth->getImageView() &&
                             depth->getExtent() == color->getExtent() &&
                             color->getFormat() == _overlayColorFormat &&
                             depth->getFormat() == _worldDepthFormat;
    if (bCanOverlay) {
        recordViewOverlay(commandBuffer, *color, *depth, layer, worldCamera);
    }
    _publishedOutput = std::move(color);
}

void EditorViewportCompositor::recordViewOverlay(ICommandBuffer&            commandBuffer,
                                                 RenderTexture&             color,
                                                 RenderTexture&             depth,
                                                 EditorLayer&               layer,
                                                 const EditorComposeCamera& worldCamera)
{
    commandBuffer.retireResource(color.getImageShared());
    commandBuffer.retireResource(color.getImageViewShared());
    commandBuffer.retireResources(color.getRetainedResources());
    commandBuffer.transitionImageLayoutAuto(color.getImage(), EImageLayout::ColorAttachmentOptimal);

    commandBuffer.retireResource(depth.getImageShared());
    commandBuffer.retireResource(depth.getImageViewShared());
    commandBuffer.retireResources(depth.getRetainedResources());
    commandBuffer.transitionImageLayoutAuto(depth.getImage(), EImageLayout::DepthStencilAttachmentOptimal);

    const Extent2D extent = color.getExtent();
    commandBuffer.beginRendering(RenderingInfo{
        .label                         = "EditorViewOverlay",
        .bExternalTransitionManagement = true,
        .attachments                   = RenderAttachmentSet{
            .renderArea = Rect2D{
                .pos    = {0.0f, 0.0f},
                .extent = {static_cast<float>(extent.width), static_cast<float>(extent.height)},
            },
            .layerCount = 1,
            .colors     = {
                RenderAttachment{
                    .image         = color.getImage(),
                    .imageView     = color.getImageView(),
                    .loadOp        = EAttachmentLoadOp::Load,
                    .storeOp       = EAttachmentStoreOp::Store,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ColorAttachmentOptimal,
                },
            },
            .depth = RenderAttachment{
                .image         = depth.getImage(),
                .imageView     = depth.getImageView(),
                .loadOp        = EAttachmentLoadOp::Load,
                .storeOp       = EAttachmentStoreOp::Store,
                .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout   = EImageLayout::DepthStencilAttachmentOptimal,
            },
        },
    });

    WorldDrawList world;
    recordEditorViewportWorldOverlays(world, layer, /*bDepthTestedWorld=*/true);
    _world.record(world, WorldDrawTarget{
        .cmd            = &commandBuffer,
        .width          = extent.width,
        .height         = extent.height,
        .colorFormat    = color.getFormat(),
        .depthFormat    = depth.getFormat(),
        .viewProjection = worldCamera.viewProjection,
    });

    ScreenDrawList screen;
    recordEditorViewportScreenOverlays(screen, layer);
    (void)_viewportScreen.record(screen, ScreenDrawTarget{
        .cmd         = &commandBuffer,
        .width       = extent.width,
        .height      = extent.height,
        .colorFormat = color.getFormat(),
    });

    commandBuffer.endRendering();
    commandBuffer.transitionImageLayoutAuto(color.getImage(), EImageLayout::ShaderReadOnlyOptimal);
    commandBuffer.transitionImageLayoutAuto(depth.getImage(), EImageLayout::ShaderReadOnlyOptimal);
}

void EditorViewportCompositor::ensureCanvasTarget(IRender& render, const Extent2D& extent)
{
    if (_canvasImage &&
        _canvasImage->getWidth() == extent.width &&
        _canvasImage->getHeight() == extent.height &&
        _canvasImage->getFormat() == kEditorCanvasPreviewColorFormat) {
        return;
    }
    _canvasImage = createCanvasPreviewImage(render, extent);
}

} // namespace ya
