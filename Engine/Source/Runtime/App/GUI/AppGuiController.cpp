#include "Runtime/App/GUI/AppGuiController.h"

#include "Runtime/App/App.h"
#include "Runtime/App/Utility/FPSCtrl.h"

#include "ImGuiHelper.h"

#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"

#include <algorithm>
#include <format>

namespace ya
{
extern ClearValue colorClearValue;
extern ClearValue depthClearValue;

namespace
{
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
} // namespace

void App::onRenderGUI(float dt)
{
    AppGuiController::onRenderGUI(*this, dt);
}

void App::renderGUI(float dt)
{
    AppGuiController::renderGUI(*this, dt);
}

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
        ImGui::Text("Frame: %d, DeltaTime: %3.2fms", App::_frameIndex, dt * 1000.f);
        ImGui::Text("FPS: %4.1f   (avg %4.1f over %3d)", currentFps, avgFps, fpsRingFill);
        renderFpsControl(*FPSControl::get());

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

        AppMode mode = app._appMode;
        if (ImGui::Combo("App Mode", reinterpret_cast<int*>(&mode), "Control\0Drawing\0")) {
            app._appMode = mode;
        }

        ImGui::SeparatorText("Interaction");
        std::string clickedPoints;
        for (const auto& p : app.clicked) {
            clickedPoints += std::format("({}, {}) ", (int)p.x, (int)p.y);
        }
        ImGui::Text("Clicked Points: %s", clickedPoints.c_str());

        if (ImGui::Button("Deserialize Scene")) {
            auto sceneManager = App::get()->getSceneManager();
            YA_CORE_ASSERT(sceneManager, "SceneManager is null");
            sceneManager->serializeToFile("Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json",
                                          app.getSceneManager()->getActiveScene());
        }

        ImGui::SeparatorText("Editor");
        renderEditorCamera(app, app.camera);
        renderClearValues();

        if (ImGui::TreeNode("ImGUI")) {
            ImGuiManager::get().onRenderGUI();
            ImGui::TreePop();
        }
    };

    if (ImGui::Begin("Render Panel")) {
        if (ImGui::TreeNode("Render 2D")) {
            Render2D::onImGui();
            ImGui::TreePop();
        }

        if (app._renderRuntime) {
            app._renderRuntime->renderGUI(dt);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Context Panel")) {
        renderContextPanel();
    }
    ImGui::End();
}

void AppGuiController::renderGUI(App& app, float dt)
{
    app._editorLayer->onImGuiRender([&app, dt]()
                                    { app.onRenderGUI(dt); });
}

} // namespace ya
