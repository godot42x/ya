#pragma once

#include "Core/Reflection/PropertyAccessor.h"

#include <string>
#include <string_view>
#include <vector>

namespace ya
{

/// Editor-side projection of reflected properties into graph leaves.
/// Core Reflection owns slot/address access; the builder owns editor paths,
/// nested expansion and multi-instance leaf materialization.
class PropertyGraphBuilder final
{
  public:
    enum class ELeafRole
    {
        Value,
        Sequence,
        Map,
    };

    struct FLeaf
    {
        type_index_t                  ownerType = 0;
        reflection::FPropertySlot     slot;
        std::string                   path;
        std::vector<void*>            ownerInstances;
        std::vector<reflection::FPropertySlot> ownerPath;
        std::vector<void*>            rootInstances;
        ELeafRole                     role = ELeafRole::Value;
    };

    static void collectLeaves(type_index_t rootType,
                              const std::vector<void*>& roots,
                              std::vector<FLeaf>& out);
};

} // namespace ya
