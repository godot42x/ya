#pragma once

#include "GameEditor/Inspector/PropertyHandle.h"

#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct PropertyNode
{
    enum class Kind
    {
        Value,
        Sequence,
        Map,
    };

    std::string name;
    std::string displayName;
    std::string group;
    std::string category;
    type_index_t valueType = 0;
    Kind kind = Kind::Value;
    bool bEditable = false;
    bool bVisible = true;
    bool bInstanceEditable = false;
    bool bColor = false;
    PropertyHandle binding;
};

struct FPropertyLabel
{
    std::string group;
    std::string displayName;
};

/// Pretty inspector label for a reflected path. Nested paths keep the parent as
/// a group header and show only the leaf on the row (so "image.uvScale" is
/// "Image" + "Uv Scale", not "Image / Uv Scale" on every line).
[[nodiscard]] FPropertyLabel propertyLabelFromPath(std::string_view path);

/// Ordered, metadata-aware editor field model. `project()` is the retained
/// inspector entry: reflection `build`, default `IComponent::onEdit()` hooks for
/// registered ECS components, then type-specific projections.
/// It contains no widget or rendering concerns.
class PropertyGraph final
{
  public:
    static PropertyGraph build(type_index_t ownerType, std::vector<void*> instances);
    /// Reflection → editor field model: `build` then the registered projection.
    static PropertyGraph project(type_index_t ownerType, std::vector<void*> instances);
    /// Show every field but refuse writes. Used for entities the editor does
    /// not own (generated companions such as the camera body or a light icon):
    /// seeing what draws is useful, editing it would be a lie, because the
    /// reconciler rebuilds the companion from the host on the next change.
    void markAllReadOnly();
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
