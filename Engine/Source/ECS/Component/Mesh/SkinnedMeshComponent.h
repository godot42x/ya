/**
 * @file SkinnedMeshComponent.h
 * @brief Authoring + runtime reference to a mesh driven by skeletal animation.
 *
 * SkinnedMeshComponent mirrors StaticMeshComponent but flags an entity as a skinned mesh,
 * letting ECS views separate skinned and static work cleanly. Skeleton state itself is
 * owned by a SkeletonAnimatorComponent (introduced in a later stage), typically placed on
 * the owning model root entity so one animator can drive multiple skinned meshes.
 */
#pragma once

#include "Core/Base.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "ECS/Component/Mesh/MeshSource.h"
#include "Render/Mesh.h"

namespace ya
{

struct SkeletonAnimatorComponent;


/**
 * @brief Entity component pointing at a single mesh intended to be skinned.
 *
 * This component does not own skeleton/animation state; it only marks the entity as a
 * skinned mesh render target. Animation state lives on a SkeletonAnimatorComponent
 * elsewhere in the scene.
 *
 * Serialization format:
 * @code
 * {
 *   "SkinnedMeshComponent": {
 *     "_mesh": {
 *       "_sourceModelPath": "Content/Models/character.fbx",
 *       "_meshIndex": 0
 *     }
 *   }
 * }
 * @endcode
 */
struct SkinnedMeshComponent : public IComponent
{
    YA_REFLECT_BEGIN(SkinnedMeshComponent)
    YA_REFLECT_FIELD(_mesh)
    YA_REFLECT_END()

    // ========================================
    // Serializable / Runtime Data
    // ========================================

    MeshSource _mesh;

    // ========================================
    // Runtime-only (not serialized)
    // ========================================

    // Pointer to the SkeletonAnimatorComponent on the model-root entity that drives
    // this mesh's skinning. Populated by ModelInstantiationSystem when the entity
    // is instantiated. Do NOT rely on this across scene reloads; authoring systems
    // must re-resolve it.
    SkeletonAnimatorComponent *_animator = nullptr;

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

    void setFromModel(const std::string &modelPath, uint32_t meshIndex, Mesh *mesh)
    {
        _mesh.setFromModel(modelPath, meshIndex, mesh);
    }
};

} // namespace ya
