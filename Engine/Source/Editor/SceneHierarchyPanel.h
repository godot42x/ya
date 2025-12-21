#pragma once

#include "ECS/Entity.h"
#include <memory>

namespace ya
{
struct Scene;

class SceneHierarchyPanel
{


  private:
    Scene *_context       = nullptr;
    Entity _selection     = {};
    Entity _lastSelection = {};

  public:
    SceneHierarchyPanel(Scene *scene)
    {
        _context = scene;
    }

    void setContext(Scene *&scene);
    void onImGuiRender();

    Entity getSelectedEntity() const { return _selection; }
    void   setSelection(Entity entity);

  private:
    void drawEntityNode(Entity &entity);
    void drawComponents(Entity &entity);



    void sceneTree();

    void detailsView();
};

} // namespace ya
