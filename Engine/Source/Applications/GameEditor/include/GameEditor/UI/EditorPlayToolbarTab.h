#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{

class ActionMap;
struct App;
struct UIButton;
struct UIText;

/// Thin Play / Stop / Simulate row. This is a Level owned dock tab so it can
/// sit above the Viewport; it is not chrome and not Runtime Tools.
class EditorPlayToolbarTab : public UICompoundWidget
{
  public:
    EditorPlayToolbarTab(ActionMap* actions = nullptr, App* app = nullptr);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    ActionMap* _actions = nullptr;
    App*       _app     = nullptr;

    std::shared_ptr<UIButton> _playButton;
    std::shared_ptr<UIButton> _simulateButton;
    std::shared_ptr<UIButton> _stopButton;
    std::shared_ptr<UIText>   _modeText;

    void refresh();
};

} // namespace ya
