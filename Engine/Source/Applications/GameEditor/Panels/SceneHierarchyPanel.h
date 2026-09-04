#pragma once

#include "ECS/Entity.h"

#include <vector>

namespace ya
{

struct Scene;
struct EditorLayer;
struct Node;

/// Entity selection bus for viewport pick and legacy imgui chrome. Retained
/// hierarchy UI lives in EditorSurface; ImGui sceneTree draw path removed in
/// Phase 8O.
struct SceneHierarchyPanel
{
    EditorLayer*         _owner             = nullptr;
    Scene*               _context           = nullptr;
    std::vector<Entity*> _selections;
    Entity*              _primarySelection  = nullptr;
    Entity*              _rangeAnchor       = nullptr;
    std::vector<Entity*> _flatEntities;

  public:
    explicit SceneHierarchyPanel(EditorLayer* owner) : _owner(owner) {}

    void setContext(Scene* scene);

    [[nodiscard]] Entity* getSelectedEntity() const { return _primarySelection; }
    void                  setSelection(Entity* entity);
    void                  handleEntityClick(Entity* entity);
    void                  replaceSelection(const std::vector<Entity*>& entities, Entity* primary);
    void                  deleteSelection();

  private:
    void notifyOwnerSelection();
    void buildFlatEntityList();
    void collectEntities(Node* node);
};

} // namespace ya
