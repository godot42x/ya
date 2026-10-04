#include "GameEditor/UI/Sections/EditorSpriteAnimationSection.h"

#include "ECS/Entity.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
#include "Scene2D/Sprite2DComponent.h"
#include "Scene2D/SpriteAnimationComponent.h"

#include <algorithm>
#include <string>

namespace ya
{
namespace
{

[[nodiscard]] FBoxSlotArgs animLabelSlot()
{
    return {.preferredSize = {editor_density::kLabelColumn, editor_density::kRowHeight}};
}

[[nodiscard]] FBoxSlotArgs animFillSlot()
{
    return {
        .sizeRule      = EUIBoxSlotSizeRule::Fill,
        .preferredSize = {160.0f, editor_density::kRowHeight},
    };
}

[[nodiscard]] FBoxSlotArgs animFixedSlot(float width)
{
    return {.preferredSize = {width, editor_density::kRowHeight}};
}

[[nodiscard]] std::vector<std::string> animationClipNames(const SpriteAnimationComponent& animation, bool& bLoaded)
{
    bLoaded = false;
    const SpriteAnimationSet* set = animation.animation.get();
    if (!set) {
        return {};
    }
    bLoaded = true;
    std::vector<std::string> names;
    names.reserve(set->clips.size());
    for (const SpriteAnimationClip& clip : set->clips) {
        names.push_back(clip.name);
    }
    return names;
}

} // namespace

EditorSpriteAnimationSection::EditorSpriteAnimationSection(std::string name,
                                                           Resolver resolve,
                                                           UndoStack* undo,
                                                           std::function<void()> onMutated,
                                                           EditorAssetPickerCallback assetPicker,
                                                           EditorRevealAssetCallback revealAsset,
                                                           OpenAsset openAsset,
                                                           bool bReadOnly)
    : UICompoundWidget(std::move(name))
    , _resolve(std::move(resolve))
    , _undo(undo)
    , _onMutated(std::move(onMutated))
    , _assetPicker(std::move(assetPicker))
    , _revealAsset(std::move(revealAsset))
    , _openAsset(std::move(openAsset))
    , _bReadOnly(bReadOnly)
{
    if (!_resolve) {
        return;
    }
    SpriteAnimationComponent* animation = _resolve();
    if (!animation) {
        return;
    }
    _graph = PropertyGraph::project(type_index_v<SpriteAnimationComponent>, {animation});
    if (const PropertyNode* node = _graph.find("animation")) {
        _animation = node->binding;
    }
    if (const PropertyNode* node = _graph.find("clip")) {
        _clip = node->binding;
    }
}

EditorSpriteAnimationSection::~EditorSpriteAnimationSection()
{
    endPreview();
}

void EditorSpriteAnimationSection::noteMutated()
{
    if (_onMutated) {
        _onMutated();
    }
}

void EditorSpriteAnimationSection::restorePreview()
{
    if (!_bHaveSavedUv || !_resolve) {
        _bHaveSavedUv = false;
        return;
    }
    SpriteAnimationComponent* animation = _resolve();
    Entity*                   owner     = animation ? animation->getOwner() : nullptr;
    if (owner) {
        if (Sprite2DComponent* sprite = owner->tryGetComponent<Sprite2DComponent>()) {
            sprite->uvRect = _savedUv;
            if (sprite->image.textureRef.getPath() != _savedImagePath) {
                sprite->image.textureRef.setPath(_savedImagePath);
            }
        }
    }
    _bHaveSavedUv = false;
}

void EditorSpriteAnimationSection::endPreview()
{
    if (!_bPreviewing && !_bHaveSavedUv) {
        return;
    }
    if (_bPreviewing && _resolve) {
        if (SpriteAnimationComponent* animation = _resolve()) {
            animation->stop();
        }
    }
    restorePreview();
    _bPreviewing = false;
}

void EditorSpriteAnimationSection::beginPreview()
{
    if (_bReadOnly || !_resolve) {
        return;
    }
    SpriteAnimationComponent* animation = _resolve();
    if (!animation || animation->clip.empty()) {
        return;
    }
    if (!_bPreviewing) {
        if (Entity* owner = animation->getOwner()) {
            if (Sprite2DComponent* sprite = owner->tryGetComponent<Sprite2DComponent>()) {
                _savedUv        = sprite->uvRect;
                _savedImagePath = sprite->image.textureRef.getPath();
                _bHaveSavedUv   = true;
            }
        }
    }
    if (!animation->play(animation->clip)) {
        restorePreview();
        _bPreviewing = false;
        return;
    }
    _bPreviewing = true;
}

void EditorSpriteAnimationSection::commitClip(const std::string& value)
{
    if (_bReadOnly || !_clip.isValid()) {
        return;
    }
    std::string current;
    if (_clip.tryGet(current) && current == value) {
        return;
    }
    endPreview();
    const std::vector<std::string> before = _clip.copy<std::string>();
    if (before.empty() || !_clip.set(value)) {
        return;
    }
    const std::vector<std::string> after = _clip.copy<std::string>();
    if (_undo) {
        PropertyHandle binding = _clip;
        (void)_undo->push({
            .label = "Set Clip",
            .undo  = [binding, before]() { (void)binding.restore(before); },
            .redo  = [binding, after]() { (void)binding.restore(after); },
        });
    }
    noteMutated();
}

void EditorSpriteAnimationSection::commitAssetPath(const std::string& value)
{
    if (_bReadOnly || !_animation.isValid()) {
        return;
    }
    std::string current;
    if (_animation.tryGetAssetPath(current) && current == value) {
        return;
    }
    endPreview();
    const std::vector<std::string> before = _animation.copyAssetPath();
    if (before.empty() || !_animation.setAssetPath(value)) {
        return;
    }
    const std::vector<std::string> after = _animation.copyAssetPath();
    if (_undo) {
        PropertyHandle binding = _animation;
        (void)_undo->push({
            .label = "Set Animation",
            .undo  = [binding, before]() { (void)binding.restoreAssetPath(before); },
            .redo  = [binding, after]() { (void)binding.restoreAssetPath(after); },
        });
    }
    noteMutated();
}

void EditorSpriteAnimationSection::tickSection(float deltaSeconds)
{
    if (!_bPreviewing || !_resolve) {
        return;
    }
    SpriteAnimationComponent* animation = _resolve();
    if (!animation) {
        endPreview();
        return;
    }
    animation->advance(deltaSeconds);
}

void EditorSpriteAnimationSection::construct()
{
    auto path = std::make_shared<UITextField>("AnimPath");
    path->setStyleKey(editorStyle(StyleKey::TextField));
    path->_onCommit = [this](const std::string& value) { commitAssetPath(value); };
    _path = path;

    auto browse = ui::button("AnimBrowse", "Browse")
                      .setContentPadding({6.0f, 2.0f})
                      .setOnClick([this]() {
                          if (_bReadOnly || !_assetPicker || !_animation.isValid()) {
                              return;
                          }
                          const std::optional<AssetTypeDesc> desc = _animation.assetTypeDesc();
                          if (!desc) {
                              return;
                          }
                          std::string current;
                          if (!_animation.tryGetAssetPath(current)) {
                              return;
                          }
                          _assetPicker(desc->refType, current, [this](std::string picked) {
                              commitAssetPath(std::move(picked));
                          });
                      })
                      .child(ui::text("AnimBrowseLabel").setText("Browse"))
                      .share();
    auto show = ui::button("AnimShow", "Show")
                    .setContentPadding({6.0f, 2.0f})
                    .setOnClick([this]() {
                        if (!_revealAsset) {
                            return;
                        }
                        std::string current;
                        if (!_animation.tryGetAssetPath(current) || current.empty()) {
                            return;
                        }
                        _revealAsset(std::move(current));
                    })
                    .child(ui::text("AnimShowLabel").setText("Show"))
                    .share();
    auto edit = ui::button("AnimEdit", "Edit")
                    .setContentPadding({6.0f, 2.0f})
                    .setOnClick([this]() {
                        if (!_openAsset) {
                            return;
                        }
                        std::string current;
                        if (!_animation.tryGetAssetPath(current) || current.empty()) {
                            return;
                        }
                        _openAsset(std::move(current));
                    })
                    .child(ui::text("AnimEditLabel").setText("Edit"))
                    .share();
    _browse = browse;
    _show   = show;
    _edit   = edit;

    auto clips = std::make_shared<UIComboBox>("AnimClip");
    clips->setStyleKey(editorStyle(StyleKey::ComboBox));
    clips->_onSelectionChanged = [this](int index) {
        if (!_clips || index < 0 || index >= static_cast<int>(_clips->_items.size())) {
            return;
        }
        commitClip(_clips->_items[static_cast<size_t>(index)]);
    };
    _clips = clips;

    auto hint = std::make_shared<UIText>("AnimClipHint");
    hint->setStyleKey(editorStyle(StyleKey::TextError));
    hint->setVisibility(EWidgetVisibility::Collapsed);
    _hint = hint;

    auto atlasNote = std::make_shared<UIText>("AnimAtlasNote");
    atlasNote->setStyleKey(editorStyle(StyleKey::TextMuted));
    atlasNote->setText("贴图由动画集提供");
    atlasNote->setVisibility(EWidgetVisibility::Collapsed);
    _atlasNote = atlasNote;

    auto play = ui::button("AnimPlay", "Play")
                    .setContentPadding({6.0f, 2.0f})
                    .setOnClick([this]() { beginPreview(); })
                    .child(ui::text("AnimPlayLabel").setText("Play"))
                    .share();
    auto stop = ui::button("AnimStop", "Stop")
                    .setContentPadding({6.0f, 2.0f})
                    .setOnClick([this]() { endPreview(); })
                    .child(ui::text("AnimStopLabel").setText("Stop"))
                    .share();
    _play = play;
    _stop = stop;

    auto status = std::make_shared<UIText>("AnimStatus");
    status->setStyleKey(editorStyle(StyleKey::TextMuted));
    _status = status;

    auto pathRow = ui::row("AnimPathRow").setSpacing(editor_density::kControlSpacing);
    pathRow.child(ui::text("AnimPathLabel").setText("Animation").setStyleKey(editorStyle(StyleKey::TextMuted)),
                  animLabelSlot());
    pathRow.child(path, animFillSlot());

    auto buttonRow = ui::row("AnimAssetButtons").setSpacing(editor_density::kControlSpacing);
    buttonRow.child(browse, animFixedSlot(editor_density::kBrowseButtonWidth));
    buttonRow.child(show, animFixedSlot(editor_density::kLocateButtonWidth));
    buttonRow.child(edit, animFixedSlot(editor_density::kLocateButtonWidth));

    auto clipRow = ui::row("AnimClipRow").setSpacing(editor_density::kControlSpacing);
    clipRow.child(ui::text("AnimClipLabel").setText("Clip").setStyleKey(editorStyle(StyleKey::TextMuted)),
                  animLabelSlot());
    clipRow.child(clips, animFillSlot());

    auto transport = ui::row("AnimTransport").setSpacing(editor_density::kControlSpacing);
    transport.child(play, animFixedSlot(editor_density::kBrowseButtonWidth));
    transport.child(stop, animFixedSlot(editor_density::kLocateButtonWidth));

    addDetachedChild(ui::column("AnimSection")
                         .setSpacing(editor_density::kRowSpacing)
                         .child(std::move(pathRow))
                         .child(std::move(buttonRow))
                         .child(std::move(clipRow))
                         .child(hint)
                         .child(atlasNote)
                         .child(std::move(transport))
                         .child(status)
                         .release());
}

void EditorSpriteAnimationSection::sync(WidgetTree& tree)
{
    SpriteAnimationComponent* animation = _resolve ? _resolve() : nullptr;
    UIElement*                focused   = tree.getFocused();
    const bool                bEditable = animation && !_bReadOnly && _animation.isValid();

    if (_path && _path.get() != focused) {
        std::string path;
        if (animation && _animation.tryGetAssetPath(path)) {
            _path->setText(path);
            _path->setError(_animation.hasAssetResolveError());
        }
        else {
            _path->setText({});
            _path->setError(false);
        }
        _path->setEnabled(bEditable);
    }
    if (_browse) {
        _browse->setEnabled(bEditable && static_cast<bool>(_assetPicker));
    }
    std::string path;
    const bool  bHasPath = animation && _animation.tryGetAssetPath(path) && !path.empty();
    if (_show) {
        _show->setEnabled(bHasPath && static_cast<bool>(_revealAsset));
    }
    if (_edit) {
        _edit->setEnabled(bHasPath && static_cast<bool>(_openAsset));
    }

    bool                         bLoaded = false;
    const std::vector<std::string> names =
        animation ? animationClipNames(*animation, bLoaded) : std::vector<std::string>{};
    std::string                    clip;
    if (animation) {
        clip = animation->clip;
    }
    const bool bListed = bLoaded && std::find(names.begin(), names.end(), clip) != names.end();
    const bool bComboEnabled = bEditable && bLoaded && !names.empty();

    if (_clips && _clips.get() != focused) {
        std::vector<std::string> items = names;
        int                      selected = -1;
        if (!bLoaded || names.empty()) {
            if (!clip.empty()) {
                items    = {clip};
                selected = 0;
            }
        }
        else if (!bListed && !clip.empty()) {
            items.insert(items.begin(), clip);
            selected = 0;
        }
        else {
            const auto found = std::find(items.begin(), items.end(), clip);
            selected = found == items.end() ? -1 : static_cast<int>(found - items.begin());
        }
        _clips->_items = std::move(items);
        _clips->setSelectedIndex(-1, false);
        _clips->setSelectedIndex(selected, false);
        _clips->setEnabled(bComboEnabled);
    }

    if (_hint) {
        std::string hint;
        if (!animation || !bLoaded) {
            hint = "Animation set is not loaded";
        }
        else if (names.empty()) {
            hint = "Animation set has no clips";
        }
        else if (!clip.empty() && !bListed) {
            hint = "Clip is not in this animation set";
        }
        if (hint.empty()) {
            _hint->setVisibility(EWidgetVisibility::Collapsed);
        }
        else {
            _hint->setText(hint);
            _hint->setVisibility(EWidgetVisibility::Visible);
        }
    }

    if (_atlasNote) {
        const SpriteAnimationSet* set = animation ? animation->animation.get() : nullptr;
        const bool                 bHasAtlas = set && !set->atlas.empty();
        _atlasNote->setVisibility(bHasAtlas ? EWidgetVisibility::Visible : EWidgetVisibility::Collapsed);
    }

    if (_play) {
        _play->setEnabled(bEditable && bLoaded && !clip.empty());
    }
    if (_stop) {
        _stop->setEnabled(_bPreviewing);
    }
    if (_status) {
        if (!animation) {
            _status->setText("Stopped");
        }
        else {
            std::string text = animation->isPlaying() ? "Playing" : "Stopped";
            if (!animation->currentClip().empty()) {
                text += "  ";
                text += animation->currentClip();
            }
            const int32_t frame = animation->shownFrame();
            if (frame >= 0) {
                text += "  frame ";
                text += std::to_string(frame);
            }
            _status->setText(text);
        }
    }
}

} // namespace ya
