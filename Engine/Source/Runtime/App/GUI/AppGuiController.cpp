#include "Runtime/App/GUI/AppGuiController.h"

#include "Runtime/App/App.h"
#include "Runtime/App/GUI/AppProfilingFacade.h"
#include "Runtime/App/Utility/FPSCtrl.h"

#include "ImGuiHelper.h"

#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"
#include "Runtime/App/RenderRuntime.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>

namespace ya
{
extern ClearValue colorClearValue;
extern ClearValue depthClearValue;

namespace
{
struct DashboardMetric
{
    const char* label = "";
    const char* value = "";
};

void beginDashboardCard(const char* label)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.18f, 0.22f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.25f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.18f, 0.21f, 0.27f, 1.0f));
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
}

void renderMetricStrip(const DashboardMetric* metrics, int count)
{
    if (count <= 0) {
        return;
    }

    if (!ImGui::BeginTable("DashboardMetricStrip", count, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
        return;
    }

    for (int index = 0; index < count; ++index) {
        ImGui::TableNextColumn();
        ya::ImGuiStyleScope style;
        style.pushColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.13f, 0.16f, 0.92f));
        style.pushColor(ImGuiCol_Border, ImVec4(0.24f, 0.28f, 0.34f, 0.9f));
        style.pushVar(ImGuiStyleVar_ChildRounding, 6.0f);
        style.pushVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        style.pushVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        ImGui::BeginChild(metrics[index].label, ImVec2(0.0f, 58.0f), true);
        ImGui::TextDisabled("%s", metrics[index].label);
        ImGui::Spacing();
        ImGui::Text("%s", metrics[index].value);
        ImGui::EndChild();
    }

    ImGui::EndTable();
}

void renderFpsControl(FPSControl& fpsCtrl)
{
    if (ImGui::CollapsingHeader("FPS Control", 0)) {
        ImGui::Indent();

        ImGui::Text("FPS Limit: %.1f", fpsCtrl.fpsLimit);

        static float newFpsLimit = fpsCtrl.fpsLimit;
        ImGui::PushItemWidth(100.0f);
        ImGui::InputFloat("New: ", &newFpsLimit, 10.0f, 10.0f, "%.1f");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Confirm")) {
            fpsCtrl.setFPSLimit(newFpsLimit);
        }

        ImGui::Checkbox("Enable FPS Control", &fpsCtrl.bEnable);
        ImGui::Unindent();
    }
}

bool renderEditorCamera(App& app, FreeCamera& camera)
{
    auto position = camera._position;
    auto rotation = camera._rotation;
    bool bChanged = false;

    if (ImGui::CollapsingHeader("Camera Controls", 0)) {
        if (ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f)) {
            bChanged = true;
        }
        if (ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.f, -180.0f, 180.0f)) {
            bChanged = true;
        }
        ImGui::DragFloat("Move Speed", &app.cameraController._moveSpeed, 0.1f, 0.1f, 20.0f);
        ImGui::DragFloat("Rotation Speed", &app.cameraController._rotationSpeed, 1.f, 10.f, 180.f);
        ImGui::Text("Hold right mouse button to rotate camera");
        ImGui::Text("WASD: Move horizontally, QE: Move vertically");
    }

    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
    return bChanged;
}

void renderClearValues()
{
    if (ImGui::CollapsingHeader("Clear Values", 0)) {
        float color[4] = {colorClearValue.color.r, colorClearValue.color.g, colorClearValue.color.b, colorClearValue.color.a};
        if (ImGui::ColorEdit4("Color Clear Value", color)) {
            colorClearValue = ClearValue(color[0], color[1], color[2], color[3]);
        }
        float depth = depthClearValue.depthStencil.depth;
        if (ImGui::DragFloat("Depth Clear Value", &depth, 0.01f, 0.0f, 1.0f)) {
            depthClearValue = ClearValue(depth, depthClearValue.depthStencil.stencil);
        }
    }
}

void renderRenderingSettings(App& app)
{
    auto* renderRuntime = app.getRenderRuntime();
    if (!renderRuntime) {
        return;
    }

    static const char* renderPipelineNames[] = {"Forward", "Deferred"};
    int                currentRenderPipeline = static_cast<int>(renderRuntime->getPendingRenderPipeline());
    if (ImGui::Combo("Render Pipeline", &currentRenderPipeline, renderPipelineNames, IM_ARRAYSIZE(renderPipelineNames))) {
        renderRuntime->setPendingRenderPipeline(static_cast<RenderRuntime::ERenderPipeline>(currentRenderPipeline));
    }
    if (renderRuntime->getPendingRenderPipeline() != renderRuntime->getRenderPipeline()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "(switch pending)");
    }

    auto* render = app.getRender();
    YA_CORE_ASSERT(render, "Render is null");
    auto* swapchain = render->getSwapchain();

    ImGui::SeparatorText("Presentation");
    bool bVsync = swapchain->getVsync();
    if (ImGui::Checkbox("VSync", &bVsync)) {
        swapchain->setVsync(bVsync);
    }

    EPresentMode::T presentMode  = swapchain->getPresentMode();
    const char*     presentModes = "Immediate\0Mailbox\0FIFO\0FIFO Relaxed\0";
    if (ImGui::Combo("Present Mode", reinterpret_cast<int*>(&presentMode), presentModes)) {
        app.taskManager.registerFrameTask([swapchain, presentMode]()
                                          { swapchain->setPresentMode(presentMode); });
    }

    ImGui::SeparatorText("World Rendering");
    renderRuntime->renderWorldSettingsGUI();
}
} // namespace

void AppGuiController::onRenderGUI(App& app, float dt)
{
    YA_PROFILE_FUNCTION()
    auto& io = ImGui::GetIO();
    (void)io;

    auto renderContextPanel = [&]()
    {
        using namespace ImGui;

        static constexpr int ringBufSize = 120;
        static float         fpsRingBuf[ringBufSize]{};
        static int           fpsRingHead = 0;
        static int           fpsRingFill = 0;
        static float         fpsSum      = 0.0f;
        const float          currentFps  = dt > 0.0f ? 1.0f / dt : 0.0f;

        if (fpsRingFill >= ringBufSize) {
            fpsSum -= fpsRingBuf[fpsRingHead];
        }
        fpsRingBuf[fpsRingHead] = currentFps;
        fpsSum += currentFps;

        fpsRingHead = (fpsRingHead + 1) % ringBufSize;
        fpsRingFill = std::min(fpsRingFill + 1, ringBufSize);

        const float avgFps = fpsRingFill > 0 ? (fpsSum / static_cast<float>(fpsRingFill)) : 0.0f;

        char frameText[32]  = {};
        char deltaText[32]  = {};
        char fpsText[32]    = {};
        char avgFpsText[32] = {};
        std::snprintf(frameText, sizeof(frameText), "%d", App::_frameIndex);
        std::snprintf(deltaText, sizeof(deltaText), "%.2f ms", dt * 1000.0f);
        std::snprintf(fpsText, sizeof(fpsText), "%.1f", currentFps);
        std::snprintf(avgFpsText, sizeof(avgFpsText), "%.1f / %d", avgFps, fpsRingFill);
        const std::array<DashboardMetric, 4> overviewMetrics = {{
            {.label = "Frame", .value = frameText},
            {.label = "Delta", .value = deltaText},
            {.label = "FPS", .value = fpsText},
            {.label = "Avg FPS", .value = avgFpsText},
        }};
        renderMetricStrip(overviewMetrics.data(), static_cast<int>(overviewMetrics.size()));

        AppMode mode = app._appMode;

        beginDashboardCard("Session");
        if (ImGui::Combo("App Mode", reinterpret_cast<int*>(&mode), "Control\0Drawing\0")) {
            app._appMode = mode;
        }
        renderFpsControl(*FPSControl::get());
        if (ImGui::Button("Deserialize Scene")) {
            auto sceneManager = App::get()->getSceneManager();
            YA_CORE_ASSERT(sceneManager, "SceneManager is null");
            sceneManager->serializeToFile("Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json",
                                          app.getSceneManager()->getActiveScene());
        }

        std::string clickedPoints;
        for (const auto& p : app.clicked) {
            clickedPoints += std::format("({}, {}) ", static_cast<int>(p.x), static_cast<int>(p.y));
        }
        if (clickedPoints.empty()) {
            clickedPoints = "<none>";
        }
        ImGui::TextWrapped("Clicked Points: %s", clickedPoints.c_str());

        beginDashboardCard("Editor Controls");
        renderEditorCamera(app, app.camera);
        renderClearValues();

        beginDashboardCard("Rendering");
        renderRenderingSettings(app);


        beginDashboardCard("ImGui");
        if (ImGui::TreeNode("Metrics / Debug")) {
            ImGuiManager::get().onRenderGUI();
            ImGui::TreePop();
        }
    };

    if (ImGui::Begin("Render Panel")) {
        beginDashboardCard("Runtime Diagnostics");
        if (ImGui::TreeNode("Render2D Debug")) {
            Render2D::onImGui();
            ImGui::TreePop();
        }

        if (app._renderRuntime) {
            if (ImGui::TreeNode("Render Runtime")) {
                app._renderRuntime->renderGUI(dt);
                ImGui::TreePop();
            }
        }
        AppProfilingFacade::render(app);
    }
    ImGui::End();

    if (ImGui::Begin("Context Panel")) {
        renderContextPanel();
    }
    ImGui::End();
}

void AppGuiController::renderGUI(App& app, float dt)
{
    (void)app;
    (void)dt;
}

} // namespace ya
