#pragma once

#include "ECS/Entity.h"
#include "Editor/FilePicker.h"
#include "Scene/Core/SceneWidgetEntry.h"
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
    static constexpr const char *UI_ENTRY_DRAG_DROP_PAYLOAD = "SCENE_HIERARCHY_UI_ENTRY";
    static constexpr size_t      SEARCH_BUFFER_SIZE     = 128;
    static constexpr size_t      UI_DRAG_MAX_PATH_DEPTH = 16;

    /// POD drag payload for Game UI entry/document drag-drop (ImGui payloads
    /// are memcpy'd, so no STL containers). The target adds its own position
    /// and queues a full UIDragRequest at drop time.
    struct UIEntryDragPayload
    {
        size_t   srcEntryIndex = SIZE_MAX;
        uint32_t srcPathLength = 0;
        uint32_t srcPath[UI_DRAG_MAX_PATH_DEPTH] = {};
    };

    /// Full reparent request queued during the tree render and flushed after
    /// it (never mutate documents while the tree iterates them).
    struct UIDragRequest
    {
        size_t               srcEntryIndex = SIZE_MAX;
        std::vector<size_t>  srcPath;   // child path inside src entry (empty = entry root)
        size_t               dstEntryIndex = SIZE_MAX;
        std::vector<size_t>  dstPath;
        ENodeDropPosition    position = ENodeDropPosition::Into;
    };

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

    // === Pending Game UI entry reparent ===
    UIDragRequest _pendingUIDrag;
    bool          _bHasPendingUIDrag = false;

    /// Tree node kept open during/after a UI drag so the drop target and its
    /// result stay visible (UMG/Godot-style auto-expand).
    struct UIDragExpandTarget
    {
        bool               valid = false;
        size_t             entryIndex = SIZE_MAX;
        std::vector<size_t> path;
    };
    UIDragExpandTarget _uiDragAutoExpand;
    UIDragExpandTarget _uiPostDropExpand;
    int                _uiPostDropExpandFrames = 0;

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
    /// Game UI authoring entries (SceneWidgetEntry) section.
    void drawWidgetEntries();
    void drawWidgetEntryRow(SceneWidgetEntry& entry, size_t index);
    void drawAddEntryMenu();
    /// Recursive display of an entry's document widget tree (the "tree view"
    /// for Game UI Entries; deep edits happen in UI Designer). Nodes are also
    /// drag sources/targets so the hierarchy can be restructured by dragging.
    void drawEntryDocumentTree(const std::shared_ptr<UIDocument>& document,
                               SceneWidgetEntry&                 entry,
                               size_t                           entryIndex,
                               std::vector<size_t>&               childPath);
    /// Display-only tree of the UI Designer's open document when it is not
    /// referenced by any scene entry yet (New/untitled). Clicking a node
    /// selects the widget in the designer.
    void drawLiveDocumentTree(const std::shared_ptr<UIDocument>& document, std::vector<size_t>& childPath);
    /// Open the entry's document in the UI Designer and select the widget at
    /// `childPath` (empty = root).
    void openEntryWidgetInDesigner(SceneWidgetEntry& entry, const std::vector<size_t>& childPath);

    // === Game UI entry drag-drop (document-level parent-child editing) ===
    /// Publish the drag source payload for an entry row / document node.
    void publishUIDragSource(size_t entryIndex, const std::vector<size_t>& path);
    /// Accept the UI-entry payload on an entry row / document node target
    /// (Into only: drop on the row = become a child). Rejects invalid drops
    /// (self / cycle / unresolvable) with red feedback. `rowId` is the row's
    /// own TreeNodeEx id (the source row is auto-rejected by ImGui).
    void publishUIDragTarget(ImGuiID rowId,
                             size_t entryIndex,
                             const std::vector<size_t>& path,
                             const ImVec2&              itemMin,
                             const ImVec2&              itemMax);
    /// Invisible (non-layout) drop band around the insertion line at `edgeY`
    /// between sibling entries — the ONLY insert path (Before/After). Blue
    /// highlight when valid, red when rejected.
    void drawEntryInsertTarget(size_t entryIndex, const std::vector<size_t>& path,
                               ENodeDropPosition position, float edgeY);
    /// Invisible (non-layout) drop band around `edgeY` between sibling 3D
    /// nodes (same unified insert-gap style). `sibling` is the reference node.
    void drawNodeInsertGap(Node* sibling, ENodeDropPosition position, float edgeY);
    /// Whether `dragged` may become a child of / be inserted next to `target`
    /// (rejects self, the scene root, and its own descendants = cycles).
    [[nodiscard]] bool canParentNode(Node* dragged, Node* target) const;
    /// Whether an entry/document drag may land on (entryIndex, path) at
    /// `position` (validation-only; mirrors moveWidgetEntryDocument rules).
    [[nodiscard]] bool canAcceptUIEntryDrop(const UIEntryDragPayload& src,
                                            size_t                   dstEntryIndex,
                                            const std::vector<size_t>& dstPath,
                                            ENodeDropPosition         position) const;
    /// Resolve a .yaui path, preferring the UI Designer's live document.
    [[nodiscard]] std::shared_ptr<UIDocument> resolveUIDocumentPath(const std::string& path) const;
    void queueUIDrag(const UIEntryDragPayload& payload,
                     size_t                   dstEntryIndex,
                     const std::vector<size_t>& dstPath,
                     ENodeDropPosition         position);
    /// Execute the pending reparent after the tree render (safe mutation).
    void flushUIDrag();
    /// Resolve the shared document at (entry, child path); empty path = the
    /// entry root document (inline only). Null when unresolvable.
    static std::shared_ptr<UIDocument> resolveEntryNode(const SceneWidgetEntry& entry,
                                                        const std::vector<size_t>& path);
    void drawNodeDropTarget(Node *node, ImVec2 itemMin, ImVec2 itemMax);
    void renderStandaloneEntities();

    void               drawFlatEntity(Entity &entity);
    const std::string &getNodeName(Node *node) const;
    Node               *getSelectedNode() const;
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
