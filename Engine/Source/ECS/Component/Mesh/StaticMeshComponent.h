/**
 * @file StaticMeshComponent.h
 * @brief Authoring + runtime reference to a non-skinned mesh.
 *
 * StaticMeshComponent is the non-skinned counterpart of SkinnedMeshComponent. It embeds a
 * MeshSource to point at either a primitive geometry or a Model mesh index. A Static/Skinned
 * split lets ECS views filter "only static mesh work" or "only skinned mesh work" without
 * extra runtime branching.
 *
 * During this refactor, StaticMeshComponent lives alongside the legacy MeshComponent; call
 * sites will migrate in a later stage.
 */
#pragma once

#include "Core/Base.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "ECS/Component/Mesh/MeshSource.h"
#include "Render/Mesh.h"

namespace ya
{

/**
 * @brief Entity component pointing at a single non-skinned mesh.
 *
 * Serialization format:
 * @code
 * {
 *   "StaticMeshComponent": {
 *     "_mesh": {
 *       "_primitiveGeometry": "Cube"
 *     }
 *   }
 * }
 * @endcode
 */
struct StaticMeshComponent : public IComponent
{
    YA_REFLECT_BEGIN(StaticMeshComponent)
    YA_REFLECT_FIELD(_mesh)
    YA_REFLECT_END()

    // ========================================
    // Serializable / Runtime Data
    // ========================================

    MeshSource _mesh;

    // ========================================
    // Resource Resolution (delegated to MeshSource)
    // ========================================

    bool resolve() { return _mesh.resolve(); }
    void invalidate() { _mesh.invalidate(); }
    bool isResolved() const { return _mesh.isResolved(); }

    // ========================================
    // Access
    // ========================================

    Mesh *getMesh() const { return _mesh.getMesh(); }
    bool  hasMeshSource() const { return _mesh.hasSource(); }

    // ========================================
    // Setup
    // ========================================

    void setPrimitiveGeometry(EPrimitiveGeometry type) { _mesh.setPrimitiveGeometry(type); }

    void setFromModel(const std::string &modelPath, uint32_t meshIndex, Mesh *mesh)
    {
        _mesh.setFromModel(modelPath, meshIndex, mesh);
    }
};

} // namespace ya
