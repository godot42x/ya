#pragma once

#include "ECS/Entity.h"
#include "Editor/FilePicker.h"
#include <cstddef>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


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
    static constexpr size_t      SEARCH_BUFFER_SIZE     = 128;

    EditorLayer *_owner;
    Scene       *_context       = nullptr;

    // === Multi-select state ===
    std::vector<Entity*> _selections;      // Click order; primary kept at front on notify
    Entity*              _primarySelection = nullptr;
    Entity*              _rangeAnchor      = nullptr;

    std::vector<Entity*> _flatEntities;    // DFS tree order + standalone entities, rebuilt per frame
    std::unordered_map<Node*, bool> _lastNodeOpenState; // Last rendered open state per node
    std::unordered_set<Node*>       _pendingTreeToggle; // Label click => toggle collapse next frame

    // === Search state ===
    char    _searchBuffer[SEARCH_BUFFER_SIZE] = "";
    std::string _searchLower;

    // === Pending batch actions (deferred to keep tree iteration safe) ===
    std::vector<Node*>   _pendingNodeDuplicate;
    std::vector<Entity*> _pendingEntityDelete;

    // === Scroll / drag-drop state ===
    Entity* _pendingScrollSelection = {};
    Node*   _pendingDraggedNode     = nullptr;
    Node*   _pendingDropTarget      = nullptr;
    ENodeDropPosition _pendingDropPosition = ENodeDropPosition::Into;

  public:
    SceneHierarchyPanel(EditorLayer *owner) : _owner(owner) {}

    void setContext(Scene *scene);
    void onImGuiRender();

    [[nodiscard]] Entity *getSelectedEntity() const { return _primarySelection; }
    void                  setSelection(Entity *entity);
    /// Plain / Ctrl-toggle / Shift-range click semantics (reads ImGui IO).
    void                  handleEntityClick(Entity *entity);
    /// Replace the whole entity selection and notify the owner.
    void                  replaceSelection(const std::vector<Entity*> &entities, Entity *primary);
    /// Batch duplicate/delete of the current selection (deferred, safe).
    void                  duplicateSelection();
    void                  deleteSelection();


    void sceneTree();
    bool shouldAutoOpenForSelection(Node *node) const;

    // Node hierarchy rendering
    void drawNodeRecursive(Node *node);
    void drawNodeDropTarget(Node *node, ImVec2 itemMin, ImVec2 itemMax, bool isHovered);
    void renderStandaloneEntities();

    void               drawFlatEntity(Entity &entity);
    const std::string &getNodeName(Node *node) const;
    Node               *getSelectedNode() const;
    ENodeDropPosition   getDropPosition(float itemMinY, float itemMaxY) const;
    void                queueMoveNode(Node *draggedNode, Node *targetNode, ENodeDropPosition dropPosition);
    void                flushPendingNodeMove();
    bool                moveNode(Node *draggedNode, Node *targetNode, ENodeDropPosition dropPosition);

    // Multi-select / search helpers
    bool isSelected(Entity *entity) const;
    void notifyOwnerSelection();
    void validateSelections();
    void buildFlatEntityList();
    void collectEntities(Node *node);
    bool isSearchActive() const { return _searchBuffer[0] != '\0'; }
    bool matchesFilter(const std::string &name) const;
    bool subtreeMatchesFilter(Node *node) const;
    void drawCreateMenuItems(Node *parentNode);
    void drawEntityNodeContextMenu(Node *node, Entity *entity);
    void flushPendingActions();
};

} // namespace ya
