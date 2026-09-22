#include "GameEditor/UI/Sections/RuntimeDebugPrimitivesSection.h"

#include "GameRuntime/App.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "Render3D/Services/DebugRenderSystem.h"

#include <format>
#include <utility>

namespace ya
{

namespace
{

std::shared_ptr<UICheckBox> makeDebugSwitch(const char* key, const char* label)
{
    auto control = std::make_shared<UICheckBox>(key);
    auto text    = std::make_shared<UIText>(std::string(key) + "Label");
    text->setText(label);
    control->addDetachedChild(text);
    return control;
}

} // namespace

RuntimeDebugPrimitivesSection::RuntimeDebugPrimitivesSection(std::string name)
    : UICompoundWidget(std::move(name), "panel")
{
}

void RuntimeDebugPrimitivesSection::construct()
{
    _counts = ui::text("RuntimeDebugCounts").share();
    _enabled = makeDebugSwitch("RuntimeDebugEnabled", "Enabled");
    _depth = makeDebugSwitch("RuntimeDebugDepth", "Depth Test");
    _lines = makeDebugSwitch("RuntimeDebugLines", "Draw Lines");
    _shapes = makeDebugSwitch("RuntimeDebugShapes", "Draw Shapes");

    auto rows = ui::column("RuntimeDebugRows")
                    .setSpacing(3.0f)
                    .child(ui::text("RuntimeDebugHeader")
                               .setText("Debug Primitives")
                               .setStyleKey("text.header"))
                    .child(_enabled)
                    .child(_depth)
                    .child(_lines)
                    .child(_shapes)
                    .child(_counts);
    addDetachedChild(rows.release());

    // One writer for the four switches: each reads the current settings, changes
    // its own field, and requests them back. The render system is a renderer
    // service, so the section reaches it through the app rather than the device.
    const auto bindSwitch = [this](const std::shared_ptr<UICheckBox>& control, auto&& apply)
    {
        control->_onChanged = [control, apply = std::forward<decltype(apply)>(apply)](bool)
        {
            auto* app = App::get();
            if (!app) {
                return;
            }
            auto& renderServices = app->getRenderServices();
            if (!renderServices.hasRenderer()) {
                return;
            }
            auto& debug = renderServices.getDebugRenderSystem();
            auto  settings = debug.buildSettingsSnapshot();
            apply(settings, control->isChecked());
            debug.requestSettings(settings);
        };
    };

    bindSwitch(_enabled, [](auto& settings, bool value) { settings.bEnabled = value; });
    bindSwitch(_depth, [](auto& settings, bool value) { settings.bDepthTest = value; });
    bindSwitch(_lines, [](auto& settings, bool value) { settings.bDrawLines = value; });
    bindSwitch(_shapes, [](auto& settings, bool value) { settings.bDrawShapes = value; });
}

void RuntimeDebugPrimitivesSection::sync(const App* app)
{
    if (!_counts || !_enabled || !_depth || !_lines || !_shapes || !app) {
        return;
    }

    auto& renderServices = app->getRenderServices();
    if (!renderServices.hasRenderer()) {
        _counts->setText("Debug primitives unavailable");
        return;
    }

    const auto settings = renderServices.getDebugRenderSystem().buildSettingsSnapshot();
    _enabled->setChecked(settings.bEnabled);
    _depth->setChecked(settings.bDepthTest);
    _lines->setChecked(settings.bDrawLines);
    _shapes->setChecked(settings.bDrawShapes);
    _counts->setText(std::format("Pending {} lines / {} shapes | Frame {} / {} | Immediate {} / {}",
                                 settings.pendingLineCount,
                                 settings.pendingShapeCount,
                                 settings.frameLineCount,
                                 settings.frameShapeCount,
                                 settings.immediateLineCount,
                                 settings.immediateShapeCount));
}

} // namespace ya
