#include "GameRuntime/Settings/UiFontSettings.h"

#include "Core/Config/ConfigManager.h"

namespace ya::ui_font_settings
{
namespace
{

/// Store the configured id. Only ever called with a validated catalog id, so the
/// document cannot accumulate faces that no longer exist.
void writeStoredFace(std::string_view faceId)
{
    if (!ConfigManager::get().hasDocument(kConfigDocument)) {
        // No config file (a tool run, a test host). The selection stays in-memory
        // for this process; creating a document here would drop a file next to an
        // app that never asked for one.
        return;
    }
    ConfigManager::Editor(kConfigDocument).set(kKeyFace, std::string(faceId));
}

} // namespace

std::vector<FOption> availableFaces()
{
    std::vector<FOption> options;
    for (const FUiFontFace& face : uiFontFaces()) {
        options.push_back(FOption{
            .id         = std::string(face.id),
            .label      = std::string(face.label),
            .bMonospace = face.bMonospace,
            .bAvailable = !resolveUiFontFacePath(face).empty(),
        });
    }
    return options;
}

std::string faceId()
{
    std::string stored;
    if (ConfigManager::get().hasDocument(kConfigDocument) &&
        ConfigManager::get().tryGet<std::string>(kConfigDocument, kKeyFace, stored) &&
        findUiFontFace(stored) != nullptr) {
        return stored;
    }
    return std::string(defaultUiFontFace().id);
}

std::string setFaceId(std::string_view faceId)
{
    const FUiFontFace* face = findUiFontFace(faceId);
    const std::string  resolved = face ? std::string(face->id) : std::string(defaultUiFontFace().id);
    writeStoredFace(resolved);
    return resolved;
}

bool apply(IRender& render)
{
    return FontManager::get()->loadUiFontStack(render, faceId());
}

bool applyAndStore(IRender& render, std::string_view faceId)
{
    const FUiFontFace* face = findUiFontFace(faceId);
    if (face == nullptr) {
        return false;
    }
    // Store only AFTER the stack loaded: persisting a face this machine cannot
    // render would come back broken on the next launch.
    if (!FontManager::get()->loadUiFontStack(render, face->id)) {
        return false;
    }
    writeStoredFace(face->id);
    return true;
}

} // namespace ya::ui_font_settings
