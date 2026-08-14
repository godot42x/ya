#include "GameEditor/Panels/RuntimeToolsPanelInternal.h"

namespace ya
{

namespace
{

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

} // namespace

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

void RuntimeToolsPanel::onImGuiRender(App& app, float dt)
{
    (void)dt;
    if (!_owner || !_controller) {
        return;
    }

    if (!ImGui::Begin("Runtime Tools")) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Session", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderSessionContent(app);
    }
    if (ImGui::CollapsingHeader("Profiling")) {
        renderProfilingContent(app);
    }
    if (ImGui::CollapsingHeader("Render Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto* runtime = app.getRenderServices().getRenderRuntime()) {
            ImGui::Indent();
            renderPresentationSettings(app, *runtime);
            ImGui::Unindent();
            ImGui::Separator();
            if (runtime->isDeferredPipelineActive()) {
                renderDeferredSettingsContent(app);
            }
            else {
                renderForwardSettingsContent(app);
            }
        }
    }
    if (ImGui::CollapsingHeader("Rendering Internals", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderRenderingInternalsContent(app);
    }
    if (ImGui::CollapsingHeader("Editor Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderEditorCameraContent(*_owner, *_controller);
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

} // namespace ya
