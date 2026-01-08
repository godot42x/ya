#pragma once

#include "Core/Base.h"
#include "ECS/Component.h"
#include "Render/Mesh.h"


namespace ya
{

/**
 * @brief Material Component - ECS component for dynamic material management
 * 
 * Design Philosophy:
 * - Material resources are managed by MaterialFactory (shared across entities)
 * - MaterialComponent holds per-entity material instances and mesh bindings
 * - Supports runtime parameter modification with dirty tracking
 * 
 * Usage:
 * @code
 * auto* lmc = entity->getComponent<LitMaterialComponent>();
 * lmc->getMaterial(0)->setObjectColor(glm::vec3(1, 0, 0));  // Dynamic color change
 * @endcode
 */
template <typename Material>
struct MaterialComponent : public IComponent
{
    std::vector<Mesh *>                                   _meshes;
    std::unordered_map<Material *, std::vector<uint32_t>> _material2meshes;

  public:
    MaterialComponent() : IComponent() {}

    /**
     * @brief Add mesh with material binding
     * @param mesh Mesh to render
     * @param material Material instance (can be shared or unique per entity)
     */
    void addMesh(Mesh *mesh, Material *material = nullptr)
    {
        if (!mesh) {
            return;
        }
        uint32_t index = _meshes.size();
        _meshes.push_back(mesh);
        if (auto it = _material2meshes.find(material); it != _material2meshes.end()) {
            it->second.push_back(index);
        }
        else {
            _material2meshes.insert({material, {index}});
        }
    }

    // ========================================
    // Query API
    // ========================================
    
    /// Get material-to-mesh-indices mapping
    auto getMaterial2MeshIDs() { return _material2meshes; }
    
    /// Get total unique material count
    uint32_t getMaterialCount() const { return static_cast<uint32_t>(_material2meshes.size()); }
    
    /// Get mesh by index
    Mesh *getMesh(uint32_t index)
    {
        if (index >= _meshes.size()) {
            return nullptr;
        }
        return _meshes[index];
    }
    
    /// Get all meshes
    const std::vector<Mesh*>& getMeshes() const { return _meshes; }
    
    // ========================================
    // Dynamic Material API (Convenience)
    // ========================================
    
    /**
     * @brief Get first material (convenience for single-material components)
     * Useful for simple entities with only one material
     */
    Material* getFirstMaterial()
    {
        if (_material2meshes.empty()) return nullptr;
        return _material2meshes.begin()->first;
    }
    
    /**
     * @brief Apply operation to all materials
     * @param func Lambda: [](Material* mat) { ... }
     * 
     * Example:
     * @code
     * lmc->forEachMaterial([](LitMaterial* mat) {
     *     mat->setObjectColor(glm::vec3(1, 0, 0));
     * });
     * @endcode
     */
    template<typename Func>
    void forEachMaterial(Func&& func)
    {
        for (auto& [material, meshIndices] : _material2meshes) {
            if (material) {
                func(material);
            }
        }
    }
};

} // namespace ya