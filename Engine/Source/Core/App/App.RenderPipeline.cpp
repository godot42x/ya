
#include "App.h"
#include "Core/Debug/RenderDocCapture.h"
#include "Core/Math/Math.h"
#include "Core/UI/UIManager.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/MirrorComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/System/Render/PhongMaterialSystem.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"
#include "Render/Pipelines/BasicPostprocessing.h"


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

    // update runtime camera
    Entity* runtimeCamera = getPrimaryCamera();
    if (runtimeCamera && runtimeCamera->isValid())
    {

        auto cc = runtimeCamera->getComponent<CameraComponent>();
        auto tc = runtimeCamera->getComponent<TransformComponent>();

        const Extent2D& ext = _viewportRT->getExtent();
        cameraController.update(*tc, *cc, inputManager, ext, dt);
        // Update aspect ratio for runtime camera
        cc->setAspectRatio(static_cast<float>(ext.width) / static_cast<float>(ext.height));
    }

    // MARK: grab frame resources
    FrameContext ctx;
    {

        // Get primary camera from ECS for runtime/simulation mode

        bool bUseRuntimeCamera = _appState == AppState::Runtime &&
                                 runtimeCamera && runtimeCamera->isValid() &&
                                 runtimeCamera->hasComponent<CameraComponent>();

        if (bUseRuntimeCamera) {
            // Use runtime camera (Entity with CameraComponent)
            auto cc        = runtimeCamera->getComponent<CameraComponent>();
            ctx.view       = cc->getFreeView();
            ctx.projection = cc->getProjection();
        }
        else {
            // Use editor camera (FreeCamera)
            ctx.view       = camera.getViewMatrix();
            ctx.projection = camera.getProjectionMatrix();
        }

        // Extract camera position from view matrix inverse
        glm::mat4 invView = glm::inverse(ctx.view);
        ctx.cameraPos     = glm::vec3(invView[3]);

        auto cameraForward = glm::vec3(ctx.view[0][2], ctx.view[1][2], ctx.view[2][2]);

        // grab lights once
        ctx.bHasDirectionalLight = false;
        auto scene               = getSceneManager()->getActiveScene();
        for (const auto& [et, dlc, tc] :
             scene->getRegistry().view<DirectionalLightComponent, TransformComponent>().each())
        {
            ctx.directionalLight.direction      = glm::normalize(tc.getForward());
            ctx.directionalLight.ambient        = dlc._ambient;
            ctx.directionalLight.diffuse        = dlc._diffuse;
            ctx.directionalLight.specular       = dlc._specular;
            ctx.directionalLight.projection     = FMath::orthographic(-dlc._orthoHalfWidth, dlc._orthoHalfWidth, -dlc._orthoHalfHeight, dlc._orthoHalfHeight, dlc._nearPlane, dlc._farPlane);
            ctx.directionalLight.view           = FMath::lookAt(-ctx.directionalLight.direction * dlc._lightDistance, glm::vec3(0.0f), glm::vec3(0, 1, 0));
            ctx.directionalLight.viewProjection = ctx.directionalLight.projection * ctx.directionalLight.view;
            ctx.bHasDirectionalLight            = true;
            break;
        }
        if (!ctx.bHasDirectionalLight) {
            for (const auto& [et, dlc] :
                 scene->getRegistry().view<DirectionalLightComponent>().each())
            {
                ctx.directionalLight.direction      = glm::normalize(dlc._direction);
                ctx.directionalLight.ambient        = dlc._ambient;
                ctx.directionalLight.diffuse        = dlc._diffuse;
                ctx.directionalLight.specular       = dlc._specular;
                ctx.directionalLight.projection     = FMath::orthographic(-dlc._orthoHalfWidth, dlc._orthoHalfWidth, -dlc._orthoHalfHeight, dlc._orthoHalfHeight, dlc._nearPlane, dlc._farPlane);
                ctx.directionalLight.view           = FMath::lookAt(-ctx.directionalLight.direction * dlc._lightDistance, glm::vec3(0.0f), glm::vec3(0, 1, 0));
                ctx.directionalLight.viewProjection = ctx.directionalLight.projection * ctx.directionalLight.view;
                ctx.bHasDirectionalLight            = true;
                break;
            }
        }
        ctx.numPointLights = 0;
        for (const auto& [et, plc, tc] :
             scene->getRegistry().view<PointLightComponent, TransformComponent>().each())
        {
            if (ctx.numPointLights >= MAX_POINT_LIGHTS) break;
            auto& pl       = ctx.pointLights[ctx.numPointLights];
            pl.type        = static_cast<float>(plc._type);
            pl.constant    = plc._constant;
            pl.linear      = plc._linear;
            pl.quadratic   = plc._quadratic;
            pl.position    = tc._position;
            pl.ambient     = plc._ambient;
            pl.diffuse     = plc._diffuse;
            pl.specular    = plc._specular;
            pl.spotDir     = tc.getForward();
            pl.innerCutOff = glm::cos(glm::radians(plc._innerConeAngle));
            pl.outerCutOff = glm::cos(glm::radians(plc._outerConeAngle));
            ++ctx.numPointLights;
        }
    }


    auto phongSys = _phongMaterialSystem->as<PhongMaterialSystem>();
    if (bShadowMapping && ctx.bHasDirectionalLight) {
        phongSys->setDirectionalShadowMappingEnabled(true);
    }
    else {
        phongSys->setDirectionalShadowMappingEnabled(false);
    }

    beginFrame();



    // MARK: shadow
    if (bShadowMapping)
    {
        RenderingInfo shadowMapRI{
            .label      = "Shadow Map Pass",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = _depthRT->getExtent().toVec2(),
            },
            .colorClearValues = {},
            .depthClearValue  = ClearValue(1.0f, 0),
            .renderTarget     = _depthRT.get(),
        };
        cmdBuf->beginRendering(shadowMapRI);
        {
            ctx.extent = _depthRT->getExtent();
            _shadowMappingSystem->tick(cmdBuf.get(), dt, &ctx);
        }
        cmdBuf->endRendering(EndRenderingInfo{.renderTarget = _depthRT.get()});
    }



    bool bViewPortRectValid = _viewportRect.extent.x > 0 && _viewportRect.extent.y > 0;

    // MARK: Mirror Rendering
    //  (Pre scene render some mirror entities and render to texture for later compositing) ---
    if (bRenderMirror && bViewPortRectValid)
    {
        YA_PROFILE_SCOPE("Mirror Pass")
        // Mirror / Rear-view mirror / Screen-in-screen rendering (TEMPORARY, for demo/testing only)
        auto         scene = getSceneManager()->getActiveScene();
        auto         view  = scene->getRegistry().view<TransformComponent, MirrorComponent>();
        FrameContext ctxCopy;
        bHasMirror = false;
        for (auto [entity, tc, mc] : view.each())
        {
            bHasMirror         = true;
            ctxCopy.viewOwner  = entity;
            ctxCopy.projection = ctx.projection;

            // Calculate mirror normal
            const glm::quat rotQuat      = glm::quat(glm::radians(tc.getWorldRotation()));
            glm::vec3       mirrorNormal = glm::normalize(rotQuat * FMath::Vector::WorldForward);
            glm::vec3       mirrorPos    = tc.getWorldPosition();

            // Extract camera forward direction from view matrix
            // glm::vec3 cameraForward = -glm::vec3(ctx.view[0][2], ctx.view[1][2], ctx.view[2][2]);
            glm::vec3 incomingDir = glm::normalize(ctx.cameraPos - mirrorPos);
            float     dist        = glm::dot(ctx.cameraPos - mirrorPos, mirrorNormal);
            // mirror normal is negative to camera dir, so subtracting moves camera to the other side of the mirror plane
            // glm::vec3 mirroredCameraPos = ctx.cameraPos - 2.0f * dist * mirrorNormal;
            glm::vec3 mirroredCameraPos = mirrorPos;
            glm::vec3 reflectedDir      = glm::reflect(incomingDir, mirrorNormal);
            ctxCopy.cameraPos           = mirroredCameraPos;
            ctxCopy.view                = glm::lookAt(mirroredCameraPos, mirroredCameraPos + reflectedDir, glm::vec3(0, 1, 0));
            ctxCopy.view                = glm::inverse(ctxCopy.view); // to another side of the mirror, so invert the view matrix to flip the handedness for correct culling

            break;
        }

        if (bHasMirror) {
            ctxCopy.extent = _mirrorRT->getExtent(); // Ensure material systems render with correct extent for mirror RT

            RenderingInfo ri{
                .label      = "ViewPort",
                .renderArea = Rect2D{
                    .pos    = {0, 0},
                    .extent = _mirrorRT->getExtent().toVec2(), // Use actual RT extent for rendering, which may differ from viewportRect if retro rendering is enabled
                },
                .layerCount       = 1,
                .colorClearValues = {colorClearValue},
                .depthClearValue  = depthClearValue,
                .renderTarget     = _mirrorRT.get(),
            };
            cmdBuf->beginRendering(ri);

            renderScene(cmdBuf.get(), dt, ctxCopy);

            cmdBuf->endRendering(EndRenderingInfo{
                .renderTarget = _mirrorRT.get(),
            });
        }
    }


    // MARK: ViewPort Pass
    if (bViewPortRectValid)
    {
        YA_PROFILE_SCOPE("ViewPort pass")

        // from the editor layer's viewport size
        // auto extent = _editorLayer.g

        auto extent = Extent2D::fromVec2(_viewportRect.extent / _viewportFrameBufferScale);


        _viewportRT->setExtent(extent);

        RenderingInfo ri{
            .label      = "ViewPort",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = _viewportRT->getExtent().toVec2(), // Use actual RT extent for rendering, which may differ from viewportRect if retro rendering is enabled
            },
            .layerCount       = 1,
            .colorClearValues = {colorClearValue},
            .depthClearValue  = depthClearValue,
            //
            .renderTarget = _viewportRT.get(),
        };

        cmdBuf->beginRendering(ri);

        ctx.extent = _viewportRT->getExtent(); // Update frame context with actual render extent for material systems

        renderScene(cmdBuf.get(), dt, ctx);

        {
            YA_PROFILE_SCOPE("Render2D");
            FRender2dContext render2dCtx{
                .cmdBuf       = cmdBuf.get(),
                .windowWidth  = ctx.extent.width,
                .windowHeight = ctx.extent.height,
                .cam          = {

                    .position       = ctx.cameraPos,
                    .view           = ctx.view,
                    .projection     = ctx.projection,
                    .viewProjection = ctx.projection * ctx.view,
                },
            };

            Render2D::begin(render2dCtx);

            if (_appMode == AppMode::Drawing) {
                for (const auto&& [idx, p] : ut::enumerate(clicked))
                {
                    auto tex = idx % 2 == 0
                                 ? AssetManager::get()->getTextureByName("uv1")
                                 : AssetManager::get()->getTextureByName("face");
                    YA_CORE_ASSERT(tex, "Texture not found");
                    glm::vec2 pos;
                    _editorLayer->screenToViewport(glm::vec2(p.x, p.y), pos);
                    Render2D::makeSprite(glm::vec3(pos, 0.0f), {50, 50}, tex);
                }
            }

            const glm::vec2 screenSize(30, 30);
            const float     viewPortHeight = ctx.extent.height;
            const float     scaleFactor    = screenSize.x / viewPortHeight;

            for (const auto& [entity, billboard, transfCompp] :
                 getSceneManager()->getActiveScene()->getRegistry().view<BillboardComponent, TransformComponent>().each())
            {
                auto texture = billboard.image.isValid() ? billboard.image.textureRef.getShared() : nullptr;

                const auto& pos = transfCompp.getWorldPosition();

                glm::vec3 billboardToCamera = ctx.cameraPos - pos;
                float     distance          = glm::length(billboardToCamera);
                billboardToCamera           = glm::normalize(billboardToCamera);

                if constexpr (1) {
                    // Build billboard rotation matrix with stable world-up constraint
                    // Forward: billboard faces camera
                    glm::vec3 forward = billboardToCamera;
                    glm::vec3 worldUp = glm::vec3(0, 1, 0);
                    glm::vec3 right   = glm::normalize(glm::cross(worldUp, forward));
                    glm::vec3 up      = glm::cross(forward, right);

                    glm::mat4 rot(1.0f);
                    rot[0] = glm::vec4(right, 0.0f);
                    rot[1] = glm::vec4(up, 0.0f);
                    rot[2] = glm::vec4(forward, 0.0f);

                    // scale
                    float     factor = scaleFactor * distance * 2.0f;
                    glm::vec3 scale  = glm::vec3(factor, factor, 1.0f);

                    glm::mat4 trans = glm::mat4(1.0);
                    trans           = glm::translate(trans, pos);
                    trans           = trans * rot;
                    trans           = glm::scale(trans, scale);

                    Render2D::makeWorldSprite(trans, texture);
                }
            }


            Render2D::onRender();
            UIManager::get()->render();
            Render2D::onRenderGUI();
            Render2D::end();
        }

        cmdBuf->endRendering(EndRenderingInfo{
            .renderTarget = _viewportRT.get(),
        });
    }


    // --- MARK: Postprocessing
    if (_basicPostprocessingSystem->bEnabled && bViewPortRectValid)
    {
        YA_PROFILE_SCOPE("Postprocessing pass")
        cmdBuf->debugBeginLabel("Postprocessing");
        // Transition postprocess image from Undefined/ShaderReadOnly to ColorAttachmentOptimal
        // Viewport RT is dynamic rendering
        cmdBuf->transitionImageLayoutAuto(_postprocessTexture->image.get(),
                                          //   EImageLayout::Undefined,
                                          EImageLayout::ColorAttachmentOptimal);

        // Build RenderingInfo from manual images
        RenderingInfo ri{
            .label      = "Postprocessing",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = _viewportRect.extent, // Use viewport size for postprocess render area
            },
            .layerCount       = 1,
            .colorClearValues = {colorClearValue},
            .depthClearValue  = depthClearValue,
            //
            .colorAttachments = {
                RenderingInfo::ImageSpec{
                    .texture     = _postprocessTexture.get(), // ← 使用 App 层的 Texture 接口
                    .sampleCount = ESampleCount::Sample_1,
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                },
            },
        };

        cmdBuf->beginRendering(ri);

        const auto& tex = bMSAA
                            ? _viewportRT->getCurFrameBuffer()->getResolveTexture()
                            : _viewportRT->getCurFrameBuffer()->getColorTexture(0);

        auto postprocessSystem = _basicPostprocessingSystem->as<BasicPostprocessing>();
        auto swapchainFormat   = _render->getSwapchain()->getFormat();
        bool bOutputIsSRGB     = (swapchainFormat == EFormat::R8G8B8A8_SRGB || swapchainFormat == EFormat::B8G8R8A8_SRGB);
        postprocessSystem->setOutputColorSpace(bOutputIsSRGB);
        postprocessSystem->setInputTexture(tex->getImageView(), Extent2D::fromVec2(_viewportRect.extent));
        postprocessSystem->tick(cmdBuf.get(), dt, &ctx);
        cmdBuf->endRendering(EndRenderingInfo{});

        // Transition postprocess image to ShaderReadOnlyOptimal for ImGui sampling
        cmdBuf->transitionImageLayoutAuto(_postprocessTexture->image.get(),
                                          //   EImageLayout::ColorAttachmentOptimal,
                                          //   EImageLayout::Undefined,
                                          EImageLayout::ShaderReadOnlyOptimal);

        cmdBuf->debugEndLabel();

        // Use postprocess texture directly (unified Texture semantics)
        _viewportTexture = _postprocessTexture.get();
    }
    else {
        // Create a Texture wrapper from framebuffer's color attachment for unified semantics
        auto fb = _viewportRT->getCurFrameBuffer();
        // _viewportTexture = fb->getColorTexture(0);
        _viewportTexture = bMSAA ? fb->getResolveTexture() : fb->getColorTexture(0);
    }
    YA_CORE_ASSERT(_viewportTexture, "Failed to get viewport texture for postprocessing");

    // Note: _postprocessTexture is now in ShaderReadOnlyOptimal, ready for EditorLayer viewport display

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