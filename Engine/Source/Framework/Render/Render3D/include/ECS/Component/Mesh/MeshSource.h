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
#include "Core/Common/AssetSlot.h"
#include "Core/Math/Geometry.h"
#include "Core/Reflection/Reflection.h"

#include <memory>

namespace ya
{

struct Mesh;
struct Model;

/**
 * @brief Reference to a concrete mesh, either from a primitive cache or a loaded Model.
 *
 * Two serialized sources (mutually exclusive):
 *  1. Primitive geometry (built-in shapes).
 *  2. A mesh belonging to a Model (identified by Model path + mesh index).
 *
 * Runtime fields (_cachedMesh/_bResolved) are not serialized; GameplayResourceBinding calls resolve().
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

    /// The model slot a path-sourced mesh resolves through. The slot keeps
    /// the Model (and with it the Mesh) alive while this source holds the
    /// handle; its fill observers let the processor re-resolve on load
    /// completion without polling.
    AssetHandle<Model> _modelHandle;

    // ========================================
    // Resource Resolution
    // ========================================

    bool resolve();

    void invalidate()
    {
        _bResolved   = false;
        _cachedMesh  = nullptr;
        _modelHandle.reset();
    }

    bool isResolved() const { return _bResolved; }

    // ========================================
    // Access
    // ========================================

    Mesh* getMesh() const { return _cachedMesh; }

    bool hasSource() const
    {
        return _primitiveGeometry != EPrimitiveGeometry::None ||
               !_sourceModelPath.empty();
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

    /// Point the slot at a mesh inside a Model without a handle to it yet, so
    /// callers never have to load the asset themselves.
    void setModelPath(const std::string& modelPath, uint32_t meshIndex = 0)
    {
        _primitiveGeometry = EPrimitiveGeometry::None;
        _sourceModelPath   = modelPath;
        _meshIndex         = meshIndex;
        invalidate();
    }

    void setFromModel(const std::string& modelPath, uint32_t meshIndex, Mesh* mesh)
    {
        _primitiveGeometry = EPrimitiveGeometry::None;
        _sourceModelPath   = modelPath;
        _meshIndex         = meshIndex;
        _cachedMesh        = mesh;
        _bResolved         = (mesh != nullptr);
        // resolve() rebinds the handle (and with it the fill subscription)
        // on the processor's next pump; a stale handle from a previous path
        // must not survive the re-point.
        _modelHandle.reset();
    }
};

} // namespace ya
