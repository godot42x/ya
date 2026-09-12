#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct Entity;
struct Scene;
struct SelectionModel;

/// Stable retained hierarchy row id for scene entities (`e:<uuid>`).
[[nodiscard]] std::string editorHierarchyEntityIdKey(uint64_t uuid);

/// Parse `editorHierarchyEntityIdKey` ids; returns false for `ui:` and other keys.
[[nodiscard]] bool parseEditorHierarchyEntityIdKey(const std::string& id, uint64_t& outUuid);

/// Resolve inspector/hierarchy entities from the owner session's SelectionModel
/// when it has entity ids; otherwise fall back to Layer document selection.
[[nodiscard]] std::vector<Entity*> editorSelectionEntities(EditorLayer& layer,
                                                           const SelectionModel* selection);

/// Reorder a scene entity using retained `UITreeView` drop modes:
/// 0 = before, 1 = into, 2 = after. Returns the moved entity on success.
[[nodiscard]] Entity* moveEditorHierarchyEntity(Scene& scene,
                                                  const std::string& fromId,
                                                  const std::string& toId,
                                                  int dropMode);

} // namespace ya
