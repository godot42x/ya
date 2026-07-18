#include "Editor/EditorAppExtension.h"

#include "Editor/EditorLayer.h"
#include "Editor/RenderTargetInspector.h"
#include "Editor/TypeRenderer.h"
#include "Config/ConfigManager.h"
#include "ImGuiHelper.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Runtime/App/App.h"
#include "Runtime/App/DebugRenderSystem.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/DeferredRender/DeferredRenderPipeline.h"

#include <algorithm>

namespace ya
{

namespace
{

EditorLayer* gEditorLayer = nullptr;

DeferredRenderPipeline* getDeferredPipeline(App& app)
{
    auto* renderRuntime = app.getRenderRuntime();
    return renderRuntime ? dynamic_cast<DeferredRenderPipeline*>(renderRuntime->getActivePipeline()) : nullptr;
}

void applyDeferredSettingsFromConfig(App& app)
{
    auto* pipeline = getDeferredPipeline(app);
    if (!pipeline) {
        return;
    }

    auto settings = pipeline->buildSettingsSnapshot();
    settings.bReverseViewportY = ConfigManager::get().getOr<bool>("editor", "render.deferred.reverseViewportY", settings.bReverseViewportY);
    settings.bSSAOEnabled      = ConfigManager::get().getOr<bool>("editor", "render.deferred.ssaoEnabled", settings.bSSAOEnabled);
    const bool bShadowsEnabled = ConfigManager::get().getOr<bool>("editor", "render.deferred.shadowsEnabled", settings.shadow.isEnabled());
    if (!bShadowsEnabled) {
        settings.shadow.quality = EShadowQuality::Off;
    }
    auto& post = settings.postProcessing;
    post.bEnableInversion       = ConfigManager::get().getOr<bool>("editor", "render.postprocess.basic.inversion", post.bEnableInversion);
    post.grayscaleMode          = static_cast<PostProcessingState::EGrayscaleMode>(ConfigManager::get().getOr<int>("editor", "render.postprocess.basic.grayscale", static_cast<int>(post.grayscaleMode)));
    post.kernelMode             = static_cast<PostProcessingState::EKernelMode>(ConfigManager::get().getOr<int>("editor", "render.postprocess.basic.kernel", static_cast<int>(post.kernelMode)));
    post.kernelTexelOffset      = ConfigManager::get().getOr<float>("editor", "render.postprocess.basic.kernelTexelOffset", post.kernelTexelOffset);
    post.bEnableToneMapping     = ConfigManager::get().getOr<bool>("editor", "render.postprocess.basic.tonemapping.enabled", post.bEnableToneMapping);
    post.toneMappingCurve       = static_cast<PostProcessingState::EToneMappingCurve>(ConfigManager::get().getOr<int>("editor", "render.postprocess.basic.tonemapping.curve", static_cast<int>(post.toneMappingCurve)));
    post.exposure               = ConfigManager::get().getOr<float>("editor", "render.postprocess.basic.tonemapping.exposure", post.exposure);
    post.bEnableGammaCorrection = ConfigManager::get().getOr<bool>("editor", "render.postprocess.basic.output.gammaCorrection", post.bEnableGammaCorrection);
    post.gamma                  = ConfigManager::get().getOr<float>("editor", "render.postprocess.basic.output.gamma", post.gamma);
    post.bEnableRandomGrain     = ConfigManager::get().getOr<bool>("editor", "render.postprocess.basic.output.randomGrain", post.bEnableRandomGrain);
    post.randomGrainStrength    = ConfigManager::get().getOr<float>("editor", "render.postprocess.basic.output.randomGrainStrength", post.randomGrainStrength);
    post.bEnableBloom           = ConfigManager::get().getOr<bool>("editor", "render.postprocess.bloom.enabled", post.bEnableBloom);
    post.bloomThreshold         = ConfigManager::get().getOr<float>("editor", "render.postprocess.bloom.threshold", post.bloomThreshold);
    post.bloomSoftKnee          = ConfigManager::get().getOr<float>("editor", "render.postprocess.bloom.softKnee", post.bloomSoftKnee);
    post.bloomExtractIntensity  = ConfigManager::get().getOr<float>("editor", "render.postprocess.bloom.extractIntensity", post.bloomExtractIntensity);
    post.bloomBlurPasses        = static_cast<uint32_t>(ConfigManager::get().getOr<int>("editor", "render.postprocess.bloom.blurPasses", static_cast<int>(post.bloomBlurPasses)));
    post.bloomStrength          = ConfigManager::get().getOr<float>("editor", "render.postprocess.bloom.strength", post.bloomStrength);
    pipeline->requestSettings(settings);
}

void savePostProcessingSettings(const PostProcessingState& settings)
{
    ConfigManager::Editor("editor")
        .set("render.postprocess.basic.inversion", settings.bEnableInversion)
        .set("render.postprocess.basic.grayscale", static_cast<int>(settings.grayscaleMode))
        .set("render.postprocess.basic.kernel", static_cast<int>(settings.kernelMode))
        .set("render.postprocess.basic.kernelTexelOffset", settings.kernelTexelOffset)
        .set("render.postprocess.basic.tonemapping.enabled", settings.bEnableToneMapping)
        .set("render.postprocess.basic.tonemapping.curve", static_cast<int>(settings.toneMappingCurve))
        .set("render.postprocess.basic.tonemapping.exposure", settings.exposure)
        .set("render.postprocess.basic.output.gammaCorrection", settings.bEnableGammaCorrection)
        .set("render.postprocess.basic.output.gamma", settings.gamma)
        .set("render.postprocess.basic.output.randomGrain", settings.bEnableRandomGrain)
        .set("render.postprocess.basic.output.randomGrainStrength", settings.randomGrainStrength)
        .set("render.postprocess.bloom.enabled", settings.bEnableBloom)
        .set("render.postprocess.bloom.threshold", settings.bloomThreshold)
        .set("render.postprocess.bloom.softKnee", settings.bloomSoftKnee)
        .set("render.postprocess.bloom.extractIntensity", settings.bloomExtractIntensity)
        .set("render.postprocess.bloom.blurPasses", static_cast<int>(settings.bloomBlurPasses))
        .set("render.postprocess.bloom.strength", settings.bloomStrength);
}

void renderDeferredSettings(App& app)
{
    auto* pipeline = getDeferredPipeline(app);
    if (!pipeline || !ImGui::Begin("Render Settings")) {
        if (pipeline) ImGui::End();
        return;
    }

    auto settings = pipeline->buildSettingsSnapshot();
    bool changed  = false;
    changed |= ImGui::Checkbox("Reverse Viewport Y", &settings.bReverseViewportY);
    changed |= ImGui::Checkbox("Enable SSAO", &settings.bSSAOEnabled);
    bool bShadowsEnabled = settings.shadow.isEnabled();
    if (ImGui::Checkbox("Enable Shadows", &bShadowsEnabled)) {
        settings.shadow.quality = bShadowsEnabled ? EShadowQuality::Medium : EShadowQuality::Off;
        changed = true;
    }
    auto& post = settings.postProcessing;
    ImGui::SeparatorText("Post Processing");
    changed |= ImGui::Checkbox("Inversion", &post.bEnableInversion);
    int grayscaleMode = static_cast<int>(post.grayscaleMode);
    if (ImGui::Combo("Grayscale", &grayscaleMode, "None\0Average\0Weighted\0")) {
        post.grayscaleMode = static_cast<PostProcessingState::EGrayscaleMode>(grayscaleMode);
        changed = true;
    }
    int kernelMode = static_cast<int>(post.kernelMode);
    if (ImGui::Combo("Kernel", &kernelMode, "None\0Sharpen\0Blur\0Edge Detection\0")) {
        post.kernelMode = static_cast<PostProcessingState::EKernelMode>(kernelMode);
        changed = true;
    }
    ImGui::BeginDisabled(post.kernelMode == PostProcessingState::EKernelMode::None);
    changed |= ImGui::DragFloat("Kernel Texel Offset", &post.kernelTexelOffset, 0.0001f, 0.0001f, 0.02f, "%.5f");
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox("Enable Tone Mapping", &post.bEnableToneMapping);
    ImGui::BeginDisabled(!post.bEnableToneMapping);
    int toneMappingCurve = static_cast<int>(post.toneMappingCurve);
    if (ImGui::Combo("Tone Mapping Curve", &toneMappingCurve, "ACES\0Uncharted2\0")) {
        post.toneMappingCurve = static_cast<PostProcessingState::EToneMappingCurve>(toneMappingCurve);
        changed = true;
    }
    changed |= ImGui::DragFloat("Exposure", &post.exposure, 0.01f, 0.0f, 8.0f, "%.2f");
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox("Gamma Correction", &post.bEnableGammaCorrection);
    ImGui::BeginDisabled(!post.bEnableGammaCorrection);
    changed |= ImGui::DragFloat("Gamma", &post.gamma, 0.01f, 0.1f, 4.0f);
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox("Random Grain", &post.bEnableRandomGrain);
    ImGui::BeginDisabled(!post.bEnableRandomGrain);
    changed |= ImGui::DragFloat("Grain Strength", &post.randomGrainStrength, 0.001f, 0.0f, 0.25f, "%.3f");
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox("Enable Bloom", &post.bEnableBloom);
    ImGui::BeginDisabled(!post.bEnableBloom);
    changed |= ImGui::DragFloat("Bloom Threshold", &post.bloomThreshold, 0.01f, 0.0f, 16.0f, "%.2f");
    changed |= ImGui::DragFloat("Bloom Soft Knee", &post.bloomSoftKnee, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Bloom Extract Intensity", &post.bloomExtractIntensity, 0.05f, 0.0f, 8.0f, "%.2f");
    int bloomBlurPasses = static_cast<int>(post.bloomBlurPasses);
    if (ImGui::DragInt("Bloom Blur Passes", &bloomBlurPasses, 1.0f, 1, 12)) {
        post.bloomBlurPasses = static_cast<uint32_t>(std::max(1, bloomBlurPasses));
        changed = true;
    }
    changed |= ImGui::DragFloat("Bloom Strength", &post.bloomStrength, 0.05f, 0.0f, 4.0f, "%.2f");
    ImGui::EndDisabled();
    if (changed) {
        pipeline->requestSettings(settings);
        ConfigManager::Editor("editor")
            .set("render.deferred.reverseViewportY", settings.bReverseViewportY)
            .set("render.deferred.ssaoEnabled", settings.bSSAOEnabled)
            .set("render.deferred.shadowsEnabled", settings.shadow.isEnabled());
        savePostProcessingSettings(post);
    }
    ImGui::End();
}

void renderDiagnostics(App& app)
{
    auto* runtime = app.getRenderRuntime();
    if (!runtime || !ImGui::Begin("Render Diagnostics")) {
        if (runtime) ImGui::End();
        return;
    }

    const auto& renderDoc = runtime->getDiagnosticsService().getRenderDocState();
    const bool bAvailable = renderDoc.capture && renderDoc.capture->isAvailable();
    ImGui::Text("RenderDoc: %s", bAvailable ? "Available" : "Unavailable");
    ImGui::TextWrapped("Last Capture: %s", renderDoc.lastCapturePath.empty() ? "<none>" : renderDoc.lastCapturePath.c_str());
    ImGui::End();
}

void renderDebugPrimitives(App& app)
{
    auto* runtime = app.getRenderRuntime();
    if (!runtime || !ImGui::Begin("Debug Primitives")) {
        if (runtime) ImGui::End();
        return;
    }

    auto settings = runtime->getDebugRenderSystem().buildSettingsSnapshot();
    bool changed  = false;
    changed |= ImGui::Checkbox("Enabled", &settings.bEnabled);
    changed |= ImGui::Checkbox("Depth Test", &settings.bDepthTest);
    changed |= ImGui::Checkbox("Draw Lines", &settings.bDrawLines);
    changed |= ImGui::Checkbox("Draw Shapes", &settings.bDrawShapes);
    ImGui::Separator();
    ImGui::Text("Pending: %zu lines, %zu shapes", settings.pendingLineCount, settings.pendingShapeCount);
    ImGui::Text("Frame: %zu lines, %zu shapes", settings.frameLineCount, settings.frameShapeCount);
    ImGui::Text("Immediate: %zu lines, %zu shapes", settings.immediateLineCount, settings.immediateShapeCount);
    if (changed) {
        runtime->getDebugRenderSystem().requestSettings(settings);
    }
    ImGui::End();
}

class EditorAppExtension final : public IAppExtension
{
  public:
    void onAttach(App& app) override
    {
        auto* renderRuntime = app.getRenderRuntime();
        YA_CORE_ASSERT(renderRuntime, "Editor extension requires an initialized RenderRuntime");

        ImGuiManager::get().init(renderRuntime->getRender(), nullptr);
        registerBuiltinTypeRenderers();

        _layer = std::make_unique<EditorLayer>(&app);
        _layer->onAttach();
        applyDeferredSettingsFromConfig(app);
        gEditorLayer = _layer.get();
    }

    void onDetach(App& app) override
    {
        (void)app;
        if (_layer) {
            _layer->onDetach();
            _layer.reset();
        }
        gEditorLayer = nullptr;
        ImGuiManager::get().shutdown();
    }

    void onSceneActivated(App& app, Scene* scene) override
    {
        (void)app;
        if (!_layer) {
            return;
        }

        const uint64_t selectedUUID = _layer->getSelectedEntityUUID();
        _layer->setSceneContext(scene);
        _layer->selectEntity(scene && selectedUUID != 0 ? scene->getEntityByUUID(selectedUUID) : nullptr);
    }

    void onSceneDestroyed(App& app, Scene* scene) override
    {
        (void)app;
        (void)scene;
        if (_layer) {
            _layer->selectEntity(nullptr);
        }
    }

    void onNativeEvent(App& app, const SDL_Event& event) override
    {
        (void)app;
        ImGuiManager::get().processEvents(const_cast<SDL_Event&>(event));
    }

    bool onEvent(App& app, const Event& event) override
    {
        (void)app;
        if (ImGuiManager::get().processEvent(event) != EventProcessState::Continue) {
            return true;
        }
        if (_layer) {
            _layer->onEvent(event);
        }
        return false;
    }

    void onLogic(App& app, float dt) override
    {
        if (!_layer) {
            return;
        }

        _layer->onUpdate(dt);
        Rect2D pendingRect;
        if (_layer->getPendingViewportResize(pendingRect)) {
            if (auto* renderRuntime = app.getRenderRuntime()) {
                renderRuntime->onViewportResized(pendingRect);
            }
        }
    }

    void onPresentation(App& app, ICommandBuffer& commandBuffer, float dt) override
    {
        if (!_layer) {
            return;
        }

        if (auto* renderRuntime = app.getRenderRuntime()) {
            _layer->setViewportContext(renderRuntime->buildViewportSnapshot());
        }

        ImGuiManager::get().beginFrame();
        _layer->onImGuiRender([&app, dt]() {
            app.onRenderGUI(dt);
            renderDeferredSettings(app);
            renderDiagnostics(app);
            renderDebugPrimitives(app);
            renderRenderTargetInspector(app);
        });
        ImGuiManager::get().endFrame();
        ImGuiManager::get().render();

        if (auto* render = app.getRender(); render && render->getAPI() == ERenderAPI::Vulkan) {
            ImGuiManager::get().submitVulkan(commandBuffer.getHandleAs<VkCommandBuffer>());
        }
    }

  private:
    std::unique_ptr<EditorLayer> _layer;
};

} // namespace

std::unique_ptr<IAppExtension> createEditorAppExtension()
{
    return std::make_unique<EditorAppExtension>();
}

EditorLayer* getEditorLayer()
{
    return gEditorLayer;
}

} // namespace ya
