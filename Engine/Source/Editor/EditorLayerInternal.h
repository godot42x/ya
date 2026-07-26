#pragma once

#include "Editor/EditorLayer.h"

#include "Config/ConfigManager.h"
#include "Core/KeyCode.h"
#include "Core/Manager/Facade.h"
#include "Core/Math/Math.h"
#include "Core/Module/ProjectDescriptor.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/Terrain/TerrainComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/RayCastMousePickingSystem.h"
#include "ECS/System/TransformSystem.h"
#include "Editor/EditorCommon.h"
#include "Editor/ImGui/ImGuiHelper.h"
#include "Render/Core/RenderImage.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Resource/AssetManager.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Runtime/Application/App.h"
#include "Scene/Node.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

#include <ImGuizmo.h>
#include <filesystem>
#include <format>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace ya
{

inline std::string normalizeConfigLabel(const std::string& label)
{
    std::string normalized;
    normalized.reserve(label.size());
    for (char ch : label) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        else {
            normalized.push_back('_');
        }
    }
    return normalized;
}

inline std::string buildDeferredMaskConfigKey(const std::string& slotLabel)
{
    return std::format("debugWindow.debugSlotImageMask.{}", normalizeConfigLabel(slotLabel));
}

inline std::string buildDebugGroupConfigKey(const std::string& groupLabel)
{
    return std::format("debugWindow.debugGroupViewer.{}", normalizeConfigLabel(groupLabel));
}

inline std::string buildDebugGroupItemConfigKey(const std::string& groupLabel, uint32_t itemIndex)
{
    return std::format("debugWindow.debugGroupViewerSelection.{}_item_{}", normalizeConfigLabel(groupLabel), itemIndex);
}

inline std::string buildDebugGroupSelectionConfigKey(const std::string& groupLabel)
{
    return std::format("debugWindow.debugGroupSelection.{}", normalizeConfigLabel(groupLabel));
}

inline constexpr const char* kEditorConfigDocument            = "editor";
inline constexpr const char* kImGuiFontSizeBaseKey            = "imgui.fontSizeBase";
inline constexpr const char* kImGuiFontScaleMainKey           = "imgui.fontScaleMain";
inline constexpr const char* kImGuiFontScaleDpiKey            = "imgui.fontScaleDpi";
inline constexpr const char* kViewportCameraOverlayEnabledKey = "viewport.cameraOverlay.enabled";

inline constexpr float kViewportCameraOverlayMarginX      = 10.0f;
inline constexpr float kViewportCameraOverlayMarginY      = 10.0f;
inline constexpr float kViewportCameraOverlayLineSpacing  = 4.0f;

inline void loadImGuiSettingsFromConfig()
{
    auto& config = ConfigManager::get();
    if (!config.hasDocument(kEditorConfigDocument)) {
        return;
    }

    auto& style = ImGui::GetStyle();
    const float fallbackFontSizeBase = style.FontSizeBase > 0.0f ? style.FontSizeBase : ImGui::GetFontSize();
    style.FontSizeBase = config.getOr<float>(kEditorConfigDocument, kImGuiFontSizeBaseKey, fallbackFontSizeBase);
    if (style.FontSizeBase <= 0.0f) {
        style.FontSizeBase = fallbackFontSizeBase;
    }
    style.FontScaleMain = config.getOr<float>(kEditorConfigDocument, kImGuiFontScaleMainKey, style.FontScaleMain);
    style.FontScaleDpi = config.getOr<float>(kEditorConfigDocument, kImGuiFontScaleDpiKey, style.FontScaleDpi);
    style._NextFrameFontSizeBase = style.FontSizeBase;
}

inline void saveImGuiSettingsToConfig()
{
    const auto& style = ImGui::GetStyle();
    ConfigManager::Editor(kEditorConfigDocument)
        .set(kImGuiFontSizeBaseKey, style.FontSizeBase)
        .set(kImGuiFontScaleMainKey, style.FontScaleMain)
        .set(kImGuiFontScaleDpiKey, style.FontScaleDpi)
        .flush();
}

inline constexpr const char* kCubeFaceLabels[6] = {
    "PosX",
    "NegX",
    "PosY",
    "NegY",
    "PosZ",
    "NegZ",
};

} // namespace ya
