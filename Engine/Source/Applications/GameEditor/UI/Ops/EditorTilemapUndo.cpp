#include "GameEditor/UI/Ops/EditorTilemapUndo.h"

#include "ECS/Component/2D/TilemapComponent.h"
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

