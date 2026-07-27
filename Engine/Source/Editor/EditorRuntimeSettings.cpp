#include "Editor/EditorRuntimeSettings.h"

#include "Config/ConfigManager.h"
#include "Runtime/Application/Utility/FPSCtrl.h"

namespace ya::editor_runtime_settings
{

namespace
{

constexpr const char* CONFIG_DOCUMENT   = "editor";
constexpr const char* KEY_FPS_ENABLE    = "runtime.framePacing.enabled";
constexpr const char* KEY_FPS_LIMIT     = "runtime.framePacing.fpsLimit";

} // namespace

void load()
{
    auto& config = ConfigManager::get();
    if (!config.hasDocument(CONFIG_DOCUMENT)) {
        return;
    }

    auto* fpsControl    = FPSControl::get();
    fpsControl->bEnable = config.getOr<bool>(CONFIG_DOCUMENT, KEY_FPS_ENABLE, fpsControl->bEnable);
    fpsControl->setFPSLimit(config.getOr<float>(CONFIG_DOCUMENT, KEY_FPS_LIMIT, fpsControl->fpsLimit));
}

void save()
{
    auto& config = ConfigManager::get();
    if (!config.hasDocument(CONFIG_DOCUMENT)) {
        return;
    }

    const auto* fpsControl = FPSControl::get();
    ConfigManager::Editor(CONFIG_DOCUMENT)
        .set(KEY_FPS_ENABLE, fpsControl->bEnable)
        .set(KEY_FPS_LIMIT, fpsControl->fpsLimit)
        .flush();
}

} // namespace ya::editor_runtime_settings
