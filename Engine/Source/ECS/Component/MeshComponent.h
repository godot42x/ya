/**
 * @file MeshComponent.h
 * @brief Mesh Component - Pure geometry data reference
 *
 * Design:
 * - MeshComponent holds a single Mesh reference (not a Model!)
 * - Can be sourced from: primitive geometry OR Model's mesh by index
 * - For Model loading, use ModelComponent which creates child entities with MeshComponent
 * - This component is data-only, resolved by ResourceResolveSystem
 */
#pragma once

#include "Core/Base.h"
#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "Render/Mesh.h"

namespace ya
{

/**
 * @brief MeshComponent - Single mesh geometry reference
 *
 * Two usage modes:
 * 1. Primitive geometry (built-in shapes)
 * 2. Mesh from Model (set by ResourceResolveSystem when processing ModelComponent)
 *
 * Serialization format:
 * @code
 * {
 *   "MeshComponent": {
 *     "_primitiveGeometry": "Cube"  // Built-in primitive
 *   }
 * }
 * @endcode
 *
 * When created from ModelComponent:
 * @code
 * {
 *   "MeshComponent": {
 *     "_sourceModelPath": "Content/Models/character.fbx",
 *     "_meshIndex": 0
 *   }
 * }
 * @endcode
 */
struct MeshComponent : public IComponent
{
    YA_REFLECT_BEGIN(MeshComponent)
    YA_REFLECT_FIELD(_primitiveGeometry)
    YA_REFLECT_FIELD(_sourceModelPath)
    YA_REFLECT_FIELD(_meshIndex)
    YA_REFLECT_END()

    // ========================================
    // Serializable Data - Primitive Geometry Mode
    // ========================================

    EPrimitiveGeometry _primitiveGeometry = EPrimitiveGeometry::None; ///< Built-in geometry type

    // ========================================
    // Serializable Data - Model Mesh Mode
    // ========================================

    /**
     * @brief Path to the source Model (for serialization)
     * When this Entity is created from ModelComponent, this stores the Model path
     * so the mesh can be re-resolved after deserialization
     */
    std::string _sourceModelPath;

    /**
     * @brief Index of the mesh within the Model
     * Used together with _sourceModelPath to identify which mesh this component represents
     */
    uint32_t _meshIndex = 0;

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================

    Mesh* _cachedMesh = nullptr; ///< Resolved mesh pointer
    bool  _bResolved  = false;

    // ========================================
    // Resource Resolution
    // ========================================

    /**
     * @brief Resolve the mesh resource
     * Called by ResourceResolveSystem
     * @return true if successfully resolved
     */
    bool resolve();

    /**
     * @brief Force re-resolve
     */
    void invalidate()
    {
        _bResolved  = false;
        _cachedMesh = nullptr;
    }

    /**
     * @brief Check if resolved
     */
    bool isResolved() const { return _bResolved; }

    // ========================================
    // Mesh Access
    // ========================================

    /**
     * @brief Get the resolved mesh
     */
    Mesh* getMesh() const { return _cachedMesh; }

    /**
     * @brief Check if this component has a valid mesh source
     */
    bool hasMeshSource() const
    {
        return _primitiveGeometry != EPrimitiveGeometry::None || !_sourceModelPath.empty();
    }

    // ========================================
    // Setup Interface
    // ========================================

    /**
     * @brief Set to built-in primitive geometry
     */
    void setPrimitiveGeometry(EPrimitiveGeometry type)
    {
        _primitiveGeometry = type;
        _sourceModelPath.clear();
        _meshIndex = 0;
        invalidate();
    }

    /**
     * @brief Set to mesh from Model
     * Called by ResourceResolveSystem when creating child entities from ModelComponent
     */
    void setFromModel(const std::string& modelPath, uint32_t meshIndex, Mesh* mesh)
    {
        _primitiveGeometry = EPrimitiveGeometry::None;
        _sourceModelPath   = modelPath;
        _meshIndex         = meshIndex;
        _cachedMesh        = mesh;
        _bResolved         = (mesh != nullptr);
    }
};

} // namespace ya
