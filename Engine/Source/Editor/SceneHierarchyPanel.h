#pragma once

#include "ECS/Entity.h"
#include <memory>

namespace ya
{
struct Scene;

class SceneHierarchyPanel
{


  private:
    std::shared_ptr<Scene> _context       = nullptr;
    Entity                 _selection     = {};
    Entity                 _lastSelection = {};

  public:
    SceneHierarchyPanel(std::shared_ptr<Scene> scene)
    {
        _context = scene;
    }

    void setContext(const std::shared_ptr<Scene> &scene);
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
