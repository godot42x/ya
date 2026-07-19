#pragma once

namespace ya
{

struct App;
struct EditorLayer;
class FreeCameraController;

void renderRuntimeToolsWindow(App& app, EditorLayer& layer, FreeCameraController& controller, float dt);
void migrateLegacyRuntimeSettings();

} // namespace ya
