#include "Editor/App/EditorAppExtension.h"

#include "Editor/App/EditorLayer.h"
#include "Editor/App/EditorPlaySession.h"

#include "Core/Camera/FreeCameraController.h"
#include "Editor/EditorProfilingSettings.h"
#include "Editor/Panels/RenderTargetInspector.h"
#include "Editor/Inspector/TypeRenderer.h"
#include "Config/ConfigManager.h"
#include "Core/Profiling/Profiling.h"
#include "Editor/ImGui/ImGuiHelper.h"
#include "Render/2D/Render2D.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Runtime/App/App.h"
#include "Runtime/App/DebugRenderSystem.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Runtime/App/DeferredRender/DeferredRenderPipeline.h"
#include "Runtime/App/Utility/FPSCtrl.h"

#include <algorithm>
#include <format>
#include <glm/gtc/type_ptr.hpp>
#include <string_view>

namespace ya
{

extern ClearValue colorClearValue;
extern ClearValue depthClearValue;

namespace
{

EditorLayer* gEditorLayer = nullptr;

DeferredRenderPipeline* getDeferredPipeline(App& app)
{
    auto* renderRuntime = app.getRenderRuntime();
    return renderRuntime ? dynamic_cast<DeferredRenderPipeline*>(renderRuntime->getActivePipeline()) : nullptr;
}

glm::vec3 resolveInitialEditorCameraPosition(const App& app)
{
    return app.getDesc().automation.editorCameraPosition.value_or(glm::vec3(0.0f, 0.0f, 5.0f));
}

glm::vec3 resolveInitialEditorCameraRotation(const App& app)
{
    return app.getDesc().automation.editorCameraRotation.value_or(glm::vec3(0.0f, 0.0f, 0.0f));
}

void initializeEditorCamera(App& app, EditorLayer& layer)
{
    auto& editorCamera = layer.getCamera();
    const auto& desc   = app.getDesc();
    const float aspect = desc.height > 0 ? static_cast<float>(desc.width) / static_cast<float>(desc.height) : (16.0f / 9.0f);
    editorCamera.setPerspective(45.0f, aspect, 0.1f, 100.0f);
    editorCamera.setPositionAndRotation(resolveInitialEditorCameraPosition(app), resolveInitialEditorCameraRotation(app));
}

void savePostProcessingSettings(const PostProcessingState& settings)
{
    ConfigManager::Editor("runtime")
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

template <typename T>
void migrateLegacyRuntimeSetting(std::string_view key)
{
    auto& config = ConfigManager::get();
    if (config.hasValue("runtime", key)) {
        return;
    }

    T value{};
    if (config.tryGet<T>("editor", key, value)) {
        ConfigManager::Editor("runtime").set(key, value);
    }
}

void migrateLegacyRuntimeSettings()
{
    migrateLegacyRuntimeSetting<bool>("render.deferred.reverseViewportY");
    migrateLegacyRuntimeSetting<bool>("render.deferred.ssaoEnabled");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.radius");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.bias");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.power");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.intensity");
    migrateLegacyRuntimeSetting<bool>("render.deferred.light.enablePBRDiffuseIBL");
    migrateLegacyRuntimeSetting<bool>("render.deferred.light.enablePBRSpecularIBL");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.inversion");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.grayscale");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.kernel");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.kernelTexelOffset");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.tonemapping.enabled");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.tonemapping.curve");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.tonemapping.exposure");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.output.gammaCorrection");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.output.gamma");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.output.randomGrain");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.output.randomGrainStrength");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.bloom.enabled");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.threshold");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.softKnee");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.extractIntensity");
    migrateLegacyRuntimeSetting<int>("render.postprocess.bloom.blurPasses");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.strength");
}

void saveShadowSettings(const ShadowSettings& settings)
{
    shadow_settings::saveRuntimeSettings(settings);
}

void renderDeferredSettingsContent(App& app)
{
    auto* runtime = app.getRenderRuntime();
    if (!runtime) {
        return;
    }

    const char* renderPipelineNames = "Forward\0Deferred\0";
    int currentRenderPipeline = static_cast<int>(runtime->getPendingRenderPipeline());
    if (ImGui::Combo("Render Pipeline", &currentRenderPipeline, renderPipelineNames)) {
        runtime->setPendingRenderPipeline(static_cast<RenderRuntime::ERenderPipeline>(currentRenderPipeline));
    }
    if (runtime->getPendingRenderPipeline() != runtime->getRenderPipeline()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "(switch pending)");
    }

    if (auto* render = app.getRender()) {
        if (auto* swapchain = render->getSwapchain()) {
            ImGui::SeparatorText("Presentation");
            bool bVsync = swapchain->getVsync();
            if (ImGui::Checkbox("VSync", &bVsync)) {
                swapchain->setVsync(bVsync);
            }

            EPresentMode::T presentMode = swapchain->getPresentMode();
            if (ImGui::Combo("Present Mode",
                             reinterpret_cast<int*>(&presentMode),
                             "Immediate\0Mailbox\0FIFO\0FIFO Relaxed\0")) {
                app.taskManager.registerFrameTask([swapchain, presentMode]() {
                    swapchain->setPresentMode(presentMode);
                });
            }
        }
    }

    auto* pipeline = getDeferredPipeline(app);
    if (!pipeline) {
        ImGui::TextDisabled("Deferred-only settings are unavailable while the forward pipeline is active.");
        return;
    }

    ImGui::SeparatorText("Deferred");
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
        ConfigManager::Editor("runtime")
            .set("render.deferred.reverseViewportY", settings.bReverseViewportY)
            .set("render.deferred.ssaoEnabled", settings.bSSAOEnabled)
            .set("render.deferred.shadowsEnabled", settings.shadow.isEnabled());
        savePostProcessingSettings(post);
        saveShadowSettings(settings.shadow);
    }
}

void renderEditorCameraContent(EditorLayer& layer, FreeCameraController& controller)
{
    auto& camera    = layer.getCamera();
    auto  position  = camera._position;
    auto  rotation  = camera._rotation;
    bool  bChanged  = false;

    bChanged |= ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f);
    bChanged |= ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.0f, -180.0f, 180.0f);
    ImGui::DragFloat("Move Speed", &controller._moveSpeed, 0.1f, 0.1f, 20.0f);
    ImGui::DragFloat("Rotation Speed", &controller._rotationSpeed, 1.0f, 10.0f, 180.0f);
    ImGui::TextDisabled("Hold right mouse button to rotate camera");
    ImGui::TextDisabled("WASD: Move horizontally, QE: Move vertically");

    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
}

void renderClearValuesContent()
{
    float color[4] = {colorClearValue.color.r, colorClearValue.color.g, colorClearValue.color.b, colorClearValue.color.a};
    if (ImGui::ColorEdit4("Color Clear Value", color)) {
        colorClearValue = ClearValue(color[0], color[1], color[2], color[3]);
    }

    float depth = depthClearValue.depthStencil.depth;
    if (ImGui::DragFloat("Depth Clear Value", &depth, 0.01f, 0.0f, 1.0f)) {
        depthClearValue = ClearValue(depth, depthClearValue.depthStencil.stencil);
    }
}

void renderRender2DDebugContent()
{
    auto& data = Render2D::data;
    ImGui::Checkbox("Reverse Viewport Y", &data.bReverseViewport);

    int worldCullMode = static_cast<int>(data.worldCullMode);
    if (ImGui::Combo("World Cull Mode", &worldCullMode, "None\0Front\0Back\0FrontAndBack\0")) {
        data.worldCullMode = static_cast<ECullMode::T>(worldCullMode);
    }

    int screenCullMode = static_cast<int>(data.screenCullMode);
    if (ImGui::Combo("Screen Cull Mode", &screenCullMode, "None\0Front\0Back\0FrontAndBack\0")) {
        data.screenCullMode = static_cast<ECullMode::T>(screenCullMode);
    }

    ImGui::InputInt("Text Layout Mode", &data.TextLayoutMode);
    ImGui::TextDisabled("Viewport: %u x %u", data.windowWidth, data.windowHeight);
}

void renderSessionContent(App& app)
{
    AppMode mode = app._appMode;
    if (ImGui::Combo("App Mode", reinterpret_cast<int*>(&mode), "Control\0Drawing\0")) {
        app._appMode = mode;
    }

    if (auto* fpsCtrl = FPSControl::get()) {
        ImGui::SeparatorText("FPS Control");
        ImGui::Text("FPS Limit: %.1f", fpsCtrl->fpsLimit);

        static float newFpsLimit = fpsCtrl->fpsLimit;
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("New Limit", &newFpsLimit, 10.0f, 10.0f, "%.1f");
        ImGui::SameLine();
        if (ImGui::Button("Apply FPS Limit")) {
            fpsCtrl->setFPSLimit(newFpsLimit);
        }
        ImGui::Checkbox("Enable FPS Control", &fpsCtrl->bEnable);
    }

    std::string clickedPoints;
    for (const auto& point : app.clicked) {
        clickedPoints += std::format("({}, {}) ", static_cast<int>(point.x), static_cast<int>(point.y));
    }
    if (clickedPoints.empty()) {
        clickedPoints = "<none>";
    }
    ImGui::SeparatorText("Input Trace");
    ImGui::TextWrapped("Clicked Points: %s", clickedPoints.c_str());
}

void renderDiagnosticsContent(App& app)
{
    auto* runtime = app.getRenderRuntime();
    if (!runtime) {
        return;
    }

    const auto& renderDoc = runtime->getDiagnosticsService().getRenderDocState();
    const bool bAvailable = renderDoc.capture && renderDoc.capture->isAvailable();
    ImGui::Text("RenderDoc: %s", bAvailable ? "Available" : "Unavailable");
    ImGui::TextWrapped("Last Capture: %s", renderDoc.lastCapturePath.empty() ? "<none>" : renderDoc.lastCapturePath.c_str());
}

void renderDebugPrimitivesContent(App& app)
{
    auto* runtime = app.getRenderRuntime();
    if (!runtime) {
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
}

void renderRuntimeToolsWindow(App& app, EditorLayer& layer, FreeCameraController& controller)
{
    if (!ImGui::Begin("Runtime Tools")) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Session", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderSessionContent(app);
    }
    if (ImGui::CollapsingHeader("Render Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderDeferredSettingsContent(app);
    }
    if (ImGui::CollapsingHeader("Editor Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderEditorCameraContent(layer, controller);
    }
    if (ImGui::CollapsingHeader("Clear Values")) {
        renderClearValuesContent();
    }
    if (ImGui::CollapsingHeader("Render2D Debug")) {
        renderRender2DDebugContent();
    }
    if (ImGui::CollapsingHeader("Diagnostics")) {
        renderDiagnosticsContent(app);
    }
    if (ImGui::CollapsingHeader("Debug Primitives")) {
        renderDebugPrimitivesContent(app);
    }
    if (ImGui::CollapsingHeader("Render Targets")) {
        renderRenderTargetInspectorContent(app);
    }

    ImGui::End();
}

class EditorAppExtension final : public IAppExtension
{
  public:
    void onConfigure(App& app, AppDesc& desc) override
    {
        ConfigManager::get().openDocument("editor", "Engine/Saved/Config/Editor.json");
        migrateLegacyRuntimeSettings();
        if (!shadow_settings::hasRuntimeSettings()) {
            shadow_settings::saveRuntimeSettings(
                shadow_settings::loadSettingsFromDocument("editor", app.getShadowSettings()));
        }
        editor_profiling_settings::load();
        if (!desc.defaultScenePath) {
            const std::string path = ConfigManager::get().getOr<std::string>("editor", "startup.defaultScenePath", "");
            if (!path.empty()) {
                desc.defaultScenePath = path;
            }
        }
    }

    void onAttach(App& app) override
    {
        auto* renderRuntime = app.getRenderRuntime();
        YA_CORE_ASSERT(renderRuntime, "Editor extension requires an initialized RenderRuntime");

        ImGuiManager::get().init(renderRuntime->getRender(), nullptr);
        registerBuiltinTypeRenderers();

        _layer = std::make_unique<EditorLayer>(&app);
        initializeEditorCamera(app, *_layer);
        _layer->onAttach();
        gEditorLayer = _layer.get();
    }

    void onDetach(App& app) override
    {
        _playSession.shutdown(app);
        app.clearExtensionRenderFrameState();
        if (_layer) {
            _layer->onDetach();
            _layer.reset();
        }
        gEditorLayer = nullptr;
        ImGuiManager::get().shutdown();
    }

    bool onBeforeAppStateChange(App& app, AppState previousState, AppState nextState) override
    {
        if (previousState == AppState::Stopped && nextState != AppState::Stopped) {
            return _playSession.begin(app, nextState);
        }
        if (previousState != AppState::Stopped && nextState == AppState::Stopped) {
            _playSession.end(app);
        }
        return true;
    }

    void onSceneActivated(App& app, Scene* scene) override
    {
        _playSession.onSceneActivated(app, scene);
        if (!_layer) {
            return;
        }

        const uint64_t selectedUUID = _layer->getSelectedEntityUUID();
        _layer->setEditableScene(_playSession.getAuthoringScene());
        _layer->setSceneContext(scene);
        _layer->selectEntity(scene && selectedUUID != 0 ? scene->getEntityByUUID(selectedUUID) : nullptr);
    }

    void onSceneDestroyed(App& app, Scene* scene) override
    {
        _playSession.onSceneDestroyed(scene);
        if (_layer) {
            _layer->setEditableScene(_playSession.getAuthoringScene());
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

        if (auto* renderRuntime = app.getRenderRuntime()) {
            auto& editorCamera = _layer->getCamera();
            const Extent2D viewportExtent = renderRuntime->getViewportExtent();
            if (_layer->shouldCaptureInput()) {
                _cameraController.update(editorCamera, app.inputManager, dt);
            }
            if (viewportExtent.height > 0) {
                editorCamera.setPerspective(editorCamera._fov,
                                            static_cast<float>(viewportExtent.width) / static_cast<float>(viewportExtent.height),
                                            editorCamera._nearClip,
                                            editorCamera._farClip);
            }
            app.setExtensionRenderFrameState({
                .view       = editorCamera.getViewMatrix(),
                .projection = editorCamera.getProjectionMatrix(),
                .cameraPos  = editorCamera.getPosition(),
            });
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
        (void)dt;
        if (!_layer) {
            return;
        }

        if (auto* renderRuntime = app.getRenderRuntime()) {
            _layer->setViewportContext(renderRuntime->buildViewportSnapshot());
        }

        ImGuiManager::get().beginFrame();
        _layer->onImGuiRender([this, &app]() {
            renderRuntimeToolsWindow(app, *_layer, _cameraController);
        });
        ImGuiManager::get().endFrame();
        ImGuiManager::get().render();

        if (auto* render = app.getRender(); render && render->getAPI() == ERenderAPI::Vulkan) {
            ImGuiManager::get().submitVulkan(commandBuffer.getHandleAs<VkCommandBuffer>());
        }
    }

  private:
    std::unique_ptr<EditorLayer> _layer;
    EditorPlaySession             _playSession;
    FreeCameraController          _cameraController;
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
