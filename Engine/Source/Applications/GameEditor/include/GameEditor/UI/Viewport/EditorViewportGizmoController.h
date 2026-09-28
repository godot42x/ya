#pragma once

#include "GameEditor/UI/Ops/EditorTransformUndo.h"
#include "GameEditor/UI/Viewport/EditorViewportHost.h"

#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace ya
{
struct ScreenDrawList;
}

namespace ya
{

struct App;
struct Entity;
struct Scene;
class UndoStack;

enum class EEditorViewportGizmoOperation : uint8_t
{
    Translate = 0,
    Rotate    = 1,
    Scale     = 2,
};

enum class EEditorViewportGizmoMode : uint8_t
{
    Local = 0,
    World = 1,
};

enum class EEditorViewportGizmoAxis : uint8_t
{
    None = 0,
    X    = 1,
    Y    = 2,
    Z    = 3,
};

/// Narrow scene/selection queries the gizmo needs without owning EditorLayer.
struct FEditorViewportGizmoSources
{
    std::function<Entity*()> getSelectedEntity;
    std::function<const std::vector<Entity*>&()> getSelections;
    std::function<Scene*()> getViewportInteractionScene;
    std::function<bool()> isViewportMode2D;
    /// Translate stays on the sprite's XY plane: the Z handle does not move Z.
    std::function<bool()> isEditorOrthoXY;
    std::function<void()> onTransformCommitted;
};

/// Native viewport TRS gizmo: hit/drag math, undo capture, and Render2D overlay.
class EditorViewportGizmoController
{
    App*                          _app     = nullptr;
    UndoStack*                    _undo    = nullptr;
    FEditorViewportGizmoSources   _sources{};
    EEditorViewportGizmoOperation _operation = EEditorViewportGizmoOperation::Translate;
    EEditorViewportGizmoMode      _mode      = EEditorViewportGizmoMode::Local;
    FEditorViewportHostState      _hostState{};
    bool                          _bHostValid          = false;
    bool                          _bHovered            = false;
    bool                          _bDragging           = false;
    bool                          _bPointerInside      = false;
    bool                          _bConsumeReleasePick = false;
    glm::vec2                     _pointerLocal        = {0.0f, 0.0f};
    EEditorViewportGizmoAxis      _hoveredAxis         = EEditorViewportGizmoAxis::None;
    EEditorViewportGizmoAxis      _activeAxis          = EEditorViewportGizmoAxis::None;
    glm::mat4                     _dragStartPrimaryWorld = glm::mat4(1.0f);
    glm::vec3                     _dragAxisWorld         = {0.0f, 0.0f, 0.0f};
    glm::vec3                     _dragOriginWorld       = {0.0f, 0.0f, 0.0f};
    glm::vec3                     _dragPlaneNormal       = {0.0f, 0.0f, 0.0f};
    glm::vec3                     _dragStartPlaneVector  = {0.0f, 0.0f, 0.0f};
    float                         _dragStartScalar       = 0.0f;
    std::vector<FEditorTransformSnapshot> _undoBefore;

  public:
    void bind(App* app, FEditorViewportGizmoSources sources);
    void setUndoStack(UndoStack* undo) { _undo = undo; }

    void syncHost(const FEditorViewportHostState& host);
    void setPointer(const glm::vec2& localPoint, bool insideViewport);
    [[nodiscard]] bool beginDrag(const glm::vec2& localPoint);
    void updateDrag(const glm::vec2& localPoint);
    void endDrag();
    void cancelDrag();
    void recordOverlay(ScreenDrawList& list) const;
    void setOperation(EEditorViewportGizmoOperation operation);

    [[nodiscard]] bool isActive() const { return _bDragging || _bHovered; }
    [[nodiscard]] bool isDragging() const { return _bDragging; }
    [[nodiscard]] bool consumeReleasePick();
    [[nodiscard]] bool hasSelectedEntities() const;

  private:
    [[nodiscard]] Entity* selectedEntity() const;
    [[nodiscard]] const std::vector<Entity*>& selections() const;
    [[nodiscard]] Scene* viewportScene() const;
    [[nodiscard]] bool hasViewportGizmoSelection() const;
};

} // namespace ya
