#include "RenderRuntime.h"

#include "App.h"
#include "Core/Debug/PerfKeys.h"
#include "Core/Debug/PerfState.h"
#include "Core/UI/UIManager.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Editor/EditorLayer.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ImGuiHelper.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Resource/AssetManager.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "utility.cc/ranges.h"

#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

void RenderRuntime::ensureViewportRectInitialized(const FrameInput& input)
{
    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        return;
    }

    if (input.viewportRect.extent.x > 0 && input.viewportRect.extent.y > 0) {
        onViewportResized(input.viewportRect);
        return;
    }

    auto swapchainExtent = _render->getSwapchain()->getExtent();
    onViewportResized(Rect2D{
        .pos    = {0.0f, 0.0f},
        .extent = {static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height)},
    });
}

bool RenderRuntime::beginFrameCommandBuffer(int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    YA_PERF_SCOPE(perf::sample::renderPrepareFrame(), perf::metric::cpuTimeMs(), perf::domain::render());

    if (_render->getSwapchain()->getExtent().width <= 0 || _render->getSwapchain()->getExtent().height <= 0) {
        return false;
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWaitIdle(), perf::metric::cpuTimeMs(), perf::domain::render());
        _render->waitIdle();
    }

    imageIndex = -1;
    {
        YA_PERF_SCOPE(perf::sample::renderBegin(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (!_render->begin(&imageIndex)) {
            return false;
        }
    }
    if (imageIndex < 0) {
        YA_CORE_WARN("Invalid image index ({}), skipping frame render", imageIndex);
        return false;
    }

    cmdBuf = _commandBuffers[imageIndex];
    cmdBuf->reset();
    cmdBuf->begin();
    if (YA_PERF_IS_ENABLED()) {
        _render->beginFrameGpuTiming(cmdBuf.get());
    }

    return true;
}

void RenderRuntime::beginViewportPassAndTickPipeline(const FrameInput& input, ICommandBuffer* cmdBuf)
{
    if (_shadingModel == EShadingModel::Forward) {
        _forwardPipeline->tick(ForwardRenderPipeline::TickDesc{
            .flightIndex              = input.flightIndex,
            .cmdBuf                   = cmdBuf,
            .dt                       = input.dt,
            .view                     = input.view,
            .projection               = input.projection,
            .cameraPos                = input.cameraPos,
            .viewportRect             = _viewportRect,
            .viewportFrameBufferScale = _viewportFrameBufferScale,
            .appMode                  = static_cast<int>(input.appMode),
            .clicked                  = input.clicked,
            .frameData                = input.frameData,
        });
        return;
    }

    _deferredPipeline->tick(DeferredRenderPipeline::TickDesc{
        .flightIndex              = input.flightIndex,
        .cmdBuf                   = cmdBuf,
        .sceneManager             = input.sceneManager,
        .dt                       = input.dt,
        .view                     = input.view,
        .projection               = input.projection,
        .cameraPos                = input.cameraPos,
        .viewportRect             = _viewportRect,
        .viewportFrameBufferScale = _viewportFrameBufferScale,
        .appMode                  = static_cast<int>(input.appMode),
        .clicked                  = input.clicked,
        .frameData                = input.frameData,
    });
}

bool RenderRuntime::hasOpenViewportPass() const
{
    if (_shadingModel == EShadingModel::Forward) {
        return _forwardPipeline && _forwardPipeline->hasOpenViewportPass();
    }
    return _deferredPipeline && _deferredPipeline->hasOpenViewportPass();
}

Extent2D RenderRuntime::getActiveViewportExtent() const
{
    if (_shadingModel == EShadingModel::Forward) {
        return _forwardPipeline ? _forwardPipeline->getViewportExtent() : Extent2D{};
    }
    return _deferredPipeline ? _deferredPipeline->getViewportExtent() : Extent2D{};
}

Texture* RenderRuntime::getActiveViewportTexture() const
{
    if (_shadingModel == EShadingModel::Forward) {
        return _forwardPipeline ? _forwardPipeline->viewportTexture : nullptr;
    }
    return _deferredPipeline ? _deferredPipeline->viewportTexture : nullptr;
}

void RenderRuntime::renderViewportPassOverlays(const FrameInput& input, ICommandBuffer* cmdBuf)
{
    if (!hasOpenViewportPass()) {
        return;
    }

    YA_PROFILE_SCOPE("Render2D")
    YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());

    const Extent2D viewportExtent = getActiveViewportExtent();
    FRender2dContext render2dCtx{
        .cmdBuf       = cmdBuf,
        .windowWidth  = viewportExtent.width,
        .windowHeight = viewportExtent.height,
        .cam          = {
            .position       = input.cameraPos,
            .view           = input.view,
            .projection     = input.projection,
            .viewProjection = input.projection * input.view,
        },
    };

    Render2D::begin(render2dCtx);

    if (input.appMode == AppMode::Drawing && input.editorLayer && input.clicked) {
        for (const auto&& [idx, p] : ut::enumerate(*input.clicked)) {
            auto tex = idx % 2 == 0
                         ? AssetManager::get()->getTextureByName("uv1")
                         : AssetManager::get()->getTextureByName("face");
            YA_CORE_ASSERT(tex, "Texture not found");
            glm::vec2 pos;
            input.editorLayer->screenToViewport(glm::vec2(p.x, p.y), pos);
            Render2D::makeSprite(glm::vec3(pos, 0.0f), {50, 50}, tex);
        }
    }

    auto scene = input.sceneManager ? input.sceneManager->getActiveScene() : nullptr;
    if (scene) {
        const glm::vec2 screenSize(30, 30);
        const float     viewPortHeight = static_cast<float>(viewportExtent.height);
        const float     scaleFactor    = screenSize.x / viewPortHeight;

        for (const auto& [entity, billboard, transfCompp] :
             scene->getRegistry().view<BillboardComponent, TransformComponent>().each()) {
            auto        texture = billboard.image.hasPath() ? billboard.image.textureRef.getShared() : nullptr;
            const auto& pos     = transfCompp.getWorldPosition();

            glm::vec3 billboardToCamera = input.cameraPos - pos;
            float     distance          = glm::length(billboardToCamera);
            billboardToCamera           = glm::normalize(billboardToCamera);

            glm::vec3 forward = billboardToCamera;
            glm::vec3 worldUp = glm::vec3(0, 1, 0);
            glm::vec3 right   = glm::normalize(glm::cross(worldUp, forward));
            glm::vec3 up      = glm::cross(forward, right);

            glm::mat4 rot(1.0f);
            rot[0] = glm::vec4(right, 0.0f);
            rot[1] = glm::vec4(up, 0.0f);
            rot[2] = glm::vec4(forward, 0.0f);

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

void RenderRuntime::endViewportPass(ICommandBuffer* cmdBuf)
{
    if (!hasOpenViewportPass()) {
        return;
    }

    if (_shadingModel == EShadingModel::Forward) {
        _forwardPipeline->endViewportPass(cmdBuf);
        YA_CORE_ASSERT(_forwardPipeline->viewportTexture, "Failed to get viewport texture for postprocessing");
        return;
    }

    _deferredPipeline->endViewportPass(cmdBuf);
    YA_CORE_ASSERT(_deferredPipeline->viewportTexture, "Failed to get deferred viewport texture");
}

void RenderRuntime::renderPresentationPass(const FrameInput& input, ICommandBuffer* cmdBuf)
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
        .renderTarget     = _screenRT.get(),
    };

    cmdBuf->beginRendering(ri);

    auto& imManager = ImGuiManager::get();
    imManager.beginFrame();
    if (_app) {
        _app->renderGUI(input.dt);
    }
    imManager.endFrame();
    imManager.render();

    if (_render->getAPI() == ERenderAPI::Vulkan) {
        imManager.submitVulkan(cmdBuf->getHandleAs<VkCommandBuffer>());
    }

    cmdBuf->endRendering(ri);
    if (_app) {
        AppAutomation::recordPresentationCapture(*_app, cmdBuf);
    }
}

void RenderRuntime::submitFrame(int32_t imageIndex, ICommandBuffer* cmdBuf)
{
    if (YA_PERF_IS_ENABLED()) {
        _render->endFrameGpuTiming(cmdBuf);
    }
    cmdBuf->end();
    _render->end(imageIndex, {cmdBuf->getHandle()});

    if (YA_PERF_IS_ENABLED()) {
        PerfState::Get().setValue(
            perf::sample::renderFrame(),
            perf::metric::gpuTimeMs(),
            _render->getLastCompletedFrameGpuTimeMs(),
            perf::domain::gpu());
    }
}

} // namespace ya
