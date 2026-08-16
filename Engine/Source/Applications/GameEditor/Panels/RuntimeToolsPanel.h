#pragma once

namespace ya
{

struct App;
struct EditorLayer;
struct FreeCameraController;

struct RuntimeToolsPanel
{
    EditorLayer*           _owner      = nullptr;
    FreeCameraController*  _controller = nullptr;

    RuntimeToolsPanel(EditorLayer* owner)
        : _owner(owner)
    {
    }

    void setCameraController(FreeCameraController* controller) { _controller = controller; }
    void onImGuiRender(App& app, float dt);
};

void migrateLegacyRuntimeSettings();

} // namespace ya
