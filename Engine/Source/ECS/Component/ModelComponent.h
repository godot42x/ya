/**
 * @file ModelComponent.h
 * @brief Model Component - Entry point for loading 3D model assets
 *
 * Design:
 * - ModelComponent is the "source" component that references a Model asset
 * - When resolved, it triggers creation of child entities for each mesh
 * - Each child entity gets: MeshComponent + PhongMaterialComponent
 * - This separates concerns: Model loading vs Mesh/Material rendering
 *
 * Data Flow:
 * 1. User sets ModelComponent._modelRef on an Entity
 * 2. ModelInstantiationSystem loads the Model and creates child entities
 * 3. ResourceResolveSystem resolves runtime resources for those child components
 * 4. Each child entity is self-contained (can be rendered independently)
 */
#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"

#include <unordered_map>

namespace ya
{

struct Material;

struct Node;

enum class EModelMaterialType : uint8_t
{
    Phong = 0,
    PBR,
    Unlit,
    Custom,
};


struct ModelComponent : public IComponent
{
    YA_REFLECT_BEGIN(ModelComponent)
    YA_REFLECT_FIELD(_modelRef)
    YA_REFLECT_FIELD(_materialType)
    YA_REFLECT_FIELD(_customMaterialPath)
    YA_REFLECT_FIELD(_useEmbeddedMaterials)
    YA_REFLECT_FIELD(_autoCreateChildEntities)
    YA_REFLECT_END()

    // Defined in ModelComponent.cpp — needs full Material + MaterialFactory types.
    ~ModelComponent() override;

    // ========================================
    // Serializable Data
    // ========================================

    ModelRef _modelRef; ///< Reference to the Model asset
    EModelMaterialType _materialType = EModelMaterialType::Phong;
    std::string        _customMaterialPath;

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

    /**
     * @brief Cached runtime materials for this Model (one per material in model file)
     * Key: material index in Model's MaterialData array
    * Value: runtime Material pointer (managed by MaterialFactory)
     * This allows multiple meshes to share the same material instance
     */
    std::unordered_map<int32_t, Material*> _cachedMaterials;

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
        // Note: Child entity cleanup should be handled by ModelInstantiationSystem
    }

    /**
     * @brief Check if this component has a valid Model reference
     */
    bool hasModelSource() const { return _modelRef.hasPath(); }
    bool usesCustomMaterial() const { return _materialType == EModelMaterialType::Custom; }
    bool canUseSharedEmbeddedMaterialCache() const
    {
        return _useEmbeddedMaterials && (_materialType == EModelMaterialType::Phong ||
                                         _materialType == EModelMaterialType::PBR);
    }

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

YA_REFLECT_ENUM_BEGIN(ya::EModelMaterialType)
YA_REFLECT_ENUM_VALUE(Phong)
YA_REFLECT_ENUM_VALUE(PBR)
YA_REFLECT_ENUM_VALUE(Unlit)
YA_REFLECT_ENUM_VALUE(Custom)
YA_REFLECT_ENUM_END()
