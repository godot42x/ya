#include "GameEditor/UI/EditorHierarchyOps.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "GUI/Binding/SelectionModel.h"
#include "GameEditor/EditorLayer.h"
#include "Hierarchy/Node.h"
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
