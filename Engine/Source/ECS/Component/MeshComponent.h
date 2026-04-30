/**
 * @file MeshComponent.h
 * @brief Mesh Component - Pure geometry data reference
 *
 * Design:
 * - MeshComponent owns a MeshRef describing which Mesh this entity points to
 * - Two sources: primitive geometry OR Model's mesh by index
 * - For Model loading, use ModelComponent which creates child entities with MeshComponent
 * - This component is data-only, resolved by ResourceResolveSystem
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
 * @brief MeshComponent - Single mesh geometry reference
 *
 * Serialization format:
 * @code
 * {
 *   "MeshComponent": {
 *     "_mesh": {
 *       "_primitiveGeometry": "Cube"
 *     }
 *   }
 * }
 * @endcode
 *
 * When created from ModelComponent:
 * @code
 * {
 *   "MeshComponent": {
 *     "_mesh": {
 *       "_sourceModelPath": "Content/Models/character.fbx",
 *       "_meshIndex": 0
 *     }
 *   }
 * }
 * @endcode
 */
struct MeshComponent : public IComponent
{
    YA_REFLECT_BEGIN(MeshComponent)
    YA_REFLECT_FIELD(_mesh)
    YA_REFLECT_END()

    // ========================================
    // Serializable / Runtime Data
    // ========================================

    MeshSource _mesh;

    // ========================================
    // Resource Resolution (delegated to MeshRef)
    // ========================================

    bool resolve() { return _mesh.resolve(); }
    void invalidate() { _mesh.invalidate(); }
    bool isResolved() const { return _mesh.isResolved(); }

    // ========================================
    // Mesh Access
    // ========================================

    Mesh *getMesh() const { return _mesh.getMesh(); }
    bool  hasMeshSource() const { return _mesh.hasSource(); }

    // ========================================
    // Setup Interface
    // ========================================

    void setPrimitiveGeometry(EPrimitiveGeometry type) { _mesh.setPrimitiveGeometry(type); }

    void setFromModel(const std::string &modelPath, uint32_t meshIndex, Mesh *mesh)
    {
        _mesh.setFromModel(modelPath, meshIndex, mesh);
    }
};

} // namespace ya
