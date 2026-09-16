#include "GameEditor/UI/EditorHierarchyOps.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "GUI/Binding/SelectionModel.h"
#include "GameEditor/EditorLayer.h"
#include "Hierarchy/Node.h"
#include "Scene3D/ManagedChildComponent.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <string_view>
#include <vector>

namespace ya
{

std::string editorHierarchyEntityIdKey(uint64_t uuid)
{
    return std::format("e:{}", uuid);
}

bool editorIsInstanceChild(const Entity* entity)
{
    return entity != nullptr && entity->hasComponent<ManagedChildComponent>();
}

Entity* editorResolveInstanceRoot(Scene& scene, Entity* entity)
{
    if (!entity || !entity->isValid() || !editorIsInstanceChild(entity)) {
        return entity;
    }

    // Walk past every managed ancestor. The instantiation system currently
    // produces exactly one managed level (meshes under the ModelComponent
    // entity), but walking the whole chain keeps this correct if a managed
    // child ever gains managed children of its own.
    Node*   node   = scene.getNodeByEntity(entity);
    Node*   parent = node ? node->getParent() : nullptr;
    Entity* root   = entity;
    uint32_t guard = 0;
    while (parent && guard++ < 64) {
        Entity* parentEntity = parent->getEntity();
        if (!parentEntity) {
            break;
        }

        root = parentEntity;
        if (!editorIsInstanceChild(parentEntity)) {
            // First unmanaged ancestor: the object the author placed in the scene.
            break;
        }
        parent = parent->getParent();
    }

    // A chain with no unmanaged ancestor (an orphaned managed subtree) falls
    // back to its topmost managed node, the best available approximation of the
    // instance root.
    return root;
}

bool editorSelectionIsAllInstanceChildren(EditorLayer& layer)
{
    const std::vector<Entity*>& selections = layer.getSelections();
    if (selections.empty()) {
        return false;
    }

    for (const Entity* entity : selections) {
        if (!entity || !entity->isValid() || !editorIsInstanceChild(entity)) {
            return false;
        }
    }
    return true;
}

size_t editorCountInstanceChildren(Scene& scene, Entity* instanceRoot)
{
    Node* rootNode = instanceRoot ? scene.getNodeByEntity(instanceRoot) : nullptr;
    if (!rootNode) {
        return 0;
    }

    auto& registry = scene.getRegistry();
    size_t count = 0;
    std::vector<const Node*> pending{rootNode};
    while (!pending.empty()) {
        const Node* node = pending.back();
        pending.pop_back();
        if (!node) {
            continue;
        }
        for (Node* child : node->getChildren()) {
            if (!child) {
                continue;
            }
            const Entity* childEntity = child->getEntity();
            if (childEntity && registry.all_of<ManagedChildComponent>(childEntity->getHandle())) {
                ++count;
            }
            pending.push_back(child);
        }
    }
    return count;
}

bool parseEditorHierarchyEntityIdKey(const std::string& id, uint64_t& outUuid)
{
    if (id.size() < 3 || id[0] != 'e' || id[1] != ':') {
        return false;
    }
    const std::string_view digits{id.data() + 2, id.size() - 2};
    auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), outUuid);
    return ec == std::errc{} && ptr == digits.data() + digits.size();
}

Entity* moveEditorHierarchyEntity(Scene& scene, const std::string& fromId, const std::string& toId, int dropMode)
{
    uint64_t fromUuid = 0;
    uint64_t toUuid   = 0;
    if (!parseEditorHierarchyEntityIdKey(fromId, fromUuid) ||
        !parseEditorHierarchyEntityIdKey(toId, toUuid)) {
        return nullptr;
    }

    Entity* draggedEntity = scene.getEntityByUUID(fromUuid);
    Entity* targetEntity  = scene.getEntityByUUID(toUuid);
    if (!draggedEntity || !targetEntity) {
        return nullptr;
    }

    Node* draggedNode = scene.getNodeByEntity(draggedEntity);
    Node* targetNode  = scene.getNodeByEntity(targetEntity);
    if (!draggedNode || !targetNode) {
        return nullptr;
    }

    // Model instance children are rebuilt from the root's ModelRef, so their
    // position in the tree is not authored state: a reorder here would be
    // silently discarded on the next instantiation. Reparenting INTO the
    // subtree is rejected for the same reason plus a sharper one — the managed
    // child would be destroyed with the instance, taking the dropped object
    // with it.
    if (editorIsInstanceChild(draggedEntity)) {
        YA_CORE_WARN("Cannot reorder '{}': it is a model instance child rebuilt from '{}'",
                     draggedEntity->getName(),
                     editorResolveInstanceRoot(scene, draggedEntity)->getName());
        return nullptr;
    }
    if (dropMode == 1 && editorIsInstanceChild(targetEntity)) {
        YA_CORE_WARN("Cannot parent '{}' into '{}': model instance children are rebuilt on load",
                     draggedEntity->getName(),
                     targetEntity->getName());
        return nullptr;
    }

    Node* rootNode = scene.getRootNode();
    if (!rootNode || draggedNode == rootNode) {
        return nullptr;
    }

    if (draggedNode == targetNode || draggedNode->isAncestorOf(targetNode)) {
        return nullptr;
    }

    Node*  newParent  = rootNode;
    size_t childIndex = rootNode->getChildCount();

    switch (dropMode) {
        case 1: // into
            newParent  = targetNode;
            childIndex = targetNode->getChildCount();
            break;
        case 0: // before
        case 2: // after
        {
            newParent = targetNode->getParent();
            if (!newParent) {
                newParent = rootNode;
            }

            childIndex = newParent->getChildIndex(targetNode);
            if (childIndex == Node::NPOS) {
                childIndex = newParent->getChildCount();
            }
            else if (dropMode == 2) {
                ++childIndex;
            }
            break;
        }
        default:
            return nullptr;
    }

    if (!scene.moveNode(draggedNode, newParent, childIndex)) {
        return nullptr;
    }
    return draggedEntity;
}

std::vector<Entity*> editorSelectionEntities(EditorLayer& layer, const SelectionModel* selection)
{
    std::vector<Entity*> targets;
    if (selection && !selection->selected().empty()) {
        Scene* scene = layer.getHierarchyScene();
        if (!scene) {
            return {};
        }
        targets.reserve(selection->selected().size());
        for (const std::string& id : selection->selected()) {
            uint64_t uuid = 0;
            if (!parseEditorHierarchyEntityIdKey(id, uuid)) {
                continue;
            }
            if (Entity* entity = scene->getEntityByUUID(uuid)) {
                targets.push_back(entity);
            }
        }
        uint64_t primaryUuid = 0;
        if (parseEditorHierarchyEntityIdKey(selection->primary(), primaryUuid)) {
            auto it = std::find_if(targets.begin(), targets.end(), [primaryUuid](Entity* entity) {
                auto* id = entity ? entity->getComponent<IDComponent>() : nullptr;
                return id && id->_id.value == primaryUuid;
            });
            if (it != targets.end() && it != targets.begin()) {
                std::rotate(targets.begin(), it, std::next(it));
            }
        }
    }
    else {
        targets = layer.getSelections();
        if (targets.empty()) {
            if (Entity* entity = layer.getSelectedEntity()) {
                targets.push_back(entity);
            }
        }
    }
    std::erase_if(targets, [](Entity* entity) { return !entity || !entity->isValid() || !entity->getScene(); });
    return targets;
}

} // namespace ya
