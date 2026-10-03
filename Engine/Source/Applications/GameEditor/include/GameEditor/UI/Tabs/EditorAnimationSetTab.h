#pragma once

#include "GameEditor/Animation/SpriteAnimationSetEditModel.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/Controls/Image.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct UICheckBox;
struct UISizeBox;
struct UIText;
struct UITextField;
struct WidgetTree;
struct EditorLayer;

/// Sheet grid over the atlas image. Cells are `columns` by `rows`, index
/// `row * columns + column` from the top-left, the same order as
/// `SpriteAnimationSet::frameRect`. A click appends that frame.
class YA_GAME_EDITOR_API UISpriteAnimAtlasGrid : public UIImage
{
    int                             _columns = 1;
    int                             _rows    = 1;
    std::vector<int32_t>            _highlighted;
    std::function<void(int32_t)>    _onCell;

  public:
    UISpriteAnimAtlasGrid();

    void setGrid(int columns, int rows);
    void setHighlighted(std::vector<int32_t> frames);
    void setOnCell(std::function<void(int32_t)> onCell) { _onCell = std::move(onCell); }

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;

  private:
    [[nodiscard]] bool pointToCell(const glm::vec2& logicalPoint, int& outColumn, int& outRow) const;
};

/// One frame of the current clip, taken from the atlas UV window.
class YA_GAME_EDITOR_API UISpriteAnimPreview : public UIElement
{
    std::string _assetPath;
    glm::vec4   _frame{0.0f, 0.0f, 1.0f, 1.0f};

  public:
    UISpriteAnimPreview();

    void setAssetPath(std::string path);
    void setFrameRect(const glm::vec4& frame);

    void paintSelf(UIFrameBuilder& builder) override;
};

/// Animation-set dock tab. The document lives in `SpriteAnimationSetEditModel`;
/// this widget only presents it.
class YA_GAME_EDITOR_API EditorAnimationSetTab : public UICompoundWidget
{
  public:
    explicit EditorAnimationSetTab(EditorLayer& layer);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer&                 _layer;
    SpriteAnimationSetEditModel  _model;

    std::shared_ptr<UIText>               _pathText;
    std::shared_ptr<UIText>               _dirtyText;
    std::shared_ptr<UIText>               _errorText;
    std::shared_ptr<UIText>               _noticeText;
    std::shared_ptr<UIText>               _atlasHint;
    std::shared_ptr<UIText>               _playLabel;
    std::shared_ptr<UITextField>          _columnsField;
    std::shared_ptr<UITextField>          _rowsField;
    std::shared_ptr<UITextField>          _fpsField;
    std::shared_ptr<UITextField>          _renameField;
    std::shared_ptr<UICheckBox>           _loopBox;
    std::shared_ptr<UISizeBox>            _gridFrame;
    std::shared_ptr<UISpriteAnimAtlasGrid> _grid;
    std::shared_ptr<UISpriteAnimPreview>  _preview;
    std::shared_ptr<UIElement>            _clipHost;
    std::shared_ptr<UIElement>            _frameHost;

    std::string _clipFingerprint;
    std::string _frameFingerprint;
    int         _playClip  = -1;
    int         _playFrame = 0;
    float       _playTime  = 0.0f;
    bool        _bPlaying  = false;

    void applyPendingOpen();
    void refresh();
    void rebuildClipList(WidgetTree& tree);
    void rebuildFrameList(WidgetTree& tree);
    void syncField(UITextField& field, const std::string& text);
    void resetPlayback();
    void advancePlayback(float deltaSeconds);
};

} // namespace ya
