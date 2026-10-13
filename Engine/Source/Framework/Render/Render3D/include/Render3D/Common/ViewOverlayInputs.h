#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Texture.h"

#include <glm/glm.hpp>
#include <vector>

namespace ya
{

struct Mesh;

/// Per-View overlay packet: the billboards, gizmos and skybox availability
/// assembled by the pipeline while it records the View.
///
/// This is values, not device state: the two features that consume it
/// (SkyboxFeature reads `skybox`, OverlayFeature reads the lists) and the id
/// feature that mirrors the billboard silhouettes all take it by const
/// reference. Keeping it here rather than on a stage header is what lets a
/// feature read the packet without depending on the device object that draws it.
struct ViewOverlayFrameInputs
{
    struct BillboardInput
    {
        glm::vec3      worldCenter    = glm::vec3(0.0f);
        glm::vec3      worldDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec2      worldSize      = glm::vec2(1.0f);
        glm::vec4      tint           = glm::vec4(1.0f);
        TextureBinding textureBinding{};
        uint32_t       entityId       = 0;
        uint32_t       textureIndex   = 0;
    };

    struct DirectionGizmoInput
    {
        glm::mat4 coneModel     = glm::mat4(1.0f);
        glm::mat4 cylinderModel = glm::mat4(1.0f);
        glm::vec3 lineStart     = glm::vec3(0.0f);
        glm::vec3 lineEnd       = glm::vec3(0.0f);
    };

    struct SkyboxInput
    {
        bool                bAvailable    = false;
        DescriptorSetHandle descriptorSet = nullptr;
        Mesh*               mesh          = nullptr;
    };

    std::vector<BillboardInput>      billboards{};
    std::vector<DirectionGizmoInput> directionGizmos{};
    SkyboxInput                      skybox{};
};

} // namespace ya
