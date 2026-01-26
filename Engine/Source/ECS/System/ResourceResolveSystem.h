
#pragma once

#include "Core/Base.h"
#include "Core/System/System.h"

namespace ya
{

// Forward declarations
struct ModelComponent;
struct MeshComponent;
struct PhongMaterialComponent;
struct Scene;
struct Entity;
struct Model;
struct EmbeddedMaterial;
struct Node;
class PhongMaterial;

/**
 * @brief ResourceResolveSystem - Unified resource loading system
 *
 * Handles all resource resolution in one place:
 * - ModelComponent: Loads Model, creates child entities for each mesh
 * - MeshComponent: Loads primitive geometry or mesh from Model
 * - IMaterialComponent: Loads textures and creates runtime materials
 *
 * Call order during frame:
 * 1. ResourceResolveSystem::onUpdate() - Load resources, create child entities
 * 2. MaterialSystem::onUpdateByRenderTarget() - Prepare descriptors
 * 3. MaterialSystem::onRender() - Render
 *
 * Multi-mesh Model handling (Strategy 2 - Split to Entities):
 * When a ModelComponent is resolved:
 * 1. Load the Model asset
 * 2. For each mesh in Model:
 *    a. Create a child Entity
 *    b. Add MeshComponent (single mesh reference)
 *    c. Add PhongMaterialComponent (from embedded material or default)
 *    d. Copy TransformComponent from parent
 * 3. Store child entity IDs in ModelComponent for cleanup
 */
struct ResourceResolveSystem : public ISystem
{
    void init() {}

    /**
     * @brief Resolve all pending resources
     * Iterates through components and calls resolve() on unresolved ones
     */
    void onUpdate(float dt) override;

    void destroy() {}

  private:
    /**
     * @brief Resolve a ModelComponent
     * Loads the Model and creates child entities for each mesh
     */
    void resolveModelComponent(Scene *scene, Entity *entity, ModelComponent &modelComp);

    /**
     * @brief Create a child node for a single mesh from Model
     * @param scene The scene to create node in
     * @param parentEntity The parent entity with ModelComponent
     * @param model The loaded Model
     * @param meshIndex Index of the mesh in Model
     * @param modelComp The ModelComponent (contains cached shared materials)
     * @return Created Node pointer
     */
    Node *createMeshNode(
        Scene          *scene,
        Entity         *parentEntity,
        Model          *model,
        uint32_t        meshIndex,
        ModelComponent &modelComp);

    /**
     * @brief Initialize a shared PhongMaterial from embedded material data
     * @param material The runtime material to initialize
     * @param embeddedMat The embedded material data
     * @param modelDirectory The model's directory for resolving texture paths
     */
    void initSharedMaterial(
        PhongMaterial            *material,
        const EmbeddedMaterial *embeddedMat,
        const std::string      &modelDirectory);

    /**
     * @brief Initialize PhongMaterialComponent from embedded material data
     */
    void initMaterialFromEmbedded(
        PhongMaterialComponent   &matComp,
        const EmbeddedMaterial *embeddedMat,
        const std::string      &modelDirectory);

    /**
     * @brief Clean up child nodes when Model changes or component is removed
     */
    void cleanupChildEntities(Scene *scene, ModelComponent &modelComp);
};

} // namespace ya