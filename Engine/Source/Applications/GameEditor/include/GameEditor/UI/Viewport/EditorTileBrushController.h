#pragma once

#include "GameEditor/UI/Viewport/EditorViewportHost.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ya
{

struct Entity;
struct Scene;
struct TilemapComponent;
struct TransformComponent;
struct WorldDrawList;
class UndoStack;

enum class ETileBrushTool : uint8_t
{
    Move = 0,
    Paint,
    Erase,
    RectFill,
    Eyedropper,
};

// Rectangular stamp: cell values (tile index + 1) row-major, painted with
// its top-left at the cursor cell. Erase ignores it; RectFill uses values[0].
struct FTileStamp
{
    int32_t              width  = 1;
    int32_t              height = 1;
    std::vector<int32_t> values = {1};
};

// Narrow scene/selection queries the brush needs without owning EditorLayer,
// mirroring FEditorViewportGizmoSources.
struct FTileBrushSources
{
    std::function<Entity*()> getSelectedEntity;
    std::function<Scene*()>  getViewportInteractionScene;
    std::function<bool()>    isEditorOrthoXY;
    std::function<void()>    onTilesCommitted;
};

// Viewport tile brush: stamp/erase/rect/eyedropper over the selected
// TilemapComponent in the ortho XY viewport. A stroke (press to release)
// captures the layer cells once and pushes exactly one undo step through
// pushTileLayerUndo, so one undo always means one stroke.
class EditorTileBrushController
{
  public:
    void bind(FTileBrushSources sources);
    void setUndoStack(UndoStack* undo) { _undo = undo; }

    void syncHost(const FEditorViewportHostState& host) { _host = host; }

    void setTool(ETileBrushTool tool) { _tool = tool; }
    [[nodiscard]] ETileBrushTool tool() const { return _tool; }
    void setStamp(FTileStamp stamp);
    [[nodiscard]] const FTileStamp& stamp() const { return _stamp; }
    void setLayer(size_t layer);
    [[nodiscard]] size_t layer() const { return _layer; }

    // True when a left-drag in the viewport must paint instead of moving
    // the gizmo: a paint-family tool is armed, a tilemap is selected, and
    // the viewport looks straight down +Z-free XY.
    [[nodiscard]] bool isEngaged() const;
    [[nodiscard]] bool isStroking() const { return _bStroking; }

    [[nodiscard]] bool beginStroke(const glm::vec2& localPoint);
    void               updateStroke(const glm::vec2& localPoint);
    void               endStroke();
    void               cancelStroke();
    void               setHover(const glm::vec2& localPoint);

    void recordWorldOverlay(WorldDrawList& list) const;

    // Viewport-local point (top-left origin) to map cell. False outside
    // the map or without a selected tilemap.
    [[nodiscard]] bool screenToCell(const glm::vec2& localPoint, int32_t& outX, int32_t& outY) const;
    [[nodiscard]] TilemapComponent* selectedMap() const;

  private:
    struct FStrokeTarget
    {
        Entity*            entity    = nullptr;
        TilemapComponent*  map       = nullptr;
        TransformComponent* transform = nullptr;
        uint64_t           entityUUID = 0;
        size_t             layer     = 0;
    };

    [[nodiscard]] bool resolveTarget(FStrokeTarget& target) const;
    [[nodiscard]] glm::mat4 mapWorldMatrix(TransformComponent* transform) const;
    void paintStamp(TilemapComponent& map, size_t layer, int32_t x, int32_t y);
    void paintStampLine(int32_t toX, int32_t toY);
    [[nodiscard]] int32_t strokeFirstValue() const;

    FTileBrushSources       _sources{};
    UndoStack*              _undo = nullptr;
    FEditorViewportHostState _host{};
    ETileBrushTool          _tool  = ETileBrushTool::Move;
    FTileStamp              _stamp;
    size_t                  _layer     = 0;

    bool                   _bStroking   = false;
    FStrokeTarget          _stroke{};
    std::vector<int32_t>   _strokeBefore;
    int32_t                _lastX = 0;
    int32_t                _lastY = 0;
    bool                   _bHasLast = false;
    int32_t                _anchorX  = 0;
    int32_t                _anchorY  = 0;
    int32_t                _hoverX   = 0;
    int32_t                _hoverY   = 0;
    bool                   _bHasHover = false;
};

} // namespace ya

