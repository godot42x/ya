#pragma once

#include "Core/Api.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Inspector/PropertyHandle.h"
#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"

#include <glm/vec4.hpp>

#include <functional>
#include <memory>
#include <string>

namespace ya
{

struct SpriteAnimationComponent;
struct UIButton;
struct UIComboBox;
struct UIText;
struct UITextField;
class UndoStack;
struct WidgetTree;

/// Inspector body for one `SpriteAnimationComponent`: asset row (path, Browse,
/// Show, Edit), clip combo, and a preview that advances only while this
/// section is playing. Preview writes `uvRect` and, when the set owns an
/// atlas, the sprite image path, and puts both back on stop.
class YA_GAME_EDITOR_API EditorSpriteAnimationSection final : public UICompoundWidget
{
    using Resolver = std::function<SpriteAnimationComponent*()>;
    using OpenAsset = std::function<void(std::string path)>;

    Resolver                  _resolve;
    UndoStack*                _undo = nullptr;
    std::function<void()>     _onMutated;
    EditorAssetPickerCallback _assetPicker;
    EditorRevealAssetCallback _revealAsset;
    OpenAsset                 _openAsset;
    bool                      _bReadOnly = false;

    PropertyGraph  _graph;
    PropertyHandle _animation;
    PropertyHandle _clip;

    std::shared_ptr<UITextField> _path;
    std::shared_ptr<UIButton>    _browse;
    std::shared_ptr<UIButton>    _show;
    std::shared_ptr<UIButton>    _edit;
    std::shared_ptr<UIComboBox>  _clips;
    std::shared_ptr<UIText>      _hint;
    std::shared_ptr<UIText>      _atlasNote;
    std::shared_ptr<UIButton>    _play;
    std::shared_ptr<UIButton>    _stop;
    std::shared_ptr<UIText>      _status;

    glm::vec4   _savedUv{0.0f, 0.0f, 1.0f, 1.0f};
    std::string _savedImagePath;
    bool        _bHaveSavedUv = false;
    bool      _bPreviewing  = false;

  public:
    EditorSpriteAnimationSection(std::string name,
                                 Resolver resolve,
                                 UndoStack* undo,
                                 std::function<void()> onMutated,
                                 EditorAssetPickerCallback assetPicker,
                                 EditorRevealAssetCallback revealAsset,
                                 OpenAsset openAsset,
                                 bool bReadOnly = false);
    ~EditorSpriteAnimationSection() override;

    void sync(WidgetTree& tree);
    void tickSection(float deltaSeconds);

  protected:
    void construct() override;

  private:
    void beginPreview();
    void endPreview();
    void restorePreview();
    void commitClip(const std::string& value);
    void commitAssetPath(const std::string& value);
    void noteMutated();
};

} // namespace ya
