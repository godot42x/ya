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

/// True when the entity is a runtime-managed child of a model instance.
///
/// ModelComponent expands into one managed child per mesh; those entities are
/// rebuilt from the root's ModelRef on load and are skipped by the serializer.
/// They are addressable (Hierarchy rows, transform edits) but are not authored
/// scene objects, so authoring commands must treat them separately.
[[nodiscard]] bool editorIsInstanceChild(const Entity* entity);

/// Resolve the model-instance root that owns `entity`.
///
/// Walks the Node parent chain past managed children and returns the first
/// non-managed ancestor, so selection lands on the object the author placed in
/// the scene rather than on one of its generated meshes. An unmanaged entity
/// resolves to itself, as does an entity outside the Node hierarchy.
[[nodiscard]] Entity* editorResolveInstanceRoot(Scene& scene, Entity* entity);

/// True when there is a selection and every entity in it is a model instance
/// child. Authoring commands that cannot apply to instance children use this to
/// present themselves as unavailable, while the command bodies stay the single
/// source of truth for the actual refusal.
[[nodiscard]] bool editorSelectionIsAllInstanceChildren(EditorLayer& layer);

/// Number of generated mesh children currently under `instanceRoot`. Counts the
/// whole managed subtree, so it matches what the viewport draws for an instance.
[[nodiscard]] size_t editorCountInstanceChildren(Scene& scene, Entity* instanceRoot);

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
