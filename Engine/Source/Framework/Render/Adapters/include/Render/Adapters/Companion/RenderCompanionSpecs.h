#pragma once

#include "Core/Api.h"
#include "Scene3D/CompanionRegistry.h"

#include <glm/glm.hpp>
#include <string>

namespace ya
{

/// Editor camera body policy, injected by the Host (which owns the config
/// source). The spec never reaches Host config.
struct CameraCompanionPolicy
{
    /// The body is a shaded solid, like UE's ACameraActor camera mesh: an
    /// unlit flat fill reads as a silhouette with no shape at all.
    glm::vec3   diffuse   = glm::vec3(0.35f, 0.85f, 1.0f);
    glm::vec3   ambient   = glm::vec3(0.09f, 0.21f, 0.25f);
    glm::vec3   specular  = glm::vec3(0.30f);
    float       shininess = 32.0f;
    float       scale     = 1.0f;
    /// Engine content, not a scene asset: the body is a normal mesh source
    /// pointing at engine geometry, so it needs no special mesh pipeline.
    std::string meshPath   = "Engine:Content/Editor/Gizmos/camera_body.obj";
    uint32_t    meshIndex  = 0;
};

/// Render/editor policy for light billboards.
struct LightBillboardConfig
{
    bool        enabled          = true;
    float       screenSizePixels = 30.0f;
    float       minWorldScale    = 0.4f;
    glm::vec4   tint             = glm::vec4(1.0f);
    std::string texturePath      = "Engine:Content/TestTextures/icons8-light-64.png";
};

struct LightBillboardPolicy
{
    LightBillboardConfig point       = LightBillboardConfig{true, 30.0f, 0.4f, glm::vec4(1.0f, 0.95f, 0.45f, 1.0f), "Engine:Content/TestTextures/icons8-light-64.png"};
    LightBillboardConfig directional = LightBillboardConfig{true, 42.0f, 1.0f, glm::vec4(1.0f, 0.96f, 0.72f, 1.0f), "Engine:Content/TestTextures/icons8-light-64.png"};
};

/// Companion declarations for the render-side hosts.
///
/// The declaration is the whole boundary statement: kind (editor gizmo),
/// editor editability and packing class are decided here once, and the builder
/// only says how to build and refresh the visuals. Camera body and light icons
/// are companions of the host entity, so the host keeps its own mesh and
/// material slots free.
[[nodiscard]] YA_RENDER_ECS_ADAPTERS_API CompanionSpec makeCameraCompanionSpec(const CameraCompanionPolicy& policy);
[[nodiscard]] YA_RENDER_ECS_ADAPTERS_API CompanionSpec makePointLightCompanionSpec(const LightBillboardConfig& config);
[[nodiscard]] YA_RENDER_ECS_ADAPTERS_API CompanionSpec makeDirectionalLightCompanionSpec(const LightBillboardConfig& config);

} // namespace ya
