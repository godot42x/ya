#include "Editor/Panels/RuntimeToolsPanelInternal.h"

namespace ya
{

void renderPerfLeaf(const char* label, float valueMs, float parentMs)
{
    if (parentMs > 0.0f) {
        ImGui::Text("%s: %.3f ms (%.1f%%)", label, valueMs, valueMs / parentMs * 100.0f);
        return;
    }
    ImGui::Text("%s: %.3f ms", label, valueMs);
}

void renderFrameStatsContent(const App& app, float dt)
{
    static constexpr size_t kHistorySize = 120;
    static std::array<float, kHistorySize> fpsHistory{};
    static size_t historyHead = 0;
    static size_t historyFill = 0;
    static float  fpsSum      = 0.0f;

    const float fps = dt > 0.0f ? 1.0f / dt : 0.0f;
    if (historyFill >= kHistorySize) {
        fpsSum -= fpsHistory[historyHead];
    }
    fpsHistory[historyHead] = fps;
    fpsSum += fps;
    historyHead = (historyHead + 1) % kHistorySize;
    historyFill = std::min(historyFill + 1, kHistorySize);

    const float avgFps = historyFill > 0 ? fpsSum / static_cast<float>(historyFill) : 0.0f;

    ImGui::Text("Frame Index: %llu", static_cast<unsigned long long>(app.getFrameIndex()));
    ImGui::Text("Delta: %.2f ms", dt * 1000.0f);
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Avg FPS: %.1f", avgFps);
}

void renderSessionContent(App& app)
{
    auto& renderServices = app.getRenderServices();
    auto* render        = renderServices.getRender();
    auto* renderRuntime = renderServices.getRenderRuntime();
    auto* swapchain     = render ? render->getSwapchain() : nullptr;

    ImGui::Text("State: %s", app.isRuntimeMode() ? "Runtime" : (app.isSimulationMode() ? "Simulation" : "Stopped"));
    if (renderRuntime) {
        ImGui::Text("Pipeline: %s", renderRuntime->isDeferredPipelineActive() ? "Deferred" : "Forward");
    }
    if (swapchain) {
        ImGui::Text("Swapchain: %u x %u", swapchain->getExtent().width, swapchain->getExtent().height);
    }
}

void renderEditorCameraContent(EditorLayer& layer, FreeCameraController& controller)
{
    auto& camera   = layer.getCamera();
    auto  position = camera._position;
    auto  rotation = camera._rotation;
    bool  bChanged = false;

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

} // namespace ya
