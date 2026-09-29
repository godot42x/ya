#include "GameEditor/UI/Viewport/EditorTileBrushController.h"

#include "Core/Log.h"
#include "ECS/Component.h"
#include "ECS/Component/2D/TilemapComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/TransformSystem.h"
#include "GameEditor/UI/Ops/EditorTilemapUndo.h"
#include "Render3D/WorldDraw.h"
#include "Scene3D/TransformComponent.h"

#include <algorithm>
#include <cmath>

namespace ya
{

void EditorTileBrushController::bind(FTileBrushSources sources)
{
    _sources = std::move(sources);
}

void EditorTileBrushController::setLayer(size_t layer)
{
    _layer = layer;
    if (TilemapComponent* map = selectedMap()) {
        if (!map->layers.empty()) {
            _layer = std::min(_layer, map->layers.size() - 1);
        }
    }
}

TilemapComponent* EditorTileBrushController::selectedMap() const
{
    if (!_sources.getSelectedEntity) {
        return nullptr;
    }
    Entity* entity = _sources.getSelectedEntity();
    if (!entity || !entity->isValid() || !entity->hasComponent<TilemapComponent>()) {
        return nullptr;
    }
    return entity->getComponent<TilemapComponent>();
}

bool EditorTileBrushController::isEngaged() const
{
    if (_tool == ETileBrushTool::Move || !_sources.isEditorOrthoXY || !_sources.isEditorOrthoXY()) {
        return false;
    }
    TilemapComponent* map = selectedMap();
    return map && map->isValid();
}

glm::mat4 EditorTileBrushController::mapWorldMatrix(TransformComponent* transform) const
{
    if (!transform) {
        return glm::mat4(1.0f);
    }
    TransformSystem::computeWorldMatrix(transform);
    return transform->getWorldMatrix();
}

bool EditorTileBrushController::resolveTarget(FStrokeTarget& target) const
{
    if (!_sources.getSelectedEntity || !_sources.getViewportInteractionScene) {
        return false;
    }
    Entity* entity = _sources.getSelectedEntity();
    Scene*  scene  = _sources.getViewportInteractionScene();
    if (!entity || !entity->isValid() || !scene) {
        return false;
    }
    TilemapComponent* map = entity->hasComponent<TilemapComponent>() ? entity->getComponent<TilemapComponent>() : nullptr;
    TransformComponent* transform =
        entity->hasComponent<TransformComponent>() ? entity->getComponent<TransformComponent>() : nullptr;
    if (!map || !map->isValid() || !transform || map->layers.empty()) {
        return false;
    }
    const auto* id = entity->getComponent<IDComponent>();
    if (!id) {
        return false;
    }
    target.entity     = entity;
    target.map        = map;
    target.transform  = transform;
    target.entityUUID = id->_id.value;
    target.layer      = std::min(_layer, map->layers.size() - 1);
    return true;
}

bool EditorTileBrushController::screenToCell(const glm::vec2& localPoint, int32_t& outX, int32_t& outY) const
{
    FStrokeTarget target;
    if (!resolveTarget(target)) {
        return false;
    }
    if (_host.extent.x <= 0.0f || _host.extent.y <= 0.0f) {
        return false;
    }
    // Viewport-local top-left origin to NDC; the ortho XY view makes x/y
    // depth-independent, so unprojecting on the near plane is exact.
    const glm::vec2 ndc(localPoint.x / _host.extent.x * 2.0f - 1.0f,
                        1.0f - localPoint.y / _host.extent.y * 2.0f);
    const glm::mat4 invViewProj = glm::inverse(_host.projection * _host.view);
    glm::vec4       world       = invViewProj * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
    if (world.w != 0.0f) {
        world /= world.w;
    }
    const glm::mat4 worldMatrix = mapWorldMatrix(target.transform);
    const glm::vec4 local       = glm::inverse(worldMatrix) * world;
    outX = static_cast<int32_t>(std::floor(local.x / target.map->cellSize.x));
    outY = static_cast<int32_t>(std::floor(local.y / target.map->cellSize.y));
    return outX >= 0 && outY >= 0 && outX < target.map->width && outY < target.map->height;
}

void EditorTileBrushController::setStamp(FTileStamp stamp)
{
    if (stamp.width <= 0 || stamp.height <= 0 ||
        stamp.values.size() != static_cast<size_t>(stamp.width) * static_cast<size_t>(stamp.height)) {
        return;
    }
    _stamp = std::move(stamp);
}

void EditorTileBrushController::paintStamp(TilemapComponent& map, size_t layer, int32_t x, int32_t y)
{
    for (int32_t sy = 0; sy < _stamp.height; ++sy) {
        for (int32_t sx = 0; sx < _stamp.width; ++sx) {
            map.setCell(x + sx, y + sy, layer,
                        _stamp.values[static_cast<size_t>(sy) * static_cast<size_t>(_stamp.width) +
                                      static_cast<size_t>(sx)]);
        }
    }
}

void EditorTileBrushController::paintStampLine(int32_t toX, int32_t toY)
{
    if (!_stroke.map) {
        return;
    }
    const int32_t steps = std::max(std::abs(toX - _lastX), std::abs(toY - _lastY));
    for (int32_t i = 0; i <= steps; ++i) {
        const float t = steps == 0 ? 0.0f : static_cast<float>(i) / static_cast<float>(steps);
        const int32_t x = _lastX + static_cast<int32_t>(std::round((toX - _lastX) * t));
        const int32_t y = _lastY + static_cast<int32_t>(std::round((toY - _lastY) * t));
        if (_tool == ETileBrushTool::Erase) {
            _stroke.map->setCell(x, y, _stroke.layer, 0);
        }
        else {
            paintStamp(*_stroke.map, _stroke.layer, x, y);
        }
    }
}

int32_t EditorTileBrushController::strokeFirstValue() const
{
    return _stamp.values.empty() ? 1 : _stamp.values[0];
}

bool EditorTileBrushController::beginStroke(const glm::vec2& localPoint)
{
    if (!isEngaged() || _bStroking || !_undo) {
        return false;
    }
    FStrokeTarget target;
    if (!resolveTarget(target)) {
        return false;
    }
    int32_t cellX = 0;
    int32_t cellY = 0;
    if (!screenToCell(localPoint, cellX, cellY)) {
        return false;
    }
    if (_tool == ETileBrushTool::Eyedropper) {
        const int32_t value = target.map->cellAt(cellX, cellY, target.layer);
        if (value > 0) {
            _stamp = FTileStamp{};
            _stamp.values[0] = value;
        }
        return false;
    }
    _stroke       = target;
    _strokeBefore = target.map->layers[target.layer].cells;
    _bStroking    = true;
    _anchorX = _lastX = cellX;
    _anchorY = _lastY = cellY;
    _bHasLast = true;
    if (_tool == ETileBrushTool::Paint) {
        paintStamp(*target.map, target.layer, cellX, cellY);
    }
    else if (_tool == ETileBrushTool::Erase) {
        target.map->setCell(cellX, cellY, target.layer, 0);
    }
    return true;
}

void EditorTileBrushController::updateStroke(const glm::vec2& localPoint)
{
    if (!_bStroking || !_stroke.map) {
        return;
    }
    int32_t cellX = 0;
    int32_t cellY = 0;
    if (!screenToCell(localPoint, cellX, cellY)) {
        return;
    }
    if ((_tool == ETileBrushTool::Paint || _tool == ETileBrushTool::Erase) && _bHasLast) {
        paintStampLine(cellX, cellY);
    }
    _lastX    = cellX;
    _lastY    = cellY;
    _bHasLast = true;
}

void EditorTileBrushController::endStroke()
{
    if (!_bStroking) {
        return;
    }
    _bStroking = false;
    if (!_stroke.map || !_undo || !_sources.getViewportInteractionScene) {
        _stroke = FStrokeTarget{};
        return;
    }
    if (_tool == ETileBrushTool::RectFill) {
        const int32_t x0 = std::min(_anchorX, _lastX);
        const int32_t y0 = std::min(_anchorY, _lastY);
        const int32_t x1 = std::max(_anchorX, _lastX);
        const int32_t y1 = std::max(_anchorY, _lastY);
        _stroke.map->fillRect(x0, y0, x1, y1, _stroke.layer, strokeFirstValue());
    }
    if (_stroke.layer >= _stroke.map->layers.size()) {
        _stroke = FStrokeTarget{};
        return;
    }
    FTileLayerSnapshot after{_stroke.entityUUID, _stroke.layer, _stroke.map->layers[_stroke.layer].cells};
    if (pushTileLayerUndo(*_undo, _sources.getViewportInteractionScene(),
                          FTileLayerSnapshot{_stroke.entityUUID, _stroke.layer, _strokeBefore}, std::move(after)) &&
        _sources.onTilesCommitted) {
        _sources.onTilesCommitted();
    }
    _stroke = FStrokeTarget{};
}

void EditorTileBrushController::cancelStroke()
{
    if (!_bStroking) {
        return;
    }
    // Restore the captured cells without recording anything.
    if (_stroke.map && _stroke.layer < _stroke.map->layers.size()) {
        _stroke.map->layers[_stroke.layer].cells = _strokeBefore;
    }
    _bStroking = false;
    _stroke    = FStrokeTarget{};
}

void EditorTileBrushController::setHover(const glm::vec2& localPoint)
{
    _bHasHover = screenToCell(localPoint, _hoverX, _hoverY);
}

void EditorTileBrushController::recordWorldOverlay(WorldDrawList& list) const
{
    FStrokeTarget target;
    if (!resolveTarget(target)) {
        return;
    }
    const TilemapComponent& map   = *target.map;
    const glm::mat4         world = mapWorldMatrix(target.transform);
    const float             z     = map.layers[target.layer].zOffset;
    const glm::vec4         grid{0.55f, 0.85f, 1.0f, 0.55f};
    for (int32_t x = 0; x <= map.width; ++x) {
        const glm::vec3 a = glm::vec3(world * glm::vec4(x * map.cellSize.x, 0.0f, z, 1.0f));
        const glm::vec3 b = glm::vec3(world * glm::vec4(x * map.cellSize.x, map.height * map.cellSize.y, z, 1.0f));
        list.makeLine(a, b, grid);
    }
    for (int32_t y = 0; y <= map.height; ++y) {
        const glm::vec3 a = glm::vec3(world * glm::vec4(0.0f, y * map.cellSize.y, z, 1.0f));
        const glm::vec3 b = glm::vec3(world * glm::vec4(map.width * map.cellSize.x, y * map.cellSize.y, z, 1.0f));
        list.makeLine(a, b, grid);
    }
    const glm::vec4 highlight{1.0f, 0.9f, 0.3f, 0.95f};
    const auto      outline = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        const glm::vec3 p00 = glm::vec3(world * glm::vec4(x0 * map.cellSize.x, y0 * map.cellSize.y, z, 1.0f));
        const glm::vec3 p10 = glm::vec3(world * glm::vec4(x1 * map.cellSize.x, y0 * map.cellSize.y, z, 1.0f));
        const glm::vec3 p11 = glm::vec3(world * glm::vec4(x1 * map.cellSize.x, y1 * map.cellSize.y, z, 1.0f));
        const glm::vec3 p01 = glm::vec3(world * glm::vec4(x0 * map.cellSize.x, y1 * map.cellSize.y, z, 1.0f));
        list.makeLine(p00, p10, highlight);
        list.makeLine(p10, p11, highlight);
        list.makeLine(p11, p01, highlight);
        list.makeLine(p01, p00, highlight);
    };
    if (_bStroking && _tool == ETileBrushTool::RectFill) {
        outline(std::min(_anchorX, _lastX), std::min(_anchorY, _lastY),
                std::max(_anchorX, _lastX) + 1, std::max(_anchorY, _lastY) + 1);
    }
    else if (_bHasHover && _tool != ETileBrushTool::Move) {
        outline(_hoverX, _hoverY, _hoverX + 1, _hoverY + 1);
    }
}

} // namespace ya
