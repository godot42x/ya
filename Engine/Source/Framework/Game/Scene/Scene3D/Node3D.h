#pragma once

#include "Core/Base.h"
#include "Hierarchy/Node.h"

namespace ya
{

struct Entity;
struct TransformComponent;

/**
 * @brief Node3D - Node with Entity and Transform support
 *
 * Design Philosophy:
 * - Extends Node with ECS integration
 * - Manages TransformComponent and cached parent TC pointer
 * - Propagates transform dirty flags through hierarchy
 */
struct YA_SCENE_3D_API Node3D : public Node
{

    YA_REFLECT_BEGIN(Node3D)
    YA_REFLECT_END()

  private:
    TransformComponent *_cachedParentTC = nullptr; // Cached for fast world matrix calculation

  public:
    explicit Node3D(Entity *entity, std::string name) : Node(std::move(name), entity) {}

    // === Entity Access ===

    // === TransformComponent Access ===
    [[nodiscard]] TransformComponent       *getTransformComponent();
    [[nodiscard]] const TransformComponent *getTransformComponent() const;

    // === Cached Parent TC (for fast world matrix calculation) ===
    [[nodiscard]] TransformComponent *getCachedParentTC() const { return _cachedParentTC; }

    /**
     * @brief Recursively mark this node and all descendants as world dirty
     * @note Called by TransformComponent when local transform changes
     */
    void propagateWorldDirty();

  protected:
    // === Virtual Hook Implementations ===
    void onNameChanged(const std::string &name) override;
    void onParentChanged() override;
    void onHierarchyDirty() override;

  private:
    /**
     * @brief Update cached parent TC pointer based on current parent
     */
    void updateCachedParentTC();

    /**
     * @brief Setup the transform dirty callback on TransformComponent
     */
    void setupTransformCallback();
};

} // namespace ya
