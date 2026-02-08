#include "App.h"
#include "Core/App/FPSCtrl.h"
#include "Core/Camera/Camera.h"

// Windows
#if defined(_WIN32)
    #include <windows.h>
#endif

namespace ya
{


extern ClearValue colorClearValue;
extern ClearValue depthClearValue;


void imcFpsControl(FPSControl &fpsCtrl)
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

bool imcEditorCamera(FreeCamera &camera)
{
    auto position = camera._position;
    auto rotation = camera._rotation;
    bool bChanged = false;

    // Add camera control settings to UI
    if (ImGui::CollapsingHeader("Camera Controls", 0)) {
        if (ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f)) {
            bChanged = true;
        }
        if (ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.f, -180.0f, 180.0f)) {
            bChanged = true;
        }
        ImGui::DragFloat("Move Speed", &App::get()->cameraController._moveSpeed, 0.1f, 0.1f, 20.0f);
        ImGui::DragFloat("Rotation Speed", &App::get()->cameraController._rotationSpeed, 1.f, 10.f, 180.f);
        ImGui::Text("Hold right mouse button to rotate camera");
        ImGui::Text("WASD: Move horizontally, QE: Move vertically");
    }


    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
    return bChanged;
}

void imcClearValues()
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


void App::handleSystemSignals()
{
#if !defined(_WIN32)
    auto handler = [](int signal) {
        if (!App::_instance) {
            return;
        }
        YA_CORE_INFO("Received signal: {}", signal);

        switch (signal) {
        case SIGINT:
        case SIGTERM:
        {
            App::_instance->requestQuit();
        } break;
        default:
            break;
        }
    };

    std::signal(SIGINT, handler);  // Ctrl+C
    std::signal(SIGTERM, handler); // Termination request
#else
    // linux: will continue to execute after handle the signal
    // Windows: need 用SetConsoleCtrlHandler来拦截并阻止默认退出
    SetConsoleCtrlHandler(
        [](DWORD dwCtrlType) -> BOOL {
            switch (dwCtrlType) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
                YA_CORE_INFO("Received Ctrl+C, requesting graceful shutdown...");
                if (App::_instance) {
                    App::_instance->requestQuit();
                }
                return true; // 返回TRUE阻止默认的终止行为
            case CTRL_CLOSE_EVENT:
            case CTRL_LOGOFF_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                YA_CORE_INFO("Received system shutdown event");
                if (App::_instance) {
                    App::_instance->requestQuit();
                }
                return true; // 返回TRUE阻止默认的终止行为
            };

            return FALSE; // 对于其他事件，使用默认处理
        },
        TRUE);
#endif
}


} // namespace ya