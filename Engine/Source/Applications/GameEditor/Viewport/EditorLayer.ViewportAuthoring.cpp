#include "GameEditor/EditorLayerInternal.h"

#include "GameEditor/UI/Ops/EditorHierarchyOps.h"
#include "GameEditor/Services/NodeCreateRegistry.h"
#include "Hierarchy/Node.h"
#include "Scene3D/Node3D.h"

namespace ya
{

bool EditorLayer::canViewportAuthor() const
{
    return _app && _app->isStopped() && hasProjectLoaded() && !isViewportMode2D();
}

void EditorLayer::cmdCreateEmptyNode(Node* parent)
{
    if (!canViewportAuthor()) {
        return;
    }
    Scene* scene = getEditableScene();
    if (!scene) {
        return;
    }
    Node* newNode = scene->createNode3D("New Node", parent);
    notifyHierarchyChanged();
    markSceneDirty();
    if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
        setSelectedEntity(node3D->getEntity());
    }
}

void EditorLayer::cmdCreateNodePreset(const std::string& presetDisplayName, Node* parent)
{
    if (!canViewportAuthor()) {
        return;
    }
    Scene* scene = getEditableScene();
    if (!scene) {
        return;
    }
    Node* node = editor::NodeCreateRegistry::get().createPreset(presetDisplayName, *scene, presetDisplayName, parent);
    notifyHierarchyChanged();
    markSceneDirty();
    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
        setSelectedEntity(node3D->getEntity());
    }
}

void EditorLayer::cmdDuplicateSelection()
{
    if (!canViewportAuthor()) {
        return;
    }
    Scene* scene = getEditableScene();
    if (!scene) {
        return;
    }

    std::vector<Entity*> duplicated;
    duplicated.reserve(_selections.size());
    for (Entity* entity : _selections) {
        if (!entity || !entity->isValid()) {
            continue;
        }
        Node* node = scene->getNodeByEntity(entity);
        if (!node) {
            continue;
        }
        // A managed child is rebuilt from its instance root on load, so a
        // duplicate of one would be deleted by the next instantiation. Clone
        // the root to get a second independent instance.
        if (editorIsInstanceChild(entity)) {
            YA_CORE_WARN("Cannot duplicate '{}': model instance children are rebuilt from '{}'. Duplicate that root instead.",
                         entity->getName(),
                         editorResolveInstanceRoot(*scene, entity)->getName());
            continue;
        }
        if (Node* newNode = scene->duplicateNode(node, node->getParent())) {
            if (Entity* newEntity = newNode->getEntity()) {
                duplicated.push_back(newEntity);
            }
        }
    }
    if (duplicated.empty()) {
        return;
    }

    notifyHierarchyChanged();
    markSceneDirty();
    YA_CORE_INFO("Duplicated {} entit{}", duplicated.size(), duplicated.size() > 1 ? "ies" : "y");
    facade().timerManager.delayCall(
        1,
        [this, duplicated = std::move(duplicated)]()
        {
            _selection.replaceSelection(duplicated, duplicated.front());
        });
}

void EditorLayer::cmdDeleteSelection()
{
    if (!canViewportAuthor()) {
        return;
    }
    if (!getEditableScene()) {
        return;
    }
    _selection.deleteSelection();
    notifyHierarchyChanged();
    markSceneDirty();
}

} // namespace ya
