#pragma once

#include "GameEditor/EditorLayer.h"

#include "Core/Config/ConfigManager.h"
#include "Core/KeyCode.h"
#include "Core/Manager/Facade.h"
#include "Core/Math/Math.h"
#include "App/Module/ProjectDescriptor.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/System/RayCastMousePickingSystem.h"
#include "ECS/Systems/TransformSystem.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Resource/AssetManager.h"
#include "RHI/Backend/TextureLibrary.h"
#include "GameRuntime/App.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"

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
inline constexpr const char* kViewportCameraOverlayEnabledKey = "viewport.cameraOverlay.enabled";

inline constexpr float kViewportCameraOverlayMarginX      = 10.0f;
inline constexpr float kViewportCameraOverlayMarginY      = 10.0f;
inline constexpr float kViewportCameraOverlayLineSpacing  = 4.0f;

inline constexpr const char* kCubeFaceLabels[6] = {
    "PosX",
    "NegX",
    "PosY",
    "NegY",
    "PosZ",
    "NegZ",
};

} // namespace ya
