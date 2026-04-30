/**
 * @file MeshSource.h
 * @brief Reusable mesh resource reference used by mesh components.
 *
 * MeshSource is the shared authoring + runtime storage for "which mesh does this component point to".
 * It is a nested reflected struct so that both static and skinned mesh components can embed it
 * without duplicating serialization/resolve logic.
 *
 * Note: this type is named MeshSource (not MeshRef) to avoid collision with the existing
 *       asset alias `using MeshRef = TAssetRef<Mesh>` in Core/Common/AssetRef.h.
 */
#pragma once

#include "Core/Base.h"
#include "Core/Reflection/Reflection.h"
#include "Render/Mesh.h"

namespace ya
{

/**
 * @brief Reference to a concrete mesh, either from a primitive cache or a loaded Model.
 *
 * Two serialized sources (mutually exclusive):
 *  1. Primitive geometry (built-in shapes).
 *  2. A mesh belonging to a Model (identified by Model path + mesh index).
 *
 * Runtime fields (_cachedMesh/_bResolved) are not serialized; ResourceResolveSystem calls resolve().
 */
struct MeshSource
{
    YA_REFLECT_BEGIN(MeshSource)
    YA_REFLECT_FIELD(_primitiveGeometry)
    YA_REFLECT_FIELD(_sourceModelPath)
    YA_REFLECT_FIELD(_meshIndex)
    YA_REFLECT_END()

    // ========================================
    // Serializable Data
    // ========================================

    EPrimitiveGeometry _primitiveGeometry = EPrimitiveGeometry::Cube;
    std::string        _sourceModelPath;
    uint32_t           _meshIndex = 0;

    // ========================================
    // Runtime State (not serialized)
    // ========================================

    Mesh* _cachedMesh = nullptr;
    bool  _bResolved  = false;

    // ========================================
    // Resource Resolution
    // ========================================

    bool resolve();

    void invalidate()
    {
        _bResolved  = false;
        _cachedMesh = nullptr;
    }

    bool isResolved() const { return _bResolved; }

    // ========================================
    // Access
    // ========================================

    Mesh* getMesh() const { return _cachedMesh; }

    bool hasSource() const
    {
        return _primitiveGeometry != EPrimitiveGeometry::None || !_sourceModelPath.empty();
    }

    // ========================================
    // Setup
    // ========================================

    void setPrimitiveGeometry(EPrimitiveGeometry type)
    {
        _primitiveGeometry = type;
        _sourceModelPath.clear();
        _meshIndex = 0;
        invalidate();
    }

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
