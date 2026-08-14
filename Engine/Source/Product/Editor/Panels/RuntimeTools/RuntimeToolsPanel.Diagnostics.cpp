#include "Editor/Panels/RuntimeToolsPanelInternal.h"

namespace ya
{

void renderRender2DDebugContent()
{
    auto& data = Render2D::debugState();
    ImGui::Checkbox("Reverse Viewport Y", &data.bReverseViewport);

    int worldCullMode = static_cast<int>(data.worldCullMode);
    if (ImGui::Combo("World Cull Mode", &worldCullMode, kCullModeLabels)) {
        data.worldCullMode = static_cast<ECullMode::T>(worldCullMode);
    }

    int screenCullMode = static_cast<int>(data.screenCullMode);
    if (ImGui::Combo("Screen Cull Mode", &screenCullMode, kCullModeLabels)) {
        data.screenCullMode = static_cast<ECullMode::T>(screenCullMode);
    }

    ImGui::InputInt("Text Layout Mode", &data.TextLayoutMode);
    const auto& session = Render2D::sessionState();
    ImGui::TextDisabled("Viewport: %u x %u", session.windowWidth, session.windowHeight);
}

void renderDiagnosticsContent(App& app)
{
    auto* runtime = app.getRenderServices().getRenderRuntime();
    if (!runtime) {
        return;
    }

    auto& renderDoc       = runtime->getDiagnosticsService().getRenderDocState();
    const bool bAvailable = renderDoc.capture && renderDoc.capture->isAvailable();
    ImGui::Text("RenderDoc: %s", bAvailable ? "Available" : "Unavailable");
    ImGui::TextWrapped("DLL Path: %s", renderDoc.configuredDllPath.empty() ? "<default>" : renderDoc.configuredDllPath.c_str());
    ImGui::TextWrapped("Output Dir: %s", renderDoc.configuredOutputDir.empty() ? "<default>" : renderDoc.configuredOutputDir.c_str());
    ImGui::TextWrapped("Last Capture: %s", renderDoc.lastCapturePath.empty() ? "<none>" : renderDoc.lastCapturePath.c_str());

    if (!bAvailable) {
        return;
    }

    bool bCaptureEnabled = renderDoc.capture->isCaptureEnabled();
    if (ImGui::Checkbox("Capture Enabled", &bCaptureEnabled)) {
        renderDoc.capture->setCaptureEnabled(bCaptureEnabled);
    }

    bool bHudVisible = renderDoc.capture->isHUDVisible();
    if (ImGui::Checkbox("Show RenderDoc HUD", &bHudVisible)) {
        renderDoc.capture->setHUDVisible(bHudVisible);
    }

    ImGui::Text("Capturing: %s", renderDoc.capture->isCapturing() ? "Yes" : "No");
    ImGui::Text("Delay Frames: %u", renderDoc.capture->getDelayFrames());
    ImGui::Combo("On Capture", &renderDoc.onCaptureAction, "None\0Open Replay UI\0Open Capture Folder\0");

    ImGui::BeginDisabled(!renderDoc.capture->isCaptureEnabled());
    if (ImGui::Button("Capture Next Frame (F9)")) {
        renderDoc.capture->requestNextFrame();
    }
    if (ImGui::Button("Capture After 120 Frames (Ctrl+F9)")) {
        renderDoc.capture->requestAfterFrames(120);
    }
    ImGui::EndDisabled();
}

void renderDebugPrimitivesContent(App& app)
{
    auto* runtime = app.getRenderServices().getRenderRuntime();
    if (!runtime) {
        return;
    }

    auto& debugSystem = runtime->getDebugRenderSystem();
    auto settings     = debugSystem.buildSettingsSnapshot();
    bool changed      = false;
    changed |= ImGui::Checkbox("Enabled", &settings.bEnabled);
    changed |= ImGui::Checkbox("Depth Test", &settings.bDepthTest);
    changed |= ImGui::Checkbox("Draw Lines", &settings.bDrawLines);
    changed |= ImGui::Checkbox("Draw Shapes", &settings.bDrawShapes);
    ImGui::Separator();
    ImGui::Text("Pending: %zu lines, %zu shapes", settings.pendingLineCount, settings.pendingShapeCount);
    ImGui::Text("Frame: %zu lines, %zu shapes", settings.frameLineCount, settings.frameShapeCount);
    ImGui::Text("Immediate: %zu lines, %zu shapes", settings.immediateLineCount, settings.immediateShapeCount);
    if (changed) {
        debugSystem.requestSettings(settings);
    }

    if (ImGui::TreeNode("Pipelines")) {
        renderGraphicsPipelineInspector("Lines", debugSystem.getPrimitives()._linePipeline.get());
        renderGraphicsPipelineInspector("Shapes", debugSystem.getPrimitives()._shapePipeline.get());
        ImGui::TreePop();
    }
}

} // namespace ya
