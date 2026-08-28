#pragma once

#include "Core/TypeIndex.h"

#include <functional>
#include <unordered_map>

namespace ya
{

class PropertyGraph;
using PropertyProjection = std::function<void(PropertyGraph&)>;

class PropertyProjectionRegistry final
{
  public:
    static PropertyProjectionRegistry& instance();
    void registerProjection(type_index_t ownerType, PropertyProjection projection);
    void apply(type_index_t ownerType, PropertyGraph& graph) const;

  private:
    std::unordered_map<type_index_t, PropertyProjection> _projections;
};

void registerBuiltinPropertyProjections();

} // namespace ya
