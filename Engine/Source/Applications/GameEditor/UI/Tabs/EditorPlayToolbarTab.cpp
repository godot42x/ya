#include "GameEditor/UI/Tabs/EditorPlayToolbarTab.h"

#include "GUI/Binding/ActionMap.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
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
    auto mode3d = labeledButton("PlayToolbarMode3D", "3D").setOnClick([this]() {
        if (_actions) {
            (void)_actions->execute("viewport.mode3d");
        }
    });
    auto mode2d = labeledButton("PlayToolbarMode2D", "2D").setOnClick([this]() {
        if (_actions) {
            (void)_actions->execute("viewport.mode2d");
        }
    });
    auto modeText = ui::text("PlayToolbarMode").setStyleKey(editorStyle(StyleKey::Text)).setText("EDIT");

    _playButton     = play.share();
    _simulateButton = simulate.share();
    _stopButton     = stop.share();
    _modeText       = modeText.share();

    addDetachedChild(ui::canvasPanel("PlayToolbarHost")
                         .child(ui::row("PlayToolbarRow")
                                    .setSpacing(6.0f)
                                    .setPadding({6.0f, 2.0f})
                                    .child(_playButton, ui::boxSlot().preferredSize({76.0f, 26.0f}))
                                    .child(_simulateButton, ui::boxSlot().preferredSize({96.0f, 26.0f}))
                                    .child(_stopButton, ui::boxSlot().preferredSize({76.0f, 26.0f}))
                                    .child(std::move(mode3d), ui::boxSlot().preferredSize({44.0f, 26.0f}))
                                    .child(std::move(mode2d), ui::boxSlot().preferredSize({44.0f, 26.0f}))
                                    .child(_modeText, ui::boxSlot().preferredSize({88.0f, 26.0f}))
                                    .release(),
                                ui::canvasSlot()
                                    .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                                    .size({0.0f, editor_density::kToolbarHeight}))
                         .release());
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
    if (_modeText) {
        const char* label = _app->isRuntimeMode()      ? "PLAYING"
                            : _app->isSimulationMode() ? "SIMULATING"
                                                       : "EDIT";
        _modeText->setText(label);
    }
    if (_playButton) {
        _playButton->setEnabled(_app->isStopped());
    }
    if (_simulateButton) {
        _simulateButton->setEnabled(_app->isStopped());
    }
    if (_stopButton) {
        _stopButton->setEnabled(!_app->isStopped());
    }
}

} // namespace ya
