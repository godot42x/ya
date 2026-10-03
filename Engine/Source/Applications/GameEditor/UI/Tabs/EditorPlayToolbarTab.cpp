#include "GameEditor/UI/Tabs/EditorPlayToolbarTab.h"

#include "GUI/Binding/ActionMap.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GameEditor/UI/Shell/EditorListRows.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
#include "GameRuntime/App.h"

namespace ya
{

EditorPlayToolbarTab::EditorPlayToolbarTab(ActionMap* actions, App* app)
    : UICompoundWidget("PlayToolbarBody", "panel.canvas")
    , _actions(actions)
    , _app(app)
{
    enableTick();
}

void EditorPlayToolbarTab::construct()
{
    auto play = iconLabeledButton("PlayToolbarPlay", "Play", editor_icons::kPlay).setOnClick([this]() {
        if (_actions) {
            (void)_actions->execute("runtime.play");
        }
    });
    auto simulate =
        iconLabeledButton("PlayToolbarSimulate", "Simulate", editor_icons::kSimulate).setOnClick([this]() {
            if (_actions) {
                (void)_actions->execute("runtime.simulate");
            }
        });
    auto stop = iconLabeledButton("PlayToolbarStop", "Stop", editor_icons::kStop).setOnClick([this]() {
        if (_actions) {
            (void)_actions->execute("runtime.stop");
        }
    });

    _playButton     = play.share();
    _simulateButton = simulate.share();
    _stopButton     = stop.share();

    // The compound's content slot centers the hug-content row; the buttons
    // read as a mode group, the active one highlighted (see refresh).
    addDetachedChild(ui::row("PlayToolbarRow")
                         .setSpacing(6.0f)
                         .setPadding({6.0f, 2.0f})
                         .child(_playButton, ui::boxSlot().preferredSize({76.0f, 26.0f}))
                         .child(_simulateButton, ui::boxSlot().preferredSize({96.0f, 26.0f}))
                         .child(_stopButton, ui::boxSlot().preferredSize({76.0f, 26.0f}))
                         .release(),
                     [](UIElement&, UISlot& slot) {
                         if (auto* content = slot.as<UIContentSlot>()) {
                             content->setHAlign(EUIOverlayAlignment::Center);
                             content->setVAlign(EUIOverlayAlignment::Center);
                         }
                     });
}

void EditorPlayToolbarTab::onAttached()
{
    refresh();
}

void EditorPlayToolbarTab::onSpawnComplete()
{
    refresh();
}

void EditorPlayToolbarTab::tick(float)
{
    refresh();
}

void EditorPlayToolbarTab::refresh()
{
    if (!_app) {
        return;
    }
    // The mode buttons read as a radio group: the active mode's button is
    // highlighted (selectedFill) and stays enabled — re-clicking it is a
    // guarded no-op in startRuntime / startSimulation.
    const bool bPlaying    = _app->isRuntimeMode();
    const bool bSimulating = _app->isSimulationMode();
    if (_playButton) {
        _playButton->setSelected(bPlaying);
    }
    if (_simulateButton) {
        _simulateButton->setSelected(bSimulating);
    }
    if (_stopButton) {
        _stopButton->setEnabled(bPlaying || bSimulating);
    }
}

} // namespace ya
