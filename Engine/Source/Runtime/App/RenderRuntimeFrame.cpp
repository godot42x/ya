#include "RenderRuntime.h"

#include "App.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
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

    if (input.pipeline.viewportRect.extent.x > 0 && input.pipeline.viewportRect.extent.y > 0) {
        onViewportResized(input.pipeline.viewportRect);
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
    YA_PROFILE_SCOPE("RenderRuntime::beginFrameCommandBuffer");
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
    auto* pipeline = getActivePipelineExecution();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while ticking viewport pass");

    pipeline->tick(RenderPipelineFrameContext{
        .flightIndex              = input.pipeline.flightIndex,
        .cmdBuf                   = cmdBuf,
        .deltaTime                = input.pipeline.deltaTime,
        .view                     = input.pipeline.view,
        .projection               = input.pipeline.projection,
        .cameraPos                = input.pipeline.cameraPos,
        .viewportRect             = _viewportRect,
        .viewportFrameBufferScale = _viewportFrameBufferScale,
        .frameData                = input.pipeline.frameData,
    });
}

bool RenderRuntime::hasOpenViewportPass() const
{
    if (auto* pipeline = getActivePipelineExecution()) {
        return pipeline->hasOpenViewportPass();
    }
    return false;
}

Extent2D RenderRuntime::getActiveViewportExtent() const
{
    if (auto* pipeline = getActivePipelineExecution()) {
        return pipeline->getViewportExtent();
    }
    return {};
}

Texture* RenderRuntime::getActiveViewportTexture() const
{
    if (auto* pipeline = getActivePipelineExecution()) {
        return pipeline->getViewportTexture();
    }
    return nullptr;
}

void RenderRuntime::renderViewportPassOverlays(const RenderPipelineFrameContext& pipelineFrame, const FrameInput::OverlayInput& overlay, ICommandBuffer* cmdBuf)
{
    if (!hasOpenViewportPass()) {
        return;
    }

    YA_PROFILE_SCOPE("Render2D");
    YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());

    const Extent2D viewportExtent = getActiveViewportExtent();
    FRender2dContext render2dCtx{
        .cmdBuf       = cmdBuf,
        .windowWidth  = viewportExtent.width,
        .windowHeight = viewportExtent.height,
        .cam          = {
            .position       = pipelineFrame.cameraPos,
            .view           = pipelineFrame.view,
            .projection     = pipelineFrame.projection,
            .viewProjection = pipelineFrame.projection * pipelineFrame.view,
        },
    };

    Render2D::begin(render2dCtx);

    if (overlay.appMode == AppMode::Drawing && overlay.editorLayer && overlay.clicked) {
        for (const auto&& [idx, p] : ut::enumerate(*overlay.clicked)) {
            auto tex = idx % 2 == 0
                         ? AssetManager::get()->getTextureByName("uv1")
                         : AssetManager::get()->getTextureByName("face");
            YA_CORE_ASSERT(tex, "Texture not found");
            glm::vec2 pos;
            overlay.editorLayer->screenToViewport(glm::vec2(p.x, p.y), pos);
            Render2D::makeSprite(glm::vec3(pos, 0.0f), {50, 50}, tex);
        }
    }

    if (auto* scene = overlay.scene) {
        const glm::vec2 screenSize(30, 30);
        const float     viewPortHeight = static_cast<float>(viewportExtent.height);
        const float     scaleFactor    = screenSize.x / viewPortHeight;

        for (const auto& [entity, billboard, transfCompp] :
             scene->getRegistry().view<BillboardComponent, TransformComponent>().each()) {
            auto        texture = billboard.image.hasPath() ? billboard.image.textureRef.getShared() : nullptr;
            const auto& pos     = transfCompp.getWorldPosition();

            glm::vec3 billboardToCamera = pipelineFrame.cameraPos - pos;
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

    auto* pipeline = getActivePipelineExecution();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while ending viewport pass");

    pipeline->endViewportPass(cmdBuf);

    if (_renderPipeline == ERenderPipeline::Forward) {
        YA_CORE_ASSERT(pipeline->getViewportTexture(), "Failed to get viewport texture for postprocessing");
    }
    else {
        YA_CORE_ASSERT(pipeline->getViewportTexture(), "Failed to get deferred viewport texture");
    }
}

void RenderRuntime::renderPresentationPass(float deltaTime, ICommandBuffer* cmdBuf)
{
    YA_PROFILE_SCOPE("Screen pass");
    YA_PERF_SCOPE(perf::sample::renderPresentation(), perf::metric::cpuTimeMs(), perf::domain::render());

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
        YA_PERF_SCOPE(perf::sample::renderImgui(), perf::metric::cpuTimeMs(), perf::domain::render());
        _app->renderGUI(deltaTime);
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
