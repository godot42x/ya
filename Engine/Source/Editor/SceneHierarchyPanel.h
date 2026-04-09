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
struct Node;

struct SceneHierarchyPanel
{
    enum class ENodeDropPosition
    {
        Before,
        Into,
        After,
    };

    static constexpr const char *NODE_DRAG_DROP_PAYLOAD = "SCENE_HIERARCHY_NODE";

    EditorLayer *_owner;
    Scene       *_context       = nullptr;
    Entity      *_selection     = {};
    Entity      *_pendingScrollSelection = {};
    Node        *_pendingDraggedNode = nullptr;
    Node        *_pendingDropTarget  = nullptr;
    ENodeDropPosition _pendingDropPosition = ENodeDropPosition::Into;

  public:
    SceneHierarchyPanel(EditorLayer *owner) : _owner(owner) {}

    void setContext(Scene *scene)
    {
        _context = scene;
        // 清空选中实体，防止指向已销毁场景中的实体
        if (_selection && (!_selection->isValid() || _selection->getScene() != scene)) {
            _selection              = nullptr;
            _pendingScrollSelection = nullptr;
        }
    }
    void onImGuiRender();

    [[nodiscard]] Entity *getSelectedEntity() const { return _selection; }
    void                  setSelection(Entity *entity);


    void sceneTree();
    bool shouldAutoOpenForSelection(Node *node) const;

    // Node hierarchy rendering
    void drawNodeRecursive(Node *node);
    void renderStandaloneEntities();

    void               drawFlatEntity(Entity &entity);
    const std::string &getNodeName(Node *node);
    Node               *getSelectedNode() const;
    ENodeDropPosition   getDropPosition(float itemMinY, float itemMaxY) const;
    void                queueMoveNode(Node *draggedNode, Node *targetNode, ENodeDropPosition dropPosition);
    void                flushPendingNodeMove();
    bool                moveNode(Node *draggedNode, Node *targetNode, ENodeDropPosition dropPosition);
};

} // namespace ya
