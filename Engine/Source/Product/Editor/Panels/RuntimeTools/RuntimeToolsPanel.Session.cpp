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


void renderSessionContent(App& app)
{
    auto& renderServices = app.getRenderServices();
    auto* render        = renderServices.getRender();
    auto* renderRuntime = renderServices.getRenderRuntime();
    auto* swapchain     = render ? render->getSwapchain() : nullptr;
    auto* fpsControl    = FPSControl::get();

    ImGui::Text("State: %s", app.isRuntimeMode() ? "Runtime" : (app.isSimulationMode() ? "Simulation" : "Stopped"));
    if (renderRuntime) {
        ImGui::Text("Pipeline: %s", renderRuntime->isDeferredPipelineActive() ? "Deferred" : "Forward");
    }
    if (swapchain) {
        ImGui::Text("Swapchain: %u x %u", swapchain->getExtent().width, swapchain->getExtent().height);
    }

    ImGui::SeparatorText("Frame Pacing");
    if (ImGui::Checkbox("Enable FPS Limit", &fpsControl->bEnable)) {
        editor_runtime_settings::save();
    }

    float fpsLimit = fpsControl->fpsLimit;
    ImGui::BeginDisabled(!fpsControl->bEnable);
    if (ImGui::DragFloat("FPS Limit", &fpsLimit, 1.0f, 1.0f, 500.0f, "%.0f")) {
        fpsControl->setFPSLimit(fpsLimit);
        editor_runtime_settings::save();
    }
    ImGui::EndDisabled();

    ImGui::TextDisabled("Frame budget: %.2f ms", fpsControl->remainSec * 1000.0f);
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
