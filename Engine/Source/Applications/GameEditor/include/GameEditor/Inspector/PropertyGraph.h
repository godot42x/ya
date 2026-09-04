#pragma once

#include "GameEditor/Inspector/PropertyHandle.h"

#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct PropertyNode
{
    std::string name;
    std::string displayName;
    std::string category;
    type_index_t valueType = 0;
    bool bEditable = false;
    bool bVisible = true;
    bool bInstanceEditable = false;
    bool bColor = false;
    PropertyHandle binding;
};

/// Ordered, metadata-aware editor field model. `project()` is the retained
/// inspector entry: reflection `build` plus registered projections.
/// It contains no widget or rendering concerns.
class PropertyGraph final
{
  public:
    static PropertyGraph build(type_index_t ownerType, std::vector<void*> instances);
    /// Reflection → editor field model: `build` then the registered projection.
    static PropertyGraph project(type_index_t ownerType, std::vector<void*> instances);
    [[nodiscard]] bool hasRetainedEditors() const;
    std::vector<PropertyNode>& getNodesMutable() { return _nodes; }
    [[nodiscard]] const std::vector<PropertyNode>& getNodes() const { return _nodes; }
    [[nodiscard]] type_index_t getOwnerType() const { return _ownerType; }
    [[nodiscard]] const std::vector<void*>& getRootInstances() const { return _rootInstances; }
    [[nodiscard]] PropertyNode* find(std::string_view name);
    [[nodiscard]] const PropertyNode* find(std::string_view name) const;

  private:
    type_index_t _ownerType = 0;
    std::vector<void*> _rootInstances;
    std::vector<PropertyNode> _nodes;
};

} // namespace ya
