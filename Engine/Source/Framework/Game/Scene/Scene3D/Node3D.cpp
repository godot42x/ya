#include "Node3D.h"

#include "Framework/Game/Gameplay/ECS/Component/TransformComponent.h"
#include "Framework/Game/Gameplay/ECS/Entity.h"

namespace ya
{

// ============================================================================
// Node3D Implementation (With ECS/Transform)
// ============================================================================

void Node3D::onNameChanged(const std::string &name)
{
    // Keep the ECS entity name in sync with the scene-tree node name (the
    // base Node class is ECS-free and delegates here).
    if (_entity) {
        _entity->name = name;
    }
}

TransformComponent *Node3D::getTransformComponent()
{
    if (_entity) {
        return _entity->getComponent<TransformComponent>();
    }
    return nullptr;
}

void Node3D::setupTransformCallback()
{
    if (auto *tc = getTransformComponent()) {
        // Capture 'this' to propagate dirty to children when transform changes
        tc->setOnChildrenDirtyCallback([this]() {
            // Propagate dirty to all children (not including self)
            for (auto *child : getChildren()) {
                if (auto *child3D = dynamic_cast<Node3D *>(child)) {
                    child3D->propagateWorldDirty();
                }
                else {
                    child->onHierarchyDirty();
                }
            }
        });
    }
}

const TransformComponent *Node3D::getTransformComponent() const
{
    if (_entity) {
        return _entity->getComponent<TransformComponent>();
    }
    return nullptr;
}

void Node3D::updateCachedParentTC()
{
    _cachedParentTC = nullptr;

    // Find parent's TransformComponent
    // Walk up the hierarchy to find the first Node3D parent with a TC
    Node *parent = getParent();
    while (parent) {
        if (auto *parent3D = dynamic_cast<Node3D *>(parent)) {
            if (auto *tc = parent3D->getTransformComponent()) {
                _cachedParentTC = tc;
                break;
            }
        }
        parent = parent->getParent();
    }

    // Update the TransformComponent's cached parent pointer and callback
    if (auto *tc = getTransformComponent()) {
        tc->setCachedParentTC(_cachedParentTC);
    }

    // Setup the callback for dirty propagation
    setupTransformCallback();
}

void Node3D::onParentChanged()
{
    updateCachedParentTC();

    // Also update all children's cached parent TC
    // (in case they were looking through this node to a grandparent)
    for (auto *child : getChildren()) {
        if (auto *child3D = dynamic_cast<Node3D *>(child)) {
            child3D->updateCachedParentTC();
        }
    }
}

void Node3D::onHierarchyDirty()
{
    propagateWorldDirty();
}

void Node3D::propagateWorldDirty()
{
    // Mark this node's transform as dirty
    if (auto *tc = getTransformComponent()) {
        tc->markWorldDirty();
    }

    // Recursively mark all descendants as dirty
    for (auto *child : getChildren()) {
        if (auto *child3D = dynamic_cast<Node3D *>(child)) {
            child3D->propagateWorldDirty();
        }
        else {
            // For non-Node3D children, just propagate through hierarchy
            child->onHierarchyDirty();
        }
    }
}

} // namespace ya
