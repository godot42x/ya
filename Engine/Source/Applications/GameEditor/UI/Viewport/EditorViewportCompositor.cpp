#include "GameEditor/UI/Viewport/EditorViewportCompositor.h"

#include "GameEditor/EditorLayer.h"
#include "GameRuntime/App.h"
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

void EditorViewportCompositor::bindDraw(ScreenDrawPipelines& screen, WorldDrawPipelines& world)
{
    if (_bRecordersBound) {
        return;
    }
    _screenPipelines = &screen;
    _worldPipelines  = &world;
    _viewportScreen.init(screen);
    _gameUiScreen.init(screen);
    _world.init(world);
    _bRecordersBound = true;
}

void EditorViewportCompositor::prepare(EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (!_bRecordersBound) {
        return;
    }
    _overlayColorFormat = colorFormat;
    _worldDepthFormat   = depthFormat;
    _viewportScreen.prepare(colorFormat, depthFormat);
    _gameUiScreen.prepare(colorFormat, EFormat::Undefined);
    _world.prepare(colorFormat, depthFormat);
}

void EditorViewportCompositor::shutdown()
{
    _viewportScreen.destroy();
    _gameUiScreen.destroy();
    _world.destroy();
    _bRecordersBound = false;
    _screenPipelines = nullptr;
    _worldPipelines  = nullptr;
    _scenePreview.shutdown();
    _publishedOutput.reset();
}

void EditorViewportCompositor::compose(ICommandBuffer&               commandBuffer,
                                       const RenderViewportSnapshot& snapshot,
                                       EditorLayer&                  layer,
                                       const EditorComposeCamera&    worldCamera)
{
    auto color = snapshot.viewportImageOwner;
    if (!color || !color->isValid() || !color->getImageView()) {
        _publishedOutput.reset();
        return;
    }

    composeMountedGameUI(commandBuffer, *color, layer);

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

void EditorViewportCompositor::composeMountedGameUI(ICommandBuffer& commandBuffer,
                                                     RenderTexture&  color,
                                                     EditorLayer&    layer)
{
    App* app = App::get();
    if (app && (app->isRuntimeMode() || app->isSimulationMode())) {
        return;
    }
    Scene* scene = layer.getViewportInteractionScene();
    if (!scene || scene->getWidgetEntries().empty() || !color.isValid()) {
        return;
    }

    const glm::vec2 logicalViewport = layer.getViewportSize();
    const Extent2D  logicalExtent{
        .width  = static_cast<uint32_t>(std::max(logicalViewport.x, 0.0f)),
        .height = static_cast<uint32_t>(std::max(logicalViewport.y, 0.0f)),
    };
    if (logicalExtent.width == 0 || logicalExtent.height == 0) {
        return;
    }

    const glm::vec2 uiScale{
        static_cast<float>(color.getExtent().width) / static_cast<float>(logicalExtent.width),
        static_cast<float>(color.getExtent().height) / static_cast<float>(logicalExtent.height),
    };
    const UIFrameSnapshot snapshot = _scenePreview.buildSnapshot(*scene,
                                                                 layer.uiDocumentStore(),
                                                                 logicalExtent,
                                                                 uiScale,
                                                                 {0.0f, 0.0f});
    recordRender2DComposePass(&commandBuffer,
                              color,
                              &snapshot,
                              FRender2DComposePassDesc{
                                  .kind          = ERender2DComposePassKind::RuntimeUIComposite,
                                  .logicalExtent = logicalExtent,
                              },
                              _gameUiScreen,
                              {});
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

} // namespace ya
