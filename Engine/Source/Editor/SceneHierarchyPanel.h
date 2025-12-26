#pragma once

#include "ECS/Entity.h"
#include <memory>

namespace ya
{
struct Scene;
struct EditorLayer;

struct SceneHierarchyPanel
{
    EditorLayer *_owner;
    Scene       *_context       = nullptr;
    Entity       _selection     = {};
    Entity       _lastSelection = {};

  public:
    SceneHierarchyPanel(EditorLayer *owner) : _owner(owner) {}

    void setContext(Scene *scene) { _context = scene; }
    void onImGuiRender();

    [[nodiscard]] Entity getSelectedEntity() const { return _selection; }
    void                 setSelection(Entity entity);


    void sceneTree();
    void detailsView();

    void drawEntityNode(Entity &entity);
    void drawComponents(Entity &entity);
};

} // namespace ya
