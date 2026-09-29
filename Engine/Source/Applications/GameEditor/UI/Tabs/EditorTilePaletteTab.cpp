#include "GameEditor/UI/Tabs/EditorTilePaletteTab.h"

#include "Core/Event.h"
#include "Core/Log.h"
#include "ECS/Component/2D/TilemapComponent.h"
#include "ECS/Entity.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/Texture.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/Text.h"

#include <algorithm>
#include <format>

namespace ya
{

namespace
{

const char* tileToolName(ETileBrushTool tool)
{
    switch (tool) {
    case ETileBrushTool::Move: return "Move";
    case ETileBrushTool::Paint: return "Paint";
    case ETileBrushTool::Erase: return "Erase";
    case ETileBrushTool::RectFill: return "Rect";
    case ETileBrushTool::Eyedropper: return "Pick";
    }
    return "Move";
}

constexpr ETileBrushTool kTileTools[5] = {
    ETileBrushTool::Move, ETileBrushTool::Paint, ETileBrushTool::Erase,
    ETileBrushTool::RectFill, ETileBrushTool::Eyedropper,
};

} // namespace

UITileAtlasGrid::UITileAtlasGrid()
    : UIImage("TileAtlasGrid")
{
    setScaleMode(EImageScaleMode::Stretch);
}

void UITileAtlasGrid::setGrid(int columns, int rows, int tileW, int tileH, float scale)
{
    _columns = std::max(1, columns);
    _rows    = std::max(1, rows);
    _tileW   = std::max(1, tileW);
    _tileH   = std::max(1, tileH);
    _scale   = scale;
}

void UITileAtlasGrid::setSelected(int x, int y, int w, int h)
{
    _selX = x;
    _selY = y;
    _selW = std::max(1, w);
    _selH = std::max(1, h);
}

bool UITileAtlasGrid::pointToTile(const glm::vec2& logicalPoint, int& outX, int& outY) const
{
    const Rect2D& rect  = getLayoutRect();
    const glm::vec2 local = logicalPoint - rect.pos;
    const float     cellW = _tileW * _scale;
    const float     cellH = _tileH * _scale;
    if (cellW <= 0.0f || cellH <= 0.0f) {
        return false;
    }
    const int x = static_cast<int>(std::floor(local.x / cellW));
    const int y = static_cast<int>(std::floor(local.y / cellH));
    if (x < 0 || y < 0 || x >= _columns || y >= _rows) {
        return false;
    }
    outX = x;
    outY = y;
    return true;
}

bool UITileAtlasGrid::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();
    if (eventType != EEvent::MouseButtonPressed && eventType != EEvent::MouseMoved &&
        eventType != EEvent::MouseButtonReleased) {
        return false;
    }
    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }
    if (eventType == EEvent::MouseButtonPressed) {
        int x = 0;
        int y = 0;
        if (!pointToTile(ctx.logicalPoint, x, y)) {
            return false;
        }
        if (WidgetTree* tree = getTree()) {
            tree->setPointerCapture(this);
        }
        _anchorX    = _selX = x;
        _anchorY    = _selY = y;
        _selW = _selH = 1;
        _bDragging  = true;
        return true;
    }
    if (eventType == EEvent::MouseMoved) {
        if (!_bDragging) {
            return false;
        }
        int x = 0;
        int y = 0;
        if (!pointToTile(ctx.logicalPoint, x, y)) {
            return true;
        }
        _selX = std::min(_anchorX, x);
        _selY = std::min(_anchorY, y);
        _selW = std::abs(x - _anchorX) + 1;
        _selH = std::abs(y - _anchorY) + 1;
        return true;
    }
    if (!_bDragging) {
        return false;
    }
    _bDragging = false;
    if (_onStamp) {
        FTileStamp stamp;
        stamp.width  = _selW;
        stamp.height = _selH;
        stamp.values.resize(static_cast<size_t>(_selW) * static_cast<size_t>(_selH));
        for (int sy = 0; sy < _selH; ++sy) {
            for (int sx = 0; sx < _selW; ++sx) {
                stamp.values[static_cast<size_t>(sy) * static_cast<size_t>(_selW) + static_cast<size_t>(sx)] =
                    static_cast<int32_t>((_selY + sy) * _columns + (_selX + sx) + 1);
            }
        }
        _onStamp(std::move(stamp));
    }
    return true;
}

EditorTilePaletteTab::EditorTilePaletteTab(EditorLayer& layer)
    : UICompoundWidget("TilePaletteBody", "panel"), _layer(layer)
{
    enableTick();
}

void EditorTilePaletteTab::construct()
{
    auto tools = ui::row("TilePaletteTools").setSpacing(editor_density::kControlSpacing);
    for (int i = 0; i < 5; ++i) {
        const ETileBrushTool tool = kTileTools[i];
        auto row = ui::selectableRow(std::format("TilePaletteTool{}", i))
                       .setItemId(tileToolName(tool))
                       .setSelected(tool == ETileBrushTool::Move)
                       .setOnSelect([this, tool](const std::string&) {
                           _layer.tileBrush().setTool(tool);
                           syncToolRows();
                       })
                       .child(ui::text(std::format("TilePaletteTool{}Label", i))
                                  .setText(tileToolName(tool))
                                  .setStyleKey("text.small")
                                  .setHAlign(EWidgetAlignH::Center)
                                  .setVAlign(EWidgetAlignV::Center),
                              ui::contentSlot().align(EUIOverlayAlignment::Center,
                                                      EUIOverlayAlignment::Center))
                       .share();
        _toolRows[i] = row;
        tools.child(row, ui::boxSlot().preferredSize({52.0f, 28.0f}));
    }

    auto layerCombo = ui::comboBox("TilePaletteLayerCombo")
                          .setStyleKey(editorStyle(StyleKey::ComboBox))
                          .setOnSelectionChanged([this](int index) {
                              _layer.tileBrush().setLayer(index < 0 ? 0 : static_cast<size_t>(index));
                          })
                          .share();
    _layerCombo = layerCombo;

    auto stamp = ui::text("TilePaletteStamp")
                     .setText("No tilemap selected")
                     .setStyleKey("text.muted")
                     .setWrap(true)
                     .share();
    _stampText = stamp;

    auto info = ui::text("TilePaletteInfo")
                    .setText("")
                    .setStyleKey("text.muted")
                    .setWrap(true)
                    .share();
    _infoText = info;

    auto grid = std::make_shared<UITileAtlasGrid>();
    grid->setOnStamp([this](FTileStamp stamp) {
        _layer.tileBrush().setStamp(std::move(stamp));
        refresh();
    });
    _grid = grid;

    auto frame = ui::sizeBox("TilePaletteGridFrame")
                     .setWidth(384.0f)
                     .setHeight(352.0f)
                     .child(grid, ui::contentSlot().fill())
                     .share();
    _gridFrame = frame;

    auto scroll = ui::scroll("TilePaletteScroll")
                      .setAxis(EScrollAxis::Vertical)
                      .child(ui::column("TilePaletteScrollContent")
                                 .setSpacing(0.0f)
                                 .child(frame, ui::boxSlot().autoSize().crossAlign(EUIBoxSlotCrossAlignment::Start))
                                 .release(),
                             ui::contentSlot()
                                 .hAlign(EUIOverlayAlignment::Start)
                                 .vAlign(EUIOverlayAlignment::Start))
                      .share();

    auto well = ui::overlay("TilePaletteWell")
                    .child(ui::border("TilePaletteWellFill")
                               .setStyleKey("panel.canvas")
                               .setVisibility(EWidgetVisibility::HitTestInvisible),
                           ui::overlaySlot().fill())
                    .child(scroll, ui::overlaySlot().fill())
                    .release();

    addDetachedChild(ui::column("TilePaletteRoot")
                         .setSpacing(editor_density::kSectionSpacing)
                         .setPadding({12.0f, 12.0f})
                         .setStretchLastChild(true)
                         .child(tools.release(), ui::boxSlot().preferredSize({0.0f, 28.0f}))
                         .child(layerCombo, ui::boxSlot().preferredSize({0.0f, editor_density::kToolbarHeight}))
                         .child(stamp)
                         .child(info)
                         .child(well, ui::boxSlot().fill())
                         .release());
}

void EditorTilePaletteTab::onAttached()
{
    refresh();
}

void EditorTilePaletteTab::tick(float)
{
    refresh();
}

void EditorTilePaletteTab::syncToolRows()
{
    const ETileBrushTool active = _layer.tileBrush().tool();
    for (int i = 0; i < 5; ++i) {
        if (_toolRows[i]) {
            _toolRows[i]->setSelected(kTileTools[i] == active);
        }
    }
}

void EditorTilePaletteTab::syncLayerCombo(const TilemapComponent& map)
{
    if (!_layerCombo) {
        return;
    }
    std::string names;
    std::vector<std::string> items;
    for (const TilemapLayer& layer : map.layers) {
        items.push_back(layer.name.empty() ? "(unnamed)" : layer.name);
        names += items.back();
        names += '|';
    }
    if (names != _layerNames) {
        _layerNames       = names;
        _layerCombo->_items = items;
        _layerCombo->markPaintDirty();
    }
    const int active = static_cast<int>(std::min(_layer.tileBrush().layer(), map.layers.empty() ? 0 : map.layers.size() - 1));
    _layerCombo->setSelectedIndex(map.layers.empty() ? -1 : active, false);
}

void EditorTilePaletteTab::refresh()
{
    if (!_stampText || !_infoText || !_grid || !_gridFrame || !_layerCombo) {
        return;
    }
    syncToolRows();

    Entity* entity = _layer.getSelectedEntity();
    TilemapComponent* map =
        (entity && entity->isValid() && entity->hasComponent<TilemapComponent>())
            ? entity->getComponent<TilemapComponent>()
            : nullptr;
    if (!map || !map->isValid()) {
        if (_fingerprint != "none") {
            _fingerprint.clear();
            _fingerprint = "none";
            _stampText->setText("Select a tilemap entity to paint");
            _infoText->setText("");
            _grid->setAssetPath({});
            _grid->setResourceMissing(false);
        }
        return;
    }

    Tileset* tileset = map->tileset.get();
    Texture* atlas   = (tileset && tileset->atlas.textureRef.get()) ? tileset->atlas.textureRef.get() : nullptr;
    std::string fingerprint =
        std::to_string(entity->getHandle() == entt::null ? 0 : static_cast<uint32_t>(entity->getHandle())) + '|' +
        map->tileset.getPath() + '|' + std::to_string(map->layers.size()) + '|' +
        std::to_string(reinterpret_cast<uintptr_t>(atlas)) + '|' +
        std::to_string(static_cast<int>(_layer.tileBrush().tool())) + '|' +
        std::to_string(_layer.tileBrush().layer()) + '|' +
        std::to_string(_layer.tileBrush().stamp().values.size()) + ':' +
        std::to_string(_layer.tileBrush().stamp().values.empty() ? 0 : _layer.tileBrush().stamp().values[0]);
    if (fingerprint != _fingerprint) {
        _fingerprint = fingerprint;
        bindAtlas();
    }
    syncLayerCombo(*map);
}

void EditorTilePaletteTab::bindAtlas()
{
    Entity* entity = _layer.getSelectedEntity();
    TilemapComponent* map =
        (entity && entity->isValid() && entity->hasComponent<TilemapComponent>())
            ? entity->getComponent<TilemapComponent>()
            : nullptr;
    if (!map) {
        return;
    }
    Tileset* tileset = map->tileset.get();
    if (!tileset) {
        _stampText->setText("Tileset not loaded: check the tileset path in the Inspector");
        _infoText->setText(map->tileset.getPath());
        _grid->setAssetPath({});
        _grid->setResourceMissing(true);
        return;
    }
    Texture* atlas = tileset->atlas.textureRef.get();
    const int columns = std::max(1, tileset->columns);
    const int rows = (atlas && tileset->tileHeight > 0)
                         ? std::max(1, static_cast<int>(atlas->getHeight()) / tileset->tileHeight)
                         : 1;
    _grid->setGrid(columns, rows, tileset->tileWidth, tileset->tileHeight, 2.0f);
    if (atlas) {
        _gridFrame->setWidthOverride(static_cast<float>(atlas->getWidth()) * 2.0f);
        _gridFrame->setHeightOverride(static_cast<float>(atlas->getHeight()) * 2.0f);
    }
    _grid->setAssetPath(tileset->atlas.textureRef.getPath());
    _grid->setResourceMissing(atlas == nullptr);

    const FTileStamp& stamp = _layer.tileBrush().stamp();
    const int first = stamp.values.empty() ? 1 : stamp.values[0];
    _stampText->setText(std::format("Stamp {}x{} starting at tile {} (layer {})", stamp.width, stamp.height,
                                        first - 1, _layer.tileBrush().layer()));
    _infoText->setText(
        std::format("{}x{} px tiles, {} columns, {} rows", tileset->tileWidth, tileset->tileHeight, columns, rows));
    const int sx = (first - 1) % columns;
    const int sy = (first - 1) / columns;
    _grid->setSelected(sx, sy, stamp.width, stamp.height);
}

} // namespace ya
