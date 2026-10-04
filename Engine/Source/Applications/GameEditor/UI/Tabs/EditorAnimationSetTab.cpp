#include "GameEditor/UI/Tabs/EditorAnimationSetTab.h"

#include "Core/Common/AssetRef.h"
#include "Core/Event.h"
#include "Core/Log.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Brush.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/GuiTextureCatalog.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <utility>

namespace ya
{
namespace
{

constexpr float kGridWidth    = 240.0f;
constexpr float kGridMaxHeight = 220.0f;
constexpr glm::vec4 kErrorColor{1.0f, 0.45f, 0.4f, 1.0f};
constexpr glm::vec4 kMutedColor{0.65f, 0.67f, 0.7f, 1.0f};

[[nodiscard]] bool parseWholeInt(const std::string& text, int& out)
{
    if (text.empty()) {
        return false;
    }
    char*             end   = nullptr;
    const long        value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

[[nodiscard]] bool parsePositiveFloat(const std::string& text, float& out)
{
    if (text.empty()) {
        return false;
    }
    char*       end   = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    out = value;
    return true;
}

[[nodiscard]] std::string formatFps(float fps)
{
    return std::format("{:.4g}", fps);
}

void clearChildren(WidgetTree& tree, UIElement& host)
{
    for (const auto& child : std::vector<UIElementRef>(host.getChildren())) {
        if (child && child->isAttached()) {
            tree.detach(*child);
        }
    }
}

} // namespace

UISpriteAnimAtlasGrid::UISpriteAnimAtlasGrid()
    : UIImage("AnimAtlasGrid")
{
    setScaleMode(EImageScaleMode::Stretch);
}

void UISpriteAnimAtlasGrid::setGrid(int columns, int rows)
{
    columns = std::max(1, columns);
    rows    = std::max(1, rows);
    if (_columns == columns && _rows == rows) {
        return;
    }
    _columns = columns;
    _rows    = rows;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UISpriteAnimAtlasGrid::setHighlighted(std::vector<int32_t> frames)
{
    if (_highlighted == frames) {
        return;
    }
    _highlighted = std::move(frames);
    invalidateProperty(EUIPropertyImpact::Paint);
}

bool UISpriteAnimAtlasGrid::pointToCell(const glm::vec2& logicalPoint, int& outColumn, int& outRow) const
{
    const Rect2D& rect = getLayoutRect();
    if (_columns < 1 || _rows < 1 || rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
        return false;
    }
    const glm::vec2 local = logicalPoint - rect.pos;
    const float     cellW = rect.extent.x / static_cast<float>(_columns);
    const float     cellH = rect.extent.y / static_cast<float>(_rows);
    if (cellW <= 0.0f || cellH <= 0.0f) {
        return false;
    }
    const int column = static_cast<int>(std::floor(local.x / cellW));
    const int row    = static_cast<int>(std::floor(local.y / cellH));
    if (column < 0 || row < 0 || column >= _columns || row >= _rows) {
        return false;
    }
    outColumn = column;
    outRow    = row;
    return true;
}

void UISpriteAnimAtlasGrid::paintSelf(UIFrameBuilder& builder)
{
    UIImage::paintSelf(builder);
    const Rect2D& rect = getLayoutRect();
    if (_columns < 1 || _rows < 1 || rect.extent.x <= 1.0f || rect.extent.y <= 1.0f) {
        return;
    }
    const float cellW = rect.extent.x / static_cast<float>(_columns);
    const float cellH = rect.extent.y / static_cast<float>(_rows);
    const glm::vec4 highlight{0.95f, 0.72f, 0.2f, 0.38f};
    for (int32_t frame : _highlighted) {
        int32_t column = 0;
        int32_t row    = 0;
        if (!SpriteAnimationSetEditModel::frameToCell(frame, _columns, _rows, column, row)) {
            continue;
        }
        const Rect2D cell{
            .pos    = {rect.pos.x + static_cast<float>(column) * cellW, rect.pos.y + static_cast<float>(row) * cellH},
            .extent = {cellW, cellH},
        };
        builder.addBrush(cell, FBrush::solid(highlight));
    }

    const glm::vec4 line{1.0f, 1.0f, 1.0f, 0.9f};
    for (int column = 0; column <= _columns; ++column) {
        const float x = rect.pos.x + static_cast<float>(column) * cellW;
        builder.addLine({x, rect.pos.y}, {x, rect.pos.y + rect.extent.y}, line, 1.0f);
    }
    for (int row = 0; row <= _rows; ++row) {
        const float y = rect.pos.y + static_cast<float>(row) * cellH;
        builder.addLine({rect.pos.x, y}, {rect.pos.x + rect.extent.x, y}, line, 1.0f);
    }

    const std::shared_ptr<Font> font = builder.getFont(FName(DEFAULT_RUNTIME_FONT_NAME), 11);
    if (!font) {
        return;
    }
    const glm::vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
    for (int row = 0; row < _rows; ++row) {
        for (int column = 0; column < _columns; ++column) {
            const Rect2D cell{
                .pos    = {rect.pos.x + static_cast<float>(column) * cellW,
                           rect.pos.y + static_cast<float>(row) * cellH},
                .extent = {cellW, cellH},
            };
            builder.addText(cell, std::to_string(row * _columns + column), textColor, font, EWidgetAlignH::Center,
                            EWidgetAlignV::Center);
        }
    }
}

bool UISpriteAnimAtlasGrid::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    if (event.getEventType() != EEvent::MouseButtonPressed) {
        return false;
    }
    const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
    if (press.GetMouseButton() != EMouse::Left || !hitTestLayoutRect(ctx.logicalPoint)) {
        return false;
    }
    int column = 0;
    int row    = 0;
    if (!pointToCell(ctx.logicalPoint, column, row)) {
        return false;
    }
    if (_onCell) {
        _onCell(static_cast<int32_t>(row * _columns + column));
    }
    return true;
}

UISpriteAnimPreview::UISpriteAnimPreview()
    : UIElement("AnimPreview", "image")
{
}

void UISpriteAnimPreview::setAssetPath(std::string path)
{
    if (_assetPath == path) {
        return;
    }
    _assetPath = std::move(path);
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UISpriteAnimPreview::setFrameRect(const glm::vec4& frame)
{
    if (_frame == frame) {
        return;
    }
    _frame = frame;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UISpriteAnimPreview::paintSelf(UIFrameBuilder& builder)
{
    const glm::vec4 placeholder{0.16f, 0.17f, 0.19f, 1.0f};
    if (_assetPath.empty()) {
        builder.addBrush(getLayoutRect(), FBrush::solid(placeholder));
        return;
    }
    const FGuiTextureLookup lookup = builder.resolveTextureLookup(_assetPath);
    if (lookup.state != EGuiTextureState::Ready || !lookup.texture) {
        builder.addBrush(getLayoutRect(), FBrush::solid(placeholder));
        return;
    }
    const glm::vec2 uvOffset{_frame.x, _frame.y};
    const glm::vec2 uvScale{_frame.z - _frame.x, _frame.w - _frame.y};
    if (uvScale.x <= 0.0f || uvScale.y <= 0.0f) {
        builder.addBrush(getLayoutRect(), FBrush::solid(placeholder));
        return;
    }
    builder.addSprite(getLayoutRect(), {1.0f, 1.0f, 1.0f, 1.0f}, lookup.texture, uvOffset, uvScale);
}

EditorAnimationSetTab::EditorAnimationSetTab(EditorLayer& layer)
    : UICompoundWidget("AnimSetBody", "panel"), _layer(layer)
{
    enableTick();
}

void EditorAnimationSetTab::construct()
{
    _pathText = ui::text("AnimPath").setText("").setStyleKey("text.small").setWrap(true).share();
    _dirtyText = ui::text("AnimDirty").setText("").setStyleKey("text.small").share();
    _errorText = ui::text("AnimError").setText("").setStyleKey("text.small").setWrap(true).share();
    _noticeText = ui::text("AnimNotice").setText("").setStyleKey("text.muted").setWrap(true).share();
    _atlasHint = ui::text("AnimAtlasHint")
                     .setText("Empty atlas: sprites use their own Sprite2D texture.")
                     .setStyleKey("text.muted")
                     .setWrap(true)
                     .share();

    _columnsField = ui::textField("AnimColumns")
                        .setStyleKey(editorStyle(StyleKey::TextField))
                        .setOnCommit([this](const std::string& text) {
                            int value = 0;
                            if (!parseWholeInt(text, value) || !_model.setColumns(value)) {
                                syncField(*_columnsField, std::to_string(_model.document().columns));
                            }
                        })
                        .share();
    _rowsField = ui::textField("AnimRows")
                     .setStyleKey(editorStyle(StyleKey::TextField))
                     .setOnCommit([this](const std::string& text) {
                         int value = 0;
                         if (!parseWholeInt(text, value) || !_model.setRows(value)) {
                             syncField(*_rowsField, std::to_string(_model.document().rows));
                         }
                     })
                     .share();
    _fpsField = ui::textField("AnimFps")
                    .setStyleKey(editorStyle(StyleKey::TextField))
                    .setOnCommit([this](const std::string& text) {
                        float value = 0.0f;
                        if (!parsePositiveFloat(text, value) || !_model.setFps(_model.selectedClip(), value)) {
                            const int clip = _model.selectedClip();
                            if (clip >= 0) {
                                syncField(*_fpsField, formatFps(_model.document().clips[static_cast<size_t>(clip)].fps));
                            }
                        }
                    })
                    .share();
    _renameField = ui::textField("AnimRename")
                       .setStyleKey(editorStyle(StyleKey::TextField))
                       .setOnCommit([this](const std::string& text) {
                           if (!_model.renameClip(_model.selectedClip(), text)) {
                               const int clip = _model.selectedClip();
                               if (clip >= 0) {
                                   syncField(*_renameField, _model.document().clips[static_cast<size_t>(clip)].name);
                               }
                           }
                           _clipFingerprint.clear();
                       })
                       .share();
    _loopBox = ui::checkBox("AnimLoop")
                   .setText("Loop")
                   .setOnChanged([this](bool checked) { (void)_model.setLoop(_model.selectedClip(), checked); })
                   .share();

    _grid = std::make_shared<UISpriteAnimAtlasGrid>();
    _grid->setOnCell([this](int32_t frame) { (void)_model.appendFrame(_model.selectedClip(), frame); });
    _gridFrame = ui::sizeBox("AnimGridFrame").setWidth(kGridWidth).setHeight(kGridWidth).child(_grid, ui::contentSlot().fill()).share();

    _preview = std::make_shared<UISpriteAnimPreview>();
    auto previewFrame = ui::sizeBox("AnimPreviewFrame").setWidth(72.0f).setHeight(72.0f).child(_preview, ui::contentSlot().fill()).release();

    _clipHost = ui::column("AnimClipList").setSpacing(2.0f).share();
    _frameHost = ui::column("AnimFrameList").setSpacing(2.0f).share();

    auto playLabel = ui::text("AnimPlayLabel")
                         .setText("Play")
                         .setStyleKey("text.small")
                         .setHAlign(EWidgetAlignH::Center)
                         .setVAlign(EWidgetAlignV::Center)
                         .share();
    _playLabel = playLabel;

    auto header = ui::row("AnimHeader")
                      .setSpacing(editor_density::kControlSpacing)
                      .child(_dirtyText)
                      .child(ui::button("AnimSave")
                                 .setOnClick([this]() {
                                     if (_model.save()) {
                                         _layer.setAnimationSetNotice({});
                                     }
                                 })
                                 .child(ui::text("AnimSaveLabel")
                                            .setText("Save")
                                            .setStyleKey("text.small")
                                            .setHAlign(EWidgetAlignH::Center)
                                            .setVAlign(EWidgetAlignV::Center))
                                 .release(),
                             ui::boxSlot().preferredSize({64.0f, editor_density::kToolbarHeight}))
                      .child(ui::button("AnimRevert")
                                 .setOnClick([this]() {
                                     _model.revert();
                                     _clipFingerprint.clear();
                                     _frameFingerprint.clear();
                                     resetPlayback();
                                 })
                                 .child(ui::text("AnimRevertLabel")
                                            .setText("Revert")
                                            .setStyleKey("text.small")
                                            .setHAlign(EWidgetAlignH::Center)
                                            .setVAlign(EWidgetAlignV::Center))
                                 .release(),
                             ui::boxSlot().preferredSize({72.0f, editor_density::kToolbarHeight}))
                      .release();

    auto gridRow = ui::row("AnimGridFields")
                       .setSpacing(editor_density::kControlSpacing)
                       .child(ui::text("AnimColumnsLabel").setText("Columns").setStyleKey("text.small").release())
                       .child(_columnsField, ui::boxSlot().preferredSize({64.0f, editor_density::kToolbarHeight}))
                       .child(ui::text("AnimRowsLabel").setText("Rows").setStyleKey("text.small").release())
                       .child(_rowsField, ui::boxSlot().preferredSize({64.0f, editor_density::kToolbarHeight}))
                       .child(ui::button("AnimPickAtlas")
                                  .setOnClick([this]() {
                                      if (!_layer._assetPickerHandler) {
                                          return;
                                      }
                                      _layer._assetPickerHandler(
                                          ya::type_index_v<TextureRef>, _model.document().atlas,
                                          [this](std::string path) {
                                              std::string canonical = SpriteAnimationSetEditModel::canonicalAssetPath(path);
                                              if (canonical.empty() || std::filesystem::path(canonical).is_absolute()) {
                                                  canonical = std::move(path);
                                              }
                                              (void)_model.setAtlas(std::move(canonical));
                                          });
                                  })
                                  .child(ui::text("AnimPickAtlasLabel")
                                             .setText("Pick Atlas")
                                             .setStyleKey("text.small")
                                             .setHAlign(EWidgetAlignH::Center)
                                             .setVAlign(EWidgetAlignV::Center))
                                  .release(),
                              ui::boxSlot().preferredSize({96.0f, editor_density::kToolbarHeight}))
                       .release();

    auto clipButtons = ui::row("AnimClipButtons")
                           .setSpacing(editor_density::kControlSpacing)
                           .child(ui::button("AnimAddClip")
                                      .setOnClick([this]() {
                                          (void)_model.addClip();
                                          resetPlayback();
                                          _clipFingerprint.clear();
                                      })
                                      .child(ui::text("AnimAddClipLabel")
                                                 .setText("Add Clip")
                                                 .setStyleKey("text.small")
                                                 .setHAlign(EWidgetAlignH::Center)
                                                 .setVAlign(EWidgetAlignV::Center))
                                      .release(),
                                  ui::boxSlot().preferredSize({80.0f, editor_density::kToolbarHeight}))
                           .child(ui::button("AnimRemoveClip")
                                      .setOnClick([this]() {
                                          (void)_model.removeClip(_model.selectedClip());
                                          resetPlayback();
                                          _clipFingerprint.clear();
                                          _frameFingerprint.clear();
                                      })
                                      .child(ui::text("AnimRemoveClipLabel")
                                                 .setText("Delete Clip")
                                                 .setStyleKey("text.small")
                                                 .setHAlign(EWidgetAlignH::Center)
                                                 .setVAlign(EWidgetAlignV::Center))
                                      .release(),
                                  ui::boxSlot().preferredSize({96.0f, editor_density::kToolbarHeight}))
                           .release();

    auto playback = ui::row("AnimPlayback")
                        .setSpacing(editor_density::kControlSpacing)
                        .child(ui::text("AnimFpsLabel").setText("FPS").setStyleKey("text.small").release())
                        .child(_fpsField, ui::boxSlot().preferredSize({72.0f, editor_density::kToolbarHeight}))
                        .child(_loopBox)
                        .child(ui::button("AnimPlay")
                                   .setOnClick([this]() { _bPlaying = !_bPlaying; })
                                   .child(playLabel)
                                   .release(),
                               ui::boxSlot().preferredSize({72.0f, editor_density::kToolbarHeight}))
                        .child(previewFrame)
                        .release();

    auto body = ui::column("AnimScrollContent")
                    .setSpacing(editor_density::kSectionSpacing)
                    .child(_pathText)
                    .child(header)
                    .child(_errorText)
                    .child(_noticeText)
                    .child(gridRow)
                    .child(_atlasHint)
                    .child(playback)
                    .child(_gridFrame, ui::boxSlot().autoSize().crossAlign(EUIBoxSlotCrossAlignment::Start))
                    .child(ui::text("AnimClipHeading").setText("Clips").setStyleKey("text.small").release())
                    .child(clipButtons)
                    .child(_clipHost)
                    .child(_renameField, ui::boxSlot().preferredSize({0.0f, editor_density::kToolbarHeight}))
                    .child(ui::text("AnimFrameHeading").setText("Frames").setStyleKey("text.small").release())
                    .child(_frameHost)
                    .release();

    auto scroll = ui::scroll("AnimScroll")
                      .setAxis(EScrollAxis::Vertical)
                      .child(body, ui::contentSlot().hAlign(EUIOverlayAlignment::Start).vAlign(EUIOverlayAlignment::Start))
                      .release();

    addDetachedChild(ui::column("AnimRoot")
                         .setSpacing(0.0f)
                         .setPadding({12.0f, 12.0f})
                         .setStretchLastChild(true)
                         .child(scroll, ui::boxSlot().fill())
                         .release());
}

void EditorAnimationSetTab::onAttached()
{
    applyPendingOpen();
    refresh();
}

void EditorAnimationSetTab::tick(float deltaSeconds)
{
    UIElement::tick(deltaSeconds);
    applyPendingOpen();
    advancePlayback(deltaSeconds);
    refresh();
}

void EditorAnimationSetTab::applyPendingOpen()
{
    const std::string& pending = _layer.pendingAnimationSetPath();
    if (pending.empty()) {
        return;
    }
    if (!_model.assetPath().empty() && pending == _model.assetPath()) {
        _layer.clearPendingAnimationSetPath();
        _layer.setAnimationSetNotice({});
        return;
    }
    if (_model.dirty()) {
        _layer.setAnimationSetNotice("Unsaved changes. Save or Revert before opening another animation set.");
        return;
    }
    const std::string path = pending;
    _layer.clearPendingAnimationSetPath();
    if (_model.loadFromPath(path)) {
        _layer.setAnimationSetNotice({});
        resetPlayback();
        _clipFingerprint.clear();
        _frameFingerprint.clear();
        return;
    }
    _layer.setAnimationSetNotice(_model.error().empty() ? "Failed to open animation set" : _model.error());
}

void EditorAnimationSetTab::resetPlayback()
{
    _playClip  = _model.selectedClip();
    _playFrame = 0;
    _playTime  = 0.0f;
    _bPlaying  = false;
}

void EditorAnimationSetTab::advancePlayback(float deltaSeconds)
{
    const int clipIndex = _model.selectedClip();
    if (clipIndex != _playClip) {
        resetPlayback();
    }
    if (!_bPlaying || clipIndex < 0) {
        return;
    }
    const SpriteAnimationSet& doc = _model.document();
    if (clipIndex >= static_cast<int>(doc.clips.size())) {
        return;
    }
    const SpriteAnimationClip& clip = doc.clips[static_cast<size_t>(clipIndex)];
    if (clip.frames.empty() || !(clip.fps > 0.0f)) {
        return;
    }
    _playTime += std::min(deltaSeconds, 0.1f);
    const float step = 1.0f / clip.fps;
    while (_playTime >= step) {
        _playTime -= step;
        ++_playFrame;
        if (_playFrame >= static_cast<int>(clip.frames.size())) {
            if (clip.bLoop) {
                _playFrame = 0;
            }
            else {
                _playFrame = static_cast<int>(clip.frames.size()) - 1;
                _bPlaying  = false;
                break;
            }
        }
    }
}

void EditorAnimationSetTab::syncField(UITextField& field, const std::string& text)
{
    if (WidgetTree* tree = getTree(); tree && tree->getFocused() == &field) {
        return;
    }
    field.setText(text);
}

void EditorAnimationSetTab::rebuildClipList(WidgetTree& tree)
{
    if (!_clipHost) {
        return;
    }
    clearChildren(tree, *_clipHost);
    const SpriteAnimationSet& doc = _model.document();
    for (int index = 0; index < static_cast<int>(doc.clips.size()); ++index) {
        auto row = ui::selectableRow(std::format("AnimClip{}", index))
                       .setItemId(doc.clips[static_cast<size_t>(index)].name)
                       .setSelected(index == _model.selectedClip())
                       .setOnSelect([this, index](const std::string&) {
                           _model.selectClip(index);
                           _clipFingerprint.clear();
                           _frameFingerprint.clear();
                       })
                       .child(ui::text(std::format("AnimClip{}Label", index))
                                  .setText(doc.clips[static_cast<size_t>(index)].name)
                                  .setStyleKey("text.small")
                                  .setVAlign(EWidgetAlignV::Center))
                       .share();
        tree.attach(*_clipHost, row);
    }
}

void EditorAnimationSetTab::rebuildFrameList(WidgetTree& tree)
{
    if (!_frameHost) {
        return;
    }
    clearChildren(tree, *_frameHost);
    const int clipIndex = _model.selectedClip();
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_model.document().clips.size())) {
        return;
    }
    const auto& frames = _model.document().clips[static_cast<size_t>(clipIndex)].frames;
    for (int index = 0; index < static_cast<int>(frames.size()); ++index) {
        const int32_t frame = frames[static_cast<size_t>(index)];
        int32_t       column = 0;
        int32_t       row    = 0;
        const bool    hit = SpriteAnimationSetEditModel::frameToCell(frame, _model.document().columns,
                                                                   _model.document().rows, column, row);
        const std::string label = hit ? std::format("{}  ({}, {})", frame, column, row) : std::to_string(frame);
        auto line = ui::row(std::format("AnimFrame{}", index))
                        .setSpacing(editor_density::kControlSpacing)
                        .child(ui::text(std::format("AnimFrame{}Label", index)).setText(label).setStyleKey("text.small").release())
                        .child(ui::button(std::format("AnimFrame{}Up", index))
                                   .setOnClick([this, clipIndex, index]() {
                                       (void)_model.moveFrame(clipIndex, index, -1);
                                       _frameFingerprint.clear();
                                   })
                                   .child(ui::text(std::format("AnimFrame{}UpLabel", index))
                                              .setText("Up")
                                              .setStyleKey("text.small")
                                              .setHAlign(EWidgetAlignH::Center)
                                              .setVAlign(EWidgetAlignV::Center))
                                   .release(),
                               ui::boxSlot().preferredSize({40.0f, 24.0f}))
                        .child(ui::button(std::format("AnimFrame{}Down", index))
                                   .setOnClick([this, clipIndex, index]() {
                                       (void)_model.moveFrame(clipIndex, index, 1);
                                       _frameFingerprint.clear();
                                   })
                                   .child(ui::text(std::format("AnimFrame{}DownLabel", index))
                                              .setText("Down")
                                              .setStyleKey("text.small")
                                              .setHAlign(EWidgetAlignH::Center)
                                              .setVAlign(EWidgetAlignV::Center))
                                   .release(),
                               ui::boxSlot().preferredSize({52.0f, 24.0f}))
                        .child(ui::button(std::format("AnimFrame{}Remove", index))
                                   .setOnClick([this, clipIndex, index]() {
                                       (void)_model.removeFrame(clipIndex, index);
                                       _frameFingerprint.clear();
                                   })
                                   .child(ui::text(std::format("AnimFrame{}RemoveLabel", index))
                                              .setText("Remove")
                                              .setStyleKey("text.small")
                                              .setHAlign(EWidgetAlignH::Center)
                                              .setVAlign(EWidgetAlignV::Center))
                                   .release(),
                               ui::boxSlot().preferredSize({72.0f, 24.0f}))
                        .share();
        tree.attach(*_frameHost, line);
    }
}

void EditorAnimationSetTab::refresh()
{
    WidgetTree* tree = getTree();
    if (!tree || !_pathText || !_grid || !_preview) {
        return;
    }
    const SpriteAnimationSet& doc = _model.document();
    _pathText->setText(_model.assetPath().empty() ? "(no animation set)" : _model.assetPath());
    _dirtyText->setText(_model.dirty() ? "Unsaved" : "");
    _errorText->setText(_model.error());
    _errorText->setColor(_model.error().empty() ? kMutedColor : kErrorColor);
    _noticeText->setText(_layer.animationSetNotice());

    const bool bHasAtlas = !doc.atlas.empty();
    _atlasHint->setVisibility(bHasAtlas ? EWidgetVisibility::Collapsed : EWidgetVisibility::Visible);
    _gridFrame->setVisibility(bHasAtlas ? EWidgetVisibility::Visible : EWidgetVisibility::Collapsed);
    _grid->setAssetPath(doc.atlas);
    _grid->setGrid(doc.columns, doc.rows);
    if (bHasAtlas && doc.columns > 0) {
        const float height = std::min(kGridMaxHeight,
                                       kGridWidth * static_cast<float>(std::max(1, doc.rows)) /
                                           static_cast<float>(doc.columns));
        _gridFrame->setWidthOverride(kGridWidth);
        _gridFrame->setHeightOverride(height);
    }

    const int clipIndex = _model.selectedClip();
    const SpriteAnimationClip* clip = nullptr;
    if (clipIndex >= 0 && clipIndex < static_cast<int>(doc.clips.size())) {
        clip = &doc.clips[static_cast<size_t>(clipIndex)];
    }
    _grid->setHighlighted(clip ? clip->frames : std::vector<int32_t>{});

    syncField(*_columnsField, std::to_string(doc.columns));
    syncField(*_rowsField, std::to_string(doc.rows));
    if (clip) {
        syncField(*_renameField, clip->name);
        syncField(*_fpsField, formatFps(clip->fps));
        _loopBox->setChecked(clip->bLoop);
    }
    else {
        syncField(*_renameField, "");
        syncField(*_fpsField, "");
        _loopBox->setChecked(false);
    }
    if (_playLabel) {
        _playLabel->setText(_bPlaying ? "Pause" : "Play");
    }

    std::string clipKey;
    for (const SpriteAnimationClip& entry : doc.clips) {
        clipKey += entry.name;
        clipKey.push_back('\n');
    }
    clipKey += std::to_string(clipIndex);
    if (clipKey != _clipFingerprint) {
        _clipFingerprint = std::move(clipKey);
        rebuildClipList(*tree);
    }

    std::string frameKey = std::to_string(clipIndex) + ":";
    if (clip) {
        for (int32_t frame : clip->frames) {
            frameKey += std::to_string(frame);
            frameKey.push_back(',');
        }
    }
    if (frameKey != _frameFingerprint) {
        _frameFingerprint = std::move(frameKey);
        rebuildFrameList(*tree);
    }

    _preview->setAssetPath(doc.atlas);
    glm::vec4 frameRect{0.0f};
    if (clip && !clip->frames.empty()) {
        const int shown = std::clamp(_playFrame, 0, static_cast<int>(clip->frames.size()) - 1);
        frameRect       = doc.frameRect(clip->frames[static_cast<size_t>(shown)]);
    }
    _preview->setFrameRect(frameRect);
}

} // namespace ya
