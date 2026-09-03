#include "GameEditor/UI/EditorHierarchyOps.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"

#include <charconv>
#include <format>
#include <string_view>

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

} // namespace ya
