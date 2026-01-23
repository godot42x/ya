

#pragma once

#include "Core/Base.h"
#include "Core/Math/Math.h"
#include "Core/UUID.h"


namespace ya
{
struct Entity;
struct TransformComponent;

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
struct Node : public disable_copy
{
    YA_REFLECT_BEGIN(Node)
    YA_REFLECT_END()

  protected:
    std::string         _name;
    Node               *_parent = nullptr;
    std::vector<Node *> _children;
    Entity             *_entity = nullptr;

    // TODO: reduce the dynamical cast from Node to Node3D!
    enum
    {
        Ty_Normal,
        Ty_2D,
        Ty_3D,
    };

  public:
    explicit Node(std::string name, Entity *entity) : _name(std::move(name)), _entity(entity) {}
    virtual ~Node() = default;

    // === Identity ===
    [[nodiscard]] const std::string &getName() const { return _name; }
    void                             setName(const std::string &name) { _name = name; }

    // === Hierarchy Management ===
    [[nodiscard]] Node *getParent() const { return _parent; }
    [[nodiscard]] bool  hasParent() const { return _parent != nullptr; }

    [[nodiscard]] const std::vector<Node *> &getChildren() const { return _children; }
    [[nodiscard]] bool                       hasChildren() const { return !_children.empty(); }
    [[nodiscard]] size_t                     getChildCount() const { return _children.size(); }
    [[nodiscard]] Node                      *getChild(size_t index) const
    {
        return index < _children.size() ? _children[index] : nullptr;
    }

    void setParent(Node *parent);
    void addChild(Node *child);
    void removeChild(Node *child);
    void removeFromParent();
    void clearChildren();


    // === Virtual Hooks for Derived Classes ===
    // These are public because they may be called on other Node instances during propagation

    [[nodiscard]] Entity       *getEntity() { return _entity; }
    [[nodiscard]] const Entity *getEntity() const { return _entity; }
    // void                        setEntity(Entity *entity) { _entity = entity; }

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


/**
 * @brief Node3D - Node with Entity and Transform support
 *
 * Design Philosophy:
 * - Extends Node with ECS integration
 * - Manages TransformComponent and cached parent TC pointer
 * - Propagates transform dirty flags through hierarchy
 */
struct Node3D : public Node
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