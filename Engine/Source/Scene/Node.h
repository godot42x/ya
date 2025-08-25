
#pragma once

#include "Core/Base.h"
#include "Core/UUID.h"


namespace ya
{


// TODO: what node? add prefix or wrap in namespace
struct Node
{
    UUID                _id;
    std::string         _name;
    Node               *_parent = nullptr;
    std::vector<Node *> _children;


    [[nodiscard]] const UUID &getId() const { return _id; }
    void                      setId(const UUID &id) { _id = id; }

    [[nodiscard]] const std::string &getName() const { return _name; }
    void                             setName(const std::string &name) { _name = name; }

    [[nodiscard]] Node *getParent() const { return _parent; }
    [[nodiscard]] bool  hasParent() const { return _parent != nullptr; }
    void                setParent(Node *parent)
    {
        parent->addChild(this);
        _parent = parent;
    }

    [[nodiscard]] const std::vector<Node *> &getChildren() const { return _children; }
    [[nodiscard]] bool                       hasChildren() const { return !_children.empty(); }

    void addChild(Node *node)
    {
        if (node->hasParent()) {
            node->getParent()->removeChild(node);
        }
        // child->setParent(this);
        node->_parent = this;
        _children.push_back(node);
    }
    void removeChild(Node *child)
    {
        if (_children.empty()) {
            YA_CORE_WARN("Node::removeChild: no children to remove from");
            return;
        }
        if (!child->hasParent() || child->getParent() != this) {
            YA_CORE_WARN("Node::removeChild: child does not belong to this parent");
            return;
        }
        child->setParent(nullptr);
        _children.erase(std::remove(_children.begin(), _children.end(), child), _children.end());
    }
    void clearChildren()
    {
        _children.clear();
    }
};

} // namespace ya