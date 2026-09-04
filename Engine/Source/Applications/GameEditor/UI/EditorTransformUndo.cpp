#include "GameEditor/UI/EditorTransformUndo.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "GUI/Binding/UndoStack.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Systems/TransformSystem.h"

namespace ya
{
namespace
{
bool equalSnapshot(const FEditorTransformSnapshot& lhs, const FEditorTransformSnapshot& rhs)
{
    return lhs.entityUUID == rhs.entityUUID && lhs.world == rhs.world;
}

bool equalSnapshots(const std::vector<FEditorTransformSnapshot>& lhs,
                    const std::vector<FEditorTransformSnapshot>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t index = 0; index < lhs.size(); ++index) {
        if (!equalSnapshot(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

void restore(Scene* scene, const std::vector<FEditorTransformSnapshot>& snapshots)
{
    if (!scene) {
        return;
    }
    for (const auto& snapshot : snapshots) {
        Entity* entity = scene->getEntityByUUID(snapshot.entityUUID);
        if (!entity || !entity->isValid() || !entity->hasComponent<TransformComponent>()) {
            continue;
        }
        TransformSystem::setWorldTransform(entity->getComponent<TransformComponent>(), snapshot.world);
    }
}
}

std::vector<FEditorTransformSnapshot> captureEditorTransformSelection(const std::vector<Entity*>& selections)
{
    std::vector<FEditorTransformSnapshot> snapshots;
    snapshots.reserve(selections.size());
    for (Entity* entity : selections) {
        if (!entity || !entity->isValid() || !entity->hasComponent<TransformComponent>()) {
            continue;
        }
        const auto* id = entity->getComponent<IDComponent>();
        if (!id) {
            continue;
        }
        snapshots.push_back({id->_id.value, entity->getComponent<TransformComponent>()->getTransform()});
    }
    return snapshots;
}

bool pushEditorTransformUndo(UndoStack& undo,
                             Scene* scene,
                             std::vector<FEditorTransformSnapshot> before,
                             std::vector<FEditorTransformSnapshot> after)
{
    if (!scene || before.empty() || before.size() != after.size() || equalSnapshots(before, after)) {
        return false;
    }
    return undo.push({
        .label = "Transform gizmo",
        .undo = [scene, before = std::move(before)]() { restore(scene, before); },
        .redo = [scene, after = std::move(after)]() { restore(scene, after); },
    });
}

} // namespace ya
