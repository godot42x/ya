#pragma once

#include <cstdint>
#include <string>

namespace ya
{

struct Entity;
struct Scene;

/// Stable retained hierarchy row id for scene entities (`e:<uuid>`).
[[nodiscard]] std::string editorHierarchyEntityIdKey(uint64_t uuid);

/// Parse `editorHierarchyEntityIdKey` ids; returns false for `ui:` and other keys.
[[nodiscard]] bool parseEditorHierarchyEntityIdKey(const std::string& id, uint64_t& outUuid);

/// Reorder a scene entity using retained `UITreeView` drop modes:
/// 0 = before, 1 = into, 2 = after. Returns the moved entity on success.
[[nodiscard]] Entity* moveEditorHierarchyEntity(Scene& scene,
                                                  const std::string& fromId,
                                                  const std::string& toId,
                                                  int dropMode);

} // namespace ya
