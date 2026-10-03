#include "GameEditor/UI/Ops/EditorTilemapUndo.h"

#include "Core/TypeIndex.h"
#include "Scene2D/TilemapComponent.h"
#include "ECS/Entity.h"
#include "GUI/Binding/UndoStack.h"
#include "Scene/Core/Scene.h"

namespace ya
{
namespace
{

void restoreLayer(Scene* scene, const FTileLayerSnapshot& snapshot)
{
    if (!scene) {
        return;
    }
    Entity* entity = scene->getEntityByUUID(snapshot.entityUUID);
    if (!entity || !entity->isValid() || !entity->hasComponent<TilemapComponent>()) {
        return;
    }
    TilemapComponent* map = entity->getComponent<TilemapComponent>();
    if (snapshot.layerIndex >= map->layers.size()) {
        return;
    }
    map->layers[snapshot.layerIndex].cells = snapshot.cells;
    // Same reaction the inspector's edit hook triggers: realign the restored
    // layer against width/height (a no-op when the snapshot already matches).
    map->onEdit();
    // The cells were written outside any typed setter: route the edit through
    // the scene funnel so derived-work processors hear it from the one signal
    // source.
    scene->notifyComponentEdited(entity->getHandle(), type_index_v<TilemapComponent>);
}

} // namespace

bool pushTileLayerUndo(UndoStack& undo, Scene* scene, FTileLayerSnapshot before, FTileLayerSnapshot after)
{
    if (!scene || before.entityUUID == 0 || before.entityUUID != after.entityUUID ||
        before.layerIndex != after.layerIndex || before.cells == after.cells) {
        return false;
    }
    return undo.push({
        .label = "Tile brush",
        .undo = [scene, before = std::move(before)]() { restoreLayer(scene, before); },
        .redo = [scene, after = std::move(after)]() { restoreLayer(scene, after); },
    });
}

} // namespace ya

