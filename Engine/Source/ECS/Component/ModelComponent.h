/**
 * @file ModelComponent.h
 * @brief Model Component - Entry point for loading 3D model assets
 *
 * Design:
 * - ModelComponent is the "source" component that references a Model asset
 * - When resolved, it triggers creation of child entities for each mesh
 * - Each child entity gets: MeshComponent + LitMaterialComponent
 * - This separates concerns: Model loading vs Mesh/Material rendering
 *
 * Data Flow:
 * 1. User sets ModelComponent._modelRef on an Entity
 * 2. ResourceResolveSystem loads the Model
 * 3. System creates child entities for each mesh in the Model
 * 4. Each child entity is self-contained (can be rendered independently)
 */
#pragma once

#include "Core/Base.h"
#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"

namespace ya
{

struct Node;

/**
 * @brief ModelComponent - Asset loading entry point for 3D models
 *
 * Serialization format:
 * @code
 * {
 *   "ModelComponent": {
 *     "_modelRef": { "_path": "Content/Models/character.fbx" },
 *     "_useEmbeddedMaterials": true,
 *     "_autoCreateChildEntities": true
 *   }
 * }
 * @endcode
 */
struct ModelComponent : public IComponent
{
    YA_REFLECT_BEGIN(ModelComponent)
    YA_REFLECT_FIELD(_modelRef)
    YA_REFLECT_FIELD(_useEmbeddedMaterials)
    YA_REFLECT_FIELD(_autoCreateChildEntities)
    YA_REFLECT_END()

    // ========================================
    // Serializable Data
    // ========================================

    ModelRef _modelRef; ///< Reference to the Model asset

    /**
     * @brief Whether to use materials embedded in the Model file
     * If false, child entities will get default materials
     */
    bool _useEmbeddedMaterials = true;

    /**
     * @brief Whether to automatically create child entities for each mesh
     * If false, the Model is loaded but no child entities are created
     * (useful for manual mesh extraction)
     */
    bool _autoCreateChildEntities = true;

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================

    bool _bResolved = false;

    /**
     * @brief Child nodes created from this Model (one per mesh)
     * Used for cleanup when ModelComponent is removed or Model changes
     * @note Stores Node pointers for hierarchical management
     */
    std::vector<Node *> _childNodes;

    // ========================================
    // Interface
    // ========================================

    /**
     * @brief Check if the Model has been resolved
     */
    bool isResolved() const { return _bResolved && _modelRef.isLoaded(); }

    /**
     * @brief Force re-resolve (will recreate child entities)
     */
    void invalidate()
    {
        _modelRef.invalidate();
        _bResolved = false;
        // Note: Child entity cleanup should be handled by ResourceResolveSystem
    }

    /**
     * @brief Check if this component has a valid Model reference
     */
    bool hasModelSource() const { return _modelRef.hasPath(); }

    /**
     * @brief Get the loaded Model (nullptr if not loaded)
     */
    Model *getModel() const { return _modelRef.get(); }

    /**
     * @brief Get the number of meshes in the loaded Model
     */
    size_t getMeshCount() const
    {
        return _modelRef.isLoaded() ? _modelRef.get()->getMeshCount() : 0;
    }

    /**
     * @brief Set the Model path and invalidate
     */
    void setModelPath(const std::string &path)
    {
        _modelRef = ModelRef(path);
        invalidate();
    }
};

} // namespace ya
