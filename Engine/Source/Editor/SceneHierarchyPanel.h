#pragma once

#include "ECS/Entity.h"
#include "FilePicker.h"
#include <memory>
#include <sol/sol.hpp>


namespace ya
{

// Constants
constexpr size_t SCRIPT_INPUT_BUFFER_SIZE = 256;

struct Scene;
struct EditorLayer;
struct LuaScriptComponent;

struct SceneHierarchyPanel
{
    EditorLayer *_owner;
    Scene       *_context       = nullptr;
    Entity      *_selection     = {};
    Entity      *_lastSelection = {};

  public:
    SceneHierarchyPanel(EditorLayer *owner) : _owner(owner) {}

    void setContext(Scene *scene)
    {
        _context = scene;
        // 清空选中实体，防止指向已销毁场景中的实体
        if (_selection && (!_selection->isValid() || _selection->getScene() != scene)) {
            _selection     = nullptr;
            _lastSelection = nullptr;
        }
    }
    void onImGuiRender();

    [[nodiscard]] Entity *getSelectedEntity() const { return _selection; }
    void                  setSelection(Entity *entity);


    void sceneTree();

    void drawEntityNode(Entity &entity);
};

} // namespace ya
