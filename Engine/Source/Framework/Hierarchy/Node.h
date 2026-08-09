

#pragma once

#include "Core/Base.h"
#include "Core/Math/Math.h"
#include "Core/UUID.h"


namespace ya
{
struct Entity;

/**
 * @brief Node - Pure hierarchical tree node (no ECS dependency)
 *
 * Design Philosophy:
 * - Pure hierarchy management (parent-child relationships)
 * - No direct dependency on ECS (Entity, Component)
 * - Provides virtual hooks for derived classes (Node3D) to handle transform
 *
 * Usage:
 * - Use Node for pure organizational hierarchy
 * - Use Node3D for nodes that need transform and ECS integration
 */
struct YA_HIERARCHY_API Node : public disable_copy
{
    YA_REFLECT_BEGIN(Node)
    YA_REFLECT_END()

  protected:
    std::string         _name;
    Node               *_parent = nullptr;
    std::vector<Node *> _children;
    Entity             *_entity = nullptr;

  public:
    explicit Node(std::string name, Entity *entity) : _name(std::move(name)), _entity(entity) {}
    virtual ~Node() = default;

    // === Identity ===
    [[nodiscard]] const std::string &getName() const;
    void                             setName(const std::string &name);

    // === Hierarchy Management ===
    static constexpr size_t NPOS = static_cast<size_t>(-1);

    [[nodiscard]] Node *getParent() const { return _parent; }
    [[nodiscard]] bool  hasParent() const { return _parent != nullptr; }

    [[nodiscard]] const std::vector<Node *> &getChildren() const { return _children; }
    [[nodiscard]] bool                       hasChildren() const { return !_children.empty(); }
    [[nodiscard]] size_t                     getChildCount() const { return _children.size(); }
    [[nodiscard]] Node                      *getChild(size_t index) const
    {
        return index < _children.size() ? _children[index] : nullptr;
    }
    [[nodiscard]] size_t getChildIndex(const Node *child) const;
    [[nodiscard]] bool   isAncestorOf(const Node *node) const;

    void setParent(Node *parent);
    void setParent(Node *parent, size_t childIndex);
    void addChild(Node *child);
    void insertChild(Node *child, size_t childIndex);
    void removeChild(Node *child);
    void removeFromParent();
    void clearChildren();


    // === Virtual Hooks for Derived Classes ===
    // These are public because they may be called on other Node instances during propagation

    [[nodiscard]] Entity       *getEntity() { return _entity; }
    [[nodiscard]] const Entity *getEntity() const { return _entity; }
    // void                        setEntity(Entity *entity) { _entity = entity; }

    /// Called after this node's display name changes. Derived classes that
    /// mirror the name into an external owner (e.g. Node3D syncing the ECS
    /// entity name) override this hook; the base class stays ECS-free.
    virtual void onNameChanged(const std::string &name) {}

    /**
     * @brief Called when this node's parent changes
     * @note Override in Node3D to update cached parent TransformComponent
     */
    virtual void onParentChanged() {}

    /**
     * @brief Called to propagate dirty flags down the hierarchy
     * @note Override in Node3D to mark TransformComponent as dirty
     */
    virtual void onHierarchyDirty() {}

  protected:
    /**
     * @brief Internal: Remove child from children list without notifying
     */
    void removeChildInternal(Node *child);
};
} // namespace ya
