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

    void setContext(Scene *scene) { _context = scene; }
    void onImGuiRender();

    [[nodiscard]] Entity *getSelectedEntity() const { return _selection; }
    void                  setSelection(Entity *entity);


    void sceneTree();
    void detailsView();

    void drawEntityNode(Entity &entity);
    void drawComponents(Entity &entity);

  private:
    void renderScriptProperty(void *propPtr, void *scriptInstancePtr);
    void tryLoadScriptForEditor(void *scriptPtr);

    // 编辑器专用 Lua 状态（用于预览属性）
    sol::state _editorLua;
    bool       _editorLuaInitialized = false;

    // 文件选择器
    FilePicker _filePicker;
};

} // namespace ya
