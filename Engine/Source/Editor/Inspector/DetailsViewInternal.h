#pragma once

#include "Editor/Inspector/DetailsView.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Reflection/ECSRegistry.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "ECS/Component/Terrain/TerrainComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Editor/EditorLayer.h"
#include "Editor/ImGui/ImGuiHelper.h"
#include "Editor/Inspector/TypeRenderer.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Runtime/Application/App.h"
#include "Scene/Node.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <unordered_set>
#include <vector>

namespace ya
{

inline constexpr size_t DETAILS_SCRIPT_INPUT_BUFFER_SIZE            = 256;
inline constexpr size_t DETAILS_SKYBOX_INPUT_BUFFER_SIZE            = 512;
inline constexpr const char* SKYBOX_SOURCE_TYPE_LABELS             = "Cube Faces\0Cylindrical\0";
inline constexpr const char* ENVIRONMENT_LIGHTING_SOURCE_TYPE_LABELS = "Scene Skybox\0Cube Faces\0Cylindrical\0";
inline constexpr float SKYBOX_PREVIEW_MAX_HEIGHT                   = 180.0f;
inline constexpr std::array<const char*, CubeFace_Count> SKYBOX_FACE_LABELS = {
    "+X",
    "-X",
    "+Y",
    "-Y",
    "+Z",
    "-Z",
};

inline bool drawPathInput(const char* id, std::string& path, size_t bufferSize)
{
    std::vector<char> buffer(bufferSize, '\0');
    strncpy_s(buffer.data(), buffer.size(), path.c_str(), buffer.size() - 1);
    if (!ImGui::InputText(id, buffer.data(), buffer.size())) {
        return false;
    }

    path = buffer.data();
    return true;
}

inline void drawTexturePreviewImage(const char* id, Texture* texture, float maxWidth, float maxHeight)
{
    if (!texture || !texture->getImageView()) {
        ImGui::TextDisabled("Preview unavailable");
        return;
    }

    const auto extent = texture->getExtent();
    if (extent.width == 0 || extent.height == 0) {
        ImGui::TextDisabled("Preview unavailable");
        return;
    }

    const float scale = std::min(maxWidth / static_cast<float>(extent.width),
                                 maxHeight / static_cast<float>(extent.height));
    const ImVec2 size = ImVec2(static_cast<float>(extent.width) * scale,
                               static_cast<float>(extent.height) * scale);
    auto sampler = TextureLibrary::get().getLinearSampler();
    if (!sampler) {
        ImGui::TextDisabled("Preview sampler unavailable");
        return;
    }

    ImGui::PushID(id);
    ImGuiHelper::Image(texture->getImageView(), sampler.get(), "No Preview", size);
    ImGui::PopID();
}

} // namespace ya
