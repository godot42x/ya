#include "Node.h"

#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"


namespace ya
{

// ============================================================================
// Node Implementation (Pure Hierarchy)
// ============================================================================

void Node::setParent(Node *parent)
{
    if (_parent == parent) {
        return;
    }

    // Remove from old parent
    if (_parent) {
        _parent->removeChildInternal(this);
    }

    _parent = parent;

    // Add to new parent
    if (parent) {
        parent->_children.push_back(this);
    }

    // Notify derived classes (Node3D will update cached parent TC)
    onParentChanged();

    // Propagate dirty through hierarchy
    onHierarchyDirty();
}

void Node::addChild(Node *child)
{
    if (!child || child == this) {
        return;
    }

    // Prevent circular reference
    Node *ancestor = _parent;
    while (ancestor) {
        if (ancestor == child) {
            YA_CORE_WARN("Node::addChild: Cannot add ancestor as child (circular reference)");
            return;
        }
        ancestor = ancestor->_parent;
    }

    child->setParent(this);
}

void Node::removeChild(Node *child)
{
    if (!child || child->_parent != this) {
        return;
    }

    child->_parent = nullptr;
    removeChildInternal(child);

    // Notify the removed child
    child->onParentChanged();
    child->onHierarchyDirty();
}

void Node::removeFromParent()
{
    if (_parent) {
        _parent->removeChild(this);
    }
}

void Node::clearChildren()
{
    for (auto *child : _children) {
        child->_parent = nullptr;
        child->onParentChanged();
        child->onHierarchyDirty();
    }
    _children.clear();
}

void Node::removeChildInternal(Node *child)
{
    auto it = std::find(_children.begin(), _children.end(), child);
    if (it != _children.end()) {
        _children.erase(it);
    }
}


// ============================================================================
// Node3D Implementation (With ECS/Transform)
// ============================================================================

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
