#include "TransformSystem.h"

#include "Core/App/App.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "Scene/Node.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

#include <glm/gtx/matrix_decompose.hpp>


namespace ya
{

// ============================================================================
// Public Matrix Computation API
// ============================================================================

void TransformSystem::computeLocalMatrix(TransformComponent *tc)
{
    if (!tc || !tc->isLocalDirty()) {
        return;
    }

    // Compute: localMatrix = T * R * S
    glm::mat4 rotation = glm::mat4_cast(glm::quat(glm::radians(tc->_rotation)));
    tc->_localMatrix =
        glm::translate(glm::mat4(1.0f), tc->_position) *
        rotation *
        glm::scale(glm::mat4(1.0f), tc->_scale);

    tc->clearLocalDirty();
}

void TransformSystem::computeWorldMatrix(TransformComponent *tc)
{
    if (!tc || !tc->isWorldDirty()) {
        return;
    }

    // Ensure local matrix is up-to-date first
    computeLocalMatrix(tc);

    // Compute: worldMatrix = parentWorld * localMatrix
    if (tc->_cachedParentTC) {
        // Recursively ensure parent's world matrix is up-to-date
        computeWorldMatrix(tc->_cachedParentTC);
        tc->_worldMatrix = tc->_cachedParentTC->_worldMatrix * tc->_localMatrix;
    }
    else {
        // No parent: world = local
        tc->_worldMatrix = tc->_localMatrix;
    }

    tc->clearWorldDirty();
}

void TransformSystem::setWorldTransform(TransformComponent *tc, const glm::mat4 &newWorldMatrix)
{
    if (!tc) {
        return;
    }

    // 1. Get parent world matrix
    glm::mat4 parentWorldMatrix = glm::mat4(1.0f);
    if (tc->_cachedParentTC) {
        // Ensure parent's world matrix is up-to-date
        computeWorldMatrix(tc->_cachedParentTC);
        parentWorldMatrix = tc->_cachedParentTC->_worldMatrix;
    }

    // 2. Compute local matrix: localMatrix = parentWorld^(-1) * worldMatrix
    glm::mat4 newLocalMatrix = glm::inverse(parentWorldMatrix) * newWorldMatrix;

    // 3. Decompose local matrix to position/rotation/scale
    glm::vec3 scale;
    glm::quat rotation;
    glm::vec3 translation;
    glm::vec3 skew;
    glm::vec4 perspective;

    if (glm::decompose(newLocalMatrix, scale, rotation, translation, skew, perspective)) {
        // Successfully decomposed - update local transform data
        tc->_position = translation;
        tc->_rotation = glm::degrees(glm::eulerAngles(rotation));
        tc->_scale    = scale;

        // Recompute local matrix from decomposed values (for consistency)
        glm::mat4 rotationMat = glm::mat4_cast(glm::quat(glm::radians(tc->_rotation)));
        tc->_localMatrix =
            glm::translate(glm::mat4(1.0f), tc->_position) *
            rotationMat *
            glm::scale(glm::mat4(1.0f), tc->_scale);

        // Recompute world matrix
        tc->_worldMatrix = parentWorldMatrix * tc->_localMatrix;

        // Mark as clean
        tc->clearLocalDirty();
        tc->clearWorldDirty();

        // Propagate dirty to children
        tc->notifyChildrenDirty();
    }
    else {
        // Decomposition failed - just set world matrix
        YA_CORE_WARN("TransformSystem::setWorldTransform: Failed to decompose matrix");
        tc->_worldMatrix = newWorldMatrix;
        tc->clearWorldDirty();
    }
}

void TransformSystem::setWorldPosition(TransformComponent *tc, const glm::vec3 &worldPos)
{
    if (!tc) {
        return;
    }

    // Ensure world matrix is up-to-date
    computeWorldMatrix(tc);

    // Modify world matrix position
    glm::mat4 worldMatrix = tc->_worldMatrix;
    worldMatrix[3]        = glm::vec4(worldPos, 1.0f);

    // Apply the modified world matrix
    setWorldTransform(tc, worldMatrix);
}

// ============================================================================
// Frame Update Logic
// ============================================================================

void TransformSystem::onUpdate(float dt)
{
    // YA_PROFILE_FUNCTION_LOG();
    auto *app = App::get();
    if (!app) {
        return;
    }

    auto *sceneManager = app->getSceneManager();
    if (!sceneManager) {
        return;
    }

    auto *scene = sceneManager->getActiveScene();
    if (!scene) {
        return;
    }

    // Step 1: Update Node-based hierarchy (if root node exists)
    if (scene->_rootNode) {
        updateNodeTree(scene->_rootNode.get(), nullptr);
    }

    // Step 2: Update flat entities (entities without Node hierarchy)
    updateFlatTransforms(scene);
}

void TransformSystem::updateNodeTree(Node *node, const glm::mat4 *parentWorldMatrix)
{
    if (!node) {
        return;
    }


    auto *node3D = dynamic_cast<Node3D *>(node);

    // Not a Node3D or no entity - just traverse children
    if (!node3D || !node3D->getEntity()) {
        for (auto *child : node->getChildren()) {
            updateNodeTree(child, parentWorldMatrix);
        }
        return;
    }
    // if (!node3D->getEntity()->hasComponent<TransformComponent>()) {
    //     return;
    // }

    auto *tc = node3D->getTransformComponent();

    bool needsUpdate = tc && (tc->isLocalDirty() || tc->isWorldDirty());

    // Update self if dirty
    if (needsUpdate) {
        updateNode3D(node3D, parentWorldMatrix);
    }

    // Determine parent world matrix for children
    // const glm::mat4 *childParentWorldMatrix = parentWorldMatrix;
    // if (tc) {
    //     // Use this node's world matrix as parent for children
    //     childParentWorldMatrix = &tc->_worldMatrix;
    // }
    const glm::mat4 *childParentWorldMatrix = &tc->_worldMatrix;

    // Recursively update children
    for (auto *child : node->getChildren()) {
        updateNodeTree(child, childParentWorldMatrix);
    }
}

void TransformSystem::updateNode3D(Node3D *node, const glm::mat4 *parentWorldMatrix)
{
    if (!node) {
        return;
    }


    auto *tc = node->getTransformComponent();
    if (!tc) {
        return;
    }

    // Compute local matrix if dirty
    if (tc->isLocalDirty()) {
        Self::computeLocalMatrix(tc);
    }

    // Compute world matrix if dirty
    if (tc->isWorldDirty()) {
        if (parentWorldMatrix) {
            tc->_worldMatrix = (*parentWorldMatrix) * tc->_localMatrix;
        }
        else {
            // Root node: world = local
            tc->_worldMatrix = tc->_localMatrix;
        }
        tc->clearWorldDirty();
    }
}

void TransformSystem::updateFlatTransforms(Scene *scene)
{
    auto &registry = scene->getRegistry();

    auto view = registry.view<TransformComponent>();
    for (auto entityHandle : view) {
        // Skip if this entity is managed by a Node
        if (scene->getNodeByEntity(entityHandle) != nullptr) {
            continue;
        }

        auto &tc = view.get<TransformComponent>(entityHandle);

        // Compute local matrix if dirty
        if (tc.isLocalDirty()) {
            computeLocalMatrix(&tc);
        }

        // Compute world matrix if dirty (no parent: world = local)
        if (tc.isWorldDirty()) {
            tc._worldMatrix = tc._localMatrix;
            tc.clearWorldDirty();
        }
    }
}

} // namespace ya
