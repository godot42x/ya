

/**
 * @brief Lit Material Component with Reflection Support
 *
 * Serializable material component that stores:
 * - Model reference (path, auto-loaded)
 * - Material parameters (ambient, diffuse, specular, shininess)
 * - Texture slots (diffuse, specular textures)
 *
 * Runtime data (not serialized):
 * - Cached material pointer
 * - Cached mesh pointers
 */
#pragma once

#include "MaterialComponent.h"
#include "Render/Material/LitMaterial.h"

namespace ya
{



/**
 * @brief LitMaterialComponent - Serializable lit material component
 *
 * JSON serialization format:
 * @code
 * {
 *   "LitMaterialComponent": {
 *     "_modelRef": { "_path": "Content/Models/cube.fbx" },
 *     "_params": {
 *       "ambient": [0.1, 0.1, 0.1],
 *       "diffuse": [1.0, 1.0, 1.0],
 *       "specular": [1.0, 1.0, 1.0],
 *       "shininess": 32.0
 *     },
 *     "_diffuseSlot": { "textureRef": { "_path": "Content/Textures/diffuse.png" }, ... },
 *     "_specularSlot": { "textureRef": { "_path": "Content/Textures/specular.png" }, ... }
 *   }
 * }
 * @endcode
 */
struct LitMaterialComponent : public MaterialComponent<LitMaterial>
{
    YA_REFLECT_BEGIN(LitMaterialComponent, MaterialComponent<LitMaterial>)
    YA_REFLECT_FIELD(_primitiveGeometry) // Built-in primitive type (serialized)
    YA_REFLECT_FIELD(_modelRef)          // External model path (serialized)
    YA_REFLECT_FIELD(_params)
    YA_REFLECT_FIELD(_diffuseSlot)
    YA_REFLECT_FIELD(_specularSlot)
    YA_REFLECT_END()

    // ========================================
    // Serializable Data
    // ========================================

    EPrimitiveGeometry    _primitiveGeometry = EPrimitiveGeometry::None; // Built-in primitive type
    ModelRef              _modelRef;                                     // Model path (serialized, auto-loaded)
    LitMaterial::ParamUBO _params;                                       // Material parameters (serialized)
    TextureSlot           _diffuseSlot;                                  // Diffuse texture slot (serialized)
    TextureSlot           _specularSlot;                                 // Specular texture slot (serialized)

    // ========================================
    // Runtime Cache (Not Serialized)
    // ========================================

    LitMaterial *_runtimeMaterial = nullptr; // Cached runtime material instance
    stdptr<Mesh> _primitiveMesh;             // Owned primitive mesh (for EPrimitiveType)
    bool         _bResolved = false;         // Whether resources have been resolved

  public:


    /**
     * @brief Resolve all asset references (called after deserialization)
     * Loads model and textures from paths via AssetManager
     * @return true if all resources resolved successfully
     */
    bool resolve();

    /**
     * @brief Check if component has been resolved
     */
    bool isResolved() const { return _bResolved; }

    /**
     * @brief Force re-resolve (e.g., when paths change at runtime)
     */
    void invalidate() { _bResolved = false; }

    // ========================================
    // Runtime Material Access
    // ========================================

    /**
     * @brief Get runtime material (may be null if not resolved)
     */
    LitMaterial *getRuntimeMaterial() const { return _runtimeMaterial; }

    /**
     * @brief Sync serializable params to runtime material
     * Call this after modifying _params to update the GPU data
     */
    void syncParamsToRuntime();

    /**
     * @brief Sync serializable texture slots to runtime material
     * Call this after modifying texture slots to update GPU bindings
     */
    void syncTexturesToRuntime(Sampler *defaultSampler);

    // ========================================
    // Convenience Setters (update both serializable + runtime)
    // ========================================

    void setDiffuseColor(const glm::vec3 &color)
    {
        _params.diffuse = color;
        if (_runtimeMaterial) {
            _runtimeMaterial->setDiffuseParam(color);
        }
    }

    void setSpecularColor(const glm::vec3 &color)
    {
        _params.specular = color;
        if (_runtimeMaterial) {
            _runtimeMaterial->setSpecularParam(color);
        }
    }

    void setShininess(float shininess)
    {
        _params.shininess = shininess;
        if (_runtimeMaterial) {
            _runtimeMaterial->setShininess(shininess);
        }
    }

    void setAmbientColor(const glm::vec3 &ambient)
    {
        _params.ambient = ambient;
        if (_runtimeMaterial) {
            _runtimeMaterial->getParamsMut().ambient = ambient;
            _runtimeMaterial->setParamDirty();
        }
    }
};

} // namespace ya