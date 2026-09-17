#pragma once

#include "Core/Math/Geometry.h"
#include "Resource/Core/EngineMeshData.h"

#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

/// Rigid placement for composing engine meshes out of primitives.
///
/// Every primitive generator emits geometry at the origin in a fixed axis
/// (cube centered, cylinder/cone along +Y), so a composite part is "take the
/// shape, rotate its axis onto `axis`, then drop it at `center". Engine meshes
/// stay a function of code: no asset, no authoring data, no scene state.
struct YA_RESOURCE_CORE_API EngineMeshBuilder
{
    EngineMeshData mesh;

    explicit EngineMeshBuilder(std::string name) { mesh.name = std::move(name); }

    /// Append `source` placed by a rigid transform. Indices are rebased.
    void append(const std::vector<Vertex>& source, const std::vector<uint32_t>& sourceIndices, const glm::mat4& transform)
    {
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        const glm::mat3 basis(transform);

        mesh.vertices.reserve(mesh.vertices.size() + source.size());
        for (const Vertex& vertex : source) {
            Vertex placed    = vertex;
            placed.position  = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
            placed.normal    = glm::normalize(basis * vertex.normal);
            placed.tangent   = glm::normalize(basis * vertex.tangent);
            mesh.vertices.push_back(placed);
        }

        mesh.indices.reserve(mesh.indices.size() + sourceIndices.size());
        for (const uint32_t index : sourceIndices) {
            mesh.indices.push_back(base + index);
        }
    }

    /// Cube of `size` centred at `center`.
    void box(const glm::vec3& size, const glm::vec3& center)
    {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
        PrimitiveGeometry::createCube(size, vertices, indices);
        append(vertices, indices, glm::translate(glm::mat4(1.0f), center));
    }

    /// Cylinder with its axis rotated onto `axis`, centred at `center`.
    void cylinder(float radius, float height, uint32_t segments, const glm::vec3& axis, const glm::vec3& center)
    {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
        PrimitiveGeometry::createCylinder(radius, height, segments, vertices, indices);
        append(vertices, indices, glm::translate(glm::mat4(1.0f), center) * alignYTo(axis));
    }

    /// Rotation taking the primitives' +Y axis onto `axis`.
    [[nodiscard]] static glm::mat4 alignYTo(const glm::vec3& axis)
    {
        const glm::vec3 target = glm::normalize(axis);
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const float     alignment = glm::clamp(glm::dot(up, target), -1.0f, 1.0f);

        if (alignment > 0.9999f) {
            return glm::mat4(1.0f);
        }
        if (alignment < -0.9999f) {
            return glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        }
        return glm::rotate(glm::mat4(1.0f), glm::acos(alignment), glm::normalize(glm::cross(up, target)));
    }
};

} // namespace ya
