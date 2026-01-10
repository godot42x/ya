#pragma once

#include "ECS/Entity.h"
#include "FilePicker.h"
#include <memory>
#include <sol/sol.hpp>

namespace ya
{

// Constants
constexpr size_t DETAILS_SCRIPT_INPUT_BUFFER_SIZE = 256;

struct Scene;
struct EditorLayer;
struct LuaScriptComponent;

struct DetailsView
{
  private:
    EditorLayer *_owner;

    // 编辑器专用 Lua 状态（用于预览属性）
    sol::state _editorLua;
    bool       _editorLuaInitialized = false;

    // 文件选择器
    FilePicker _filePicker;

  public:
    DetailsView(EditorLayer *owner) : _owner(owner) {}

    void onImGuiRender();

  private:
    void drawComponents(Entity &entity);
    void renderScriptProperty(void *propPtr, void *scriptInstancePtr);
    void tryLoadScriptForEditor(void *scriptPtr);
};

} // namespace ya
