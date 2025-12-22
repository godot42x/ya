#pragma once

#include "ECS/Entity.h"
#include <memory>

namespace ya
{
struct Scene;

struct SceneHierarchyPanel
{
    Scene *_context       = nullptr;
    Entity _selection     = {};
    Entity _lastSelection = {};

  public:
    SceneHierarchyPanel() = default;
    explicit SceneHierarchyPanel(Scene *scene) : _context(scene) {}

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
