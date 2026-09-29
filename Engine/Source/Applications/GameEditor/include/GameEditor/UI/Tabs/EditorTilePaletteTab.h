#pragma once

#include "GameEditor/UI/Viewport/EditorTileBrushController.h"
#include "ECS/Component/2D/TilemapComponent.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/Controls/Image.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct UIComboBox;
struct UISelectableRow;
struct UISizeBox;
struct UIText;
struct EditorLayer;

// Click/drag tile picker over the atlas image: press selects one tile,
// drag selects a stamp box. Tile math uses the same top-left row-major
// indexing as Tileset (tile = y * columns + x).
class UITileAtlasGrid : public UIImage
{
  public:
    UITileAtlasGrid();

    void setGrid(int columns, int rows, int tileW, int tileH, float scale);
    void setSelected(int x, int y, int w, int h);
    void setOnStamp(std::function<void(FTileStamp)> onStamp) { _onStamp = std::move(onStamp); }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;

  private:
    [[nodiscard]] bool pointToTile(const glm::vec2& logicalPoint, int& outX, int& outY) const;

    int _columns = 1;
    int _rows    = 1;
    int _tileW   = 16;
    int _tileH   = 16;
    float _scale = 2.0f;
    int _selX = 0;
    int _selY = 0;
    int _selW = 1;
    int _selH = 1;
    int _anchorX = 0;
    int _anchorY = 0;
    bool _bDragging = false;
    std::function<void(FTileStamp)> _onStamp;
};

// Tile Palette dock tab: tool buttons, layer picker and atlas stamp picker
// driving the viewport brush on EditorLayer. Empty hint when no tilemap is
// selected; never invents a map.
class EditorTilePaletteTab : public UICompoundWidget
{
  public:
    explicit EditorTilePaletteTab(EditorLayer& layer);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer& _layer;

    std::shared_ptr<UISelectableRow> _toolRows[5]{};
    std::shared_ptr<UIComboBox>      _layerCombo;
    std::shared_ptr<UIText>          _stampText;
    std::shared_ptr<UIText>          _infoText;
    std::shared_ptr<UITileAtlasGrid> _grid;
    std::shared_ptr<UISizeBox>       _gridFrame;
    std::string                      _fingerprint;
    std::string                      _layerNames;

    void refresh();
    void bindAtlas();
    void syncToolRows();
    void syncLayerCombo(const TilemapComponent& map);
};

} // namespace ya
