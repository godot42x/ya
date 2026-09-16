#include "GameEditor/Panels/SceneHierarchyPanel.h"

#include "ECS/Component.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/EditorHierarchyOps.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"
#include "Core/KeyCode.h"
#include "Core/Os/Os.h"

#include <algorithm>

namespace ya
{

namespace
{

bool isMultiSelectModifierPressed()
{
    const uint32_t mod = Os::queryKeyModState();
    return (mod & EKeyMod::Ctrl) != 0 || (mod & EKeyMod::Gui) != 0;
}

bool isRangeSelectModifierPressed()
{
    return (Os::queryKeyModState() & EKeyMod::Shift) != 0;
}

} // namespace

void SceneHierarchyPanel::setContext(Scene* scene)
{
    if (_context == scene) {
        return;
    }

    _context = scene;

    bool bSelectionChanged = false;
    if (!_selections.empty()) {
        const size_t before = _selections.size();
        std::erase_if(_selections, [&](Entity* e) { return !e || !e->isValid() || e->getScene() != scene; });
        bSelectionChanged = _selections.size() != before;
    }
    if (_primarySelection && (!_primarySelection->isValid() || _primarySelection->getScene() != scene)) {
        _primarySelection = _selections.empty() ? nullptr : _selections.front();
        bSelectionChanged = true;
    }
    if (_rangeAnchor && (!_rangeAnchor->isValid() || _rangeAnchor->getScene() != scene)) {
        _rangeAnchor = nullptr;
    }

    if (bSelectionChanged || !_primarySelection) {
        notifyOwnerSelection();
    }
}

void SceneHierarchyPanel::setSelection(Entity* entity)
{
    if (entity && entity->isValid()) {
        _selections       = {entity};
        _primarySelection = entity;
        _rangeAnchor      = entity;
    }
    else {
        _selections.clear();
        _primarySelection = nullptr;
        _rangeAnchor      = nullptr;
    }
    notifyOwnerSelection();
}

void SceneHierarchyPanel::handleEntityClick(Entity* entity)
{
    if (!entity || !entity->isValid()) {
        return;
    }

    const bool bMulti = isMultiSelectModifierPressed();
    const bool bRange = isRangeSelectModifierPressed();

    if (bRange && _rangeAnchor) {
        buildFlatEntityList();
        auto anchorIt = std::find(_flatEntities.begin(), _flatEntities.end(), _rangeAnchor);
        auto clickIt  = std::find(_flatEntities.begin(), _flatEntities.end(), entity);
        if (anchorIt != _flatEntities.end() && clickIt != _flatEntities.end()) {
            auto [rangeBegin, rangeEnd] = std::minmax(anchorIt, clickIt);
            _selections.assign(rangeBegin, std::next(rangeEnd));
            std::erase(_selections, entity);
            _selections.insert(_selections.begin(), entity);
            _primarySelection = entity;
            notifyOwnerSelection();
            return;
        }
    }

    if (bMulti) {
        auto it = std::find(_selections.begin(), _selections.end(), entity);
        if (it != _selections.end()) {
            _selections.erase(it);
            if (_primarySelection == entity) {
                _primarySelection = _selections.empty() ? nullptr : _selections.front();
            }
        }
        else {
            _selections.insert(_selections.begin(), entity);
            _primarySelection = entity;
        }
        _rangeAnchor = entity;
        notifyOwnerSelection();
        return;
    }

    _selections       = {entity};
    _primarySelection = entity;
    _rangeAnchor      = entity;
    notifyOwnerSelection();
}

void SceneHierarchyPanel::replaceSelection(const std::vector<Entity*>& entities, Entity* primary)
{
    _selections       = entities;
    _primarySelection = primary ? primary : (_selections.empty() ? nullptr : _selections.front());
    _rangeAnchor      = _primarySelection;
    notifyOwnerSelection();
}

void SceneHierarchyPanel::deleteSelection()
{
    if (!_context) {
        replaceSelection({}, nullptr);
        return;
    }

    // Model instance children are regenerated from the root's ModelRef, so
    // deleting one cannot stick; it would only punch a hole until the next
    // instantiation recreated it. Delete the instance root instead.
    for (Entity* entity : _selections) {
        if (!entity || !entity->isValid() || entity->getScene() != _context) {
            continue;
        }
        if (editorIsInstanceChild(entity)) {
            YA_CORE_WARN("Cannot delete '{}': model instance children are rebuilt from '{}'. Delete that root instead.",
                         entity->getName(),
                         editorResolveInstanceRoot(*_context, entity)->getName());
            continue;
        }
        _context->destroyEntity(entity);
    }
    replaceSelection({}, nullptr);
}

void SceneHierarchyPanel::notifyOwnerSelection()
{
    if (_owner) {
        _owner->setSelections(_selections, _primarySelection);
    }
}

void SceneHierarchyPanel::buildFlatEntityList()
{
    _flatEntities.clear();
    if (!_context) {
        return;
    }

    if (Node* rootNode = _context->getRootNode()) {
        for (Node* child : rootNode->getChildren()) {
            collectEntities(child);
        }
    }

    auto view = _context->getRegistry().view<TransformComponent>();
    for (auto entityHandle : view) {
        Entity* entity = _context->getEntityByEnttID(entityHandle);
        if (entity && !_context->getNodeByEntity(entityHandle)) {
            _flatEntities.push_back(entity);
        }
    }
}

void SceneHierarchyPanel::collectEntities(Node* node)
{
    if (!node) {
        return;
    }

    if (Entity* entity = node->getEntity(); entity && entity->isValid()) {
        _flatEntities.push_back(entity);
    }

    for (Node* child : node->getChildren()) {
        collectEntities(child);
    }
}

} // namespace ya
