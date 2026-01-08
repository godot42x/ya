
#pragma once

#include "ECS/Component.h"
#include "Render/Material/Material.h"
#include "Render/Model.h"
namespace ya
{


struct StaticMeshComponent : public IComponent
{
    YA_REFLECT_BEGIN(StaticMeshComponent)
    YA_REFLECT_END()

  public:
    // ========================================
    // Core Data
    // ========================================

    stdptr<Model> _model = nullptr; // Geometry resource
    std::string   _assetPath;       // Serialized model path

    // Mesh-Material binding: _materials[i] → _model->getMesh(i)
    std::vector<Material *>  _materials;     // Runtime material instances (not serialized)
    std::vector<std::string> _materialPaths; // Serialized material paths

    // ========================================
    // Convenience API
    // ========================================

    /**
     * @brief Set model resource
     */
    void setModel(stdptr<Model> model) { _model = model; }

    /**
     * @brief Get model
     */
    stdptr<Model> getModel() const { return _model; }

    /**
     * @brief Set material for specific mesh index
     * @param meshIndex Index of mesh in model (0-based)
     * @param material Material instance
     */
    void setMaterial(size_t meshIndex, Material *material)
    {
        if (meshIndex >= _materials.size()) {
            _materials.resize(meshIndex + 1, nullptr);
        }
        _materials[meshIndex] = material;
    }

    /**
     * @brief Get material for specific mesh index
     * @return Material pointer or nullptr if not set
     */
    Material *getMaterial(size_t meshIndex) const
    {
        return meshIndex < _materials.size() ? _materials[meshIndex] : nullptr;
    }

    /**
     * @brief Add material (append to end)
     * Compatible with old MaterialComponent API
     */
    void addMaterial(Material *material)
    {
        _materials.push_back(material);
    }

    /**
     * @brief Set all meshes to use the same material
     * Convenience method for simple objects with single material
     */
    void setAllMaterials(Material *material)
    {
        if (!_model) return;
        _materials.resize(_model->getMeshCount(), material);
        std::fill(_materials.begin(), _materials.end(), material);
    }

    /**
     * @brief Apply operation to all materials
     * @param func Lambda: [](Material* mat) { ... }
     *
     * Example:
     * @code
     * smc->forEachMaterial([](Material* mat) {
     *     if (auto* lit = dynamic_cast<LitMaterial*>(mat)) {
     *         lit->setObjectColor(glm::vec3(1, 0, 0));
     *     }
     * });
     * @endcode
     */
    template <typename Func>
    void forEachMaterial(Func &&func)
    {
        for (auto *mat : _materials) {
            if (mat) {
                func(mat);
            }
        }
    }

    /**
     * @brief Get material count
     */
    size_t getMaterialCount() const { return _materials.size(); }

    /**
     * @brief Check if component has valid model and materials
     */
    bool isValid() const
    {
        return _model != nullptr && !_materials.empty();
    }

    StaticMeshComponent()                            = default;
    StaticMeshComponent(const StaticMeshComponent &) = default;
};
} // namespace ya