
#include "App.h"
#include "Runtime/App/ForwardRenderPipeline.h"
#include "Core/Debug/RenderDocCapture.h"


namespace ya
{
extern ClearValue colorClearValue;
extern ClearValue depthClearValue;


void App::tickRenderPipeline(float dt)
{
    YA_PROFILE_FUNCTION()
    auto render = getRender();


    if (_windowSize.x <= 0 || _windowSize.y <= 0) {
        YA_CORE_INFO("{}x{}: Window minimized, skipping frame", _windowSize.x, _windowSize.y);
        return;
    }

    // BUG: crash on resizing while renderdoc open
    //  How to fix it?
    if (_renderDocCapture) {
        _renderDocCapture->onFrameBegin();
    }

    // Process pending viewport resize before rendering
    if (_editorLayer) {
        Rect2D pendingRect;
        if (_editorLayer->getPendingViewportResize(pendingRect)) {
            onSceneViewportResized(pendingRect);
        }
    }

    // this can avoid bunch black mosaic when resizing
    // remove this if swapchain size == every rt's size
    _render->waitIdle();


    // ===== Get swapchain image index =====
    int32_t imageIndex = -1;
    if (!render->begin(&imageIndex)) {
        return;
    }
    if (imageIndex < 0) {
        YA_CORE_WARN("Invalid image index ({}), skipping frame render", imageIndex);
        return;
    }

    // ===== Single CommandBuffer for both Scene and UI Passes =====
    auto cmdBuf = _commandBuffers[imageIndex];
    cmdBuf->reset();
    cmdBuf->begin();

    Entity* runtimeCamera = getPrimaryCamera();
    if (runtimeCamera && runtimeCamera->isValid())
    {
        auto cc = runtimeCamera->getComponent<CameraComponent>();
        auto tc = runtimeCamera->getComponent<TransformComponent>();

        const Extent2D ext = _pipeline->getViewportExtent();
        cameraController.update(*tc, *cc, inputManager, ext, dt);
        cc->setAspectRatio(static_cast<float>(ext.width) / static_cast<float>(ext.height));
    }

    glm::mat4 view;
    glm::mat4 projection;
    bool bUseRuntimeCamera = _appState == AppState::Runtime &&
                             runtimeCamera && runtimeCamera->isValid() &&
                             runtimeCamera->hasComponent<CameraComponent>();
    if (bUseRuntimeCamera) {
        auto cc   = runtimeCamera->getComponent<CameraComponent>();
        view       = cc->getFreeView();
        projection = cc->getProjection();
    }
    else {
        view       = camera.getViewMatrix();
        projection = camera.getProjectionMatrix();
    }
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 cameraPos(invView[3]);

    _pipeline->bRenderMirror = bRenderMirror;
    _pipeline->tick(ForwardRenderPipeline::TickDesc{
        .cmdBuf                   = cmdBuf.get(),
        .dt                       = dt,
        .view                     = view,
        .projection               = projection,
        .cameraPos                = cameraPos,
        .viewportRect             = _viewportRect,
        .viewportFrameBufferScale = _viewportFrameBufferScale,
        .appMode                  = static_cast<int>(_appMode),
        .clicked                  = &clicked,
        .editorLayer              = _editorLayer,
    });
    YA_CORE_ASSERT(_pipeline->viewportTexture, "Failed to get viewport texture for postprocessing");

    // --- MARK: Editor pass
    {
        YA_PROFILE_SCOPE("Screen pass")

        RenderingInfo ri{
            .label      = "Screen",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = _screenRT->getExtent().toVec2(),
            },
            .layerCount       = 1,
            .colorClearValues = {ClearValue::Black()},
            //
            .renderTarget = _screenRT.get(),
        };

        cmdBuf->beginRendering(ri);

        // Render ImGui
        auto& imManager = ImGuiManager::get();
        imManager.beginFrame();
        {
            this->renderGUI(dt);
        }
        imManager.endFrame();
        imManager.render();

        if (render->getAPI() == ERenderAPI::Vulkan) {
            imManager.submitVulkan(cmdBuf->getHandleAs<VkCommandBuffer>());
        }

        cmdBuf->endRendering(EndRenderingInfo{
            .renderTarget = _screenRT.get(),
        });
    }
    cmdBuf->end();

    // TODO: multi-thread rendering
    // ===== Single Submit: Wait on imageAvailable, Signal renderFinished, Set fence =====
    // render->submitToQueue(
    //     {cmdBuf->getHandle()},
    //     {render->getCurrentImageAvailableSemaphore()},    // Wait for swapchain image
    //     {render->getRenderFinishedSemaphore(imageIndex)}, // Signal when all rendering done
    //     render->getCurrentFrameFence());                  // Signal fence when done

    // // ===== Present: Wait on renderFinished =====
    // int result = render->presentImage(imageIndex, {render->getRenderFinishedSemaphore(imageIndex)});

    // // Check for swapchain recreation needed
    // if (result == 2 /* VK_SUBOPTIMAL_KHR */) {
    //     YA_CORE_INFO("Swapchain suboptimal detected in App, will recreate next frame");
    // }
    // // Advance to next frame
    // render->advanceFrame();
    render->end(imageIndex, {cmdBuf->getHandle()});

    if (_renderDocCapture) {
        _renderDocCapture->onFrameEnd();
    }
}

} // namespace ya