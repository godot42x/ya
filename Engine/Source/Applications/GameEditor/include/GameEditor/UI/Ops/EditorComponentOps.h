#pragma once

#include "Core/TypeIndex.h"

#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct Entity;

/// Components the author never adds or removes: identity of a Node3D, and
/// markers written by systems that rebuild the entity on load.
[[nodiscard]] bool isIdentityAuthoringComponent(std::string_view typeName);

/// True when the inspector may mutate the entity's component set.
/// Generated companions and model-instance children are rebuilt from a host,
/// so adding or removing a component there would be silently discarded.
[[nodiscard]] bool canMutateAuthoringComponents(const Entity& entity);

[[nodiscard]] bool canAddAuthoringComponent(const Entity& entity, type_index_t type);
[[nodiscard]] bool canRemoveAuthoringComponent(const Entity& entity, type_index_t type);

[[nodiscard]] bool canAddAuthoringComponent(const std::vector<Entity*>& entities, type_index_t type);
[[nodiscard]] bool canRemoveAuthoringComponent(const std::vector<Entity*>& entities, type_index_t type);

/// Registry types the Add Component menu lists, sorted by name. Identity and
/// empty stub types stay off this list; already-present types still appear so
/// the menu can disable them.
[[nodiscard]] std::vector<std::pair<std::string, type_index_t>> authoringComponentTypes();

[[nodiscard]] std::string authoringComponentTypeName(type_index_t type);

bool addAuthoringComponent(const std::vector<Entity*>& entities, type_index_t type);
bool removeAuthoringComponent(const std::vector<Entity*>& entities, type_index_t type);

} // namespace ya
