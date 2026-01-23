#pragma once

#include "Core/Base.h"
#include "Core/System/System.h"

namespace ya
{

struct Scene;
struct Entity;
struct Node;
struct Node3D;
struct TransformComponent;

/**
 * @brief TransformSystem - Manages hierarchical transform updates
 *
 * Responsibilities:
 * - Update world matrices for all Nodes based on hierarchy
 * - Handle dirty flag propagation efficiently
 * - Support both Node-based (hierarchical) and flat Entity transforms
 *
 * Usage:
 * Call onUpdate() each frame before rendering to ensure all world matrices are up-to-date.
 *
 * Update Strategy:
 * 1. If using Node Tree: Traverse from root nodes, recursively update world transforms
 * 2. If using flat Entities: Simply copy local to world (no parent)
 */
struct TransformSystem : public ISystem
{
    using Self = TransformSystem;
    void init() {}

    /**
     * @brief Update all transforms in the scene
     *
     * For Node-based hierarchy:
     * - Find all root nodes (nodes without parent)
     * - Recursively update world transforms from roots down
     *
     * For flat entities (no Node):
     * - Update world = local for entities without hierarchy
     */
    void onUpdate(float dt) override;

    void destroy() {}

    // ========================================================================
    // Public Matrix Computation API (for immediate updates like Gizmo)
    // ========================================================================

    /**
     * @brief Compute local matrix from position/rotation/scale
     * @param tc TransformComponent to compute for
     *
     * Computes: localMatrix = T * R * S
     * Clears _localDirty flag after computation
     */
    static void computeLocalMatrix(TransformComponent *tc);

    /**
     * @brief Compute world matrix from parent and local matrix
     * @param tc TransformComponent to compute for
     *
     * Computes: worldMatrix = parentWorld * localMatrix
     * Uses _cachedParentTC for fast parent access
     * Clears _worldDirty flag after computation
     */
    static void computeWorldMatrix(TransformComponent *tc);

    /**
     * @brief Set world matrix and decompose to local transform
     * @param tc TransformComponent to update
     * @param worldMatrix New world matrix
     *
     * Used by Gizmo manipulation:
     * 1. Compute localMatrix = parentWorld^(-1) * worldMatrix
     * 2. Decompose localMatrix to position/rotation/scale
     * 3. Update cached matrices
     * 4. Propagate dirty to children
     */
    static void setWorldTransform(TransformComponent *tc, const glm::mat4 &worldMatrix);

    /**
     * @brief Set world position (convenience method)
     * @param tc TransformComponent to update
     * @param worldPos New world position
     */
    static void setWorldPosition(TransformComponent *tc, const glm::vec3 &worldPos);

  private:
    /**
     * @brief Recursively update world transforms for Node tree
     * @param node Root node to start updating from
     * @param parentWorldMatrix World matrix of the parent node (nullptr for root)
     *
     * Algorithm:
     * 1. Check if node is dirty
     * 2. If dirty, update node's world matrix using provided parent matrix
     * 3. Pass updated world matrix to children
     * 4. Recursively update children
     *
     * Optimization: Passes parent world matrix to avoid repeated getParent() calls
     */
    void updateNodeTree(Node *node, const glm::mat4 *parentWorldMatrix);

    /**
     * @brief Update single node's world transform
     * @param node Node to update
     * @param parentWorldMatrix World matrix of the parent node (nullptr for root)
     *
     * Computes: worldMatrix = parentWorldMatrix * localMatrix
     * For root nodes: worldMatrix = localMatrix
     *
     * Optimization: Uses provided parent world matrix instead of calling getParent()
     */
    void updateNode3D(Node3D *node, const glm::mat4 *parentWorldMatrix);
    /**
     * @brief Update transforms for entities without Node hierarchy
     * Simply sets world matrix = local matrix
     */
    void updateFlatTransforms(Scene *scene);
};

} // namespace ya
