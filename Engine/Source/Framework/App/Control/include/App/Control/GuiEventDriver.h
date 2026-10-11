#pragma once

// Scriptable GUI input driver (shared app foundation). Parses a JSON-lines
// scenario into steps and executes them against an IGuiEventSink using the
// same Core Event types the SDL path produces, so "simulate any GUI operation"
// is a property of the event system itself, not a per-app smoke switch-case.

#include "Core/Api.h"
#include "Core/Event.h"
#include "Core/KeyCode.h"

#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

/// Receives injected events at a tree-local logical point (top-left origin).
struct IGuiEventSink
{
    virtual ~IGuiEventSink() = default;
    virtual void dispatch(const Event& event, const glm::vec2& logicalPoint) = 0;
};

enum class EGuiScenarioStepKind : uint8_t
{
    Frame,
    MouseMove,
    MousePress,
    MouseRelease,
    MouseWheel,
    KeyPress,
    KeyRelease,
    KeyTyped,
    Drag,
    WidgetClick,
    SetWindowSize,
    Checkpoint,
    Assert,
    AssertValidationClean,
};

struct GuiScenarioStep
{
    EGuiScenarioStepKind kind = EGuiScenarioStepKind::Frame;
    uint32_t   frame      = 0;
    glm::vec2  point      = {0.0f, 0.0f};
    glm::vec2  wheel      = {0.0f, 0.0f};
    int        button     = 0;
    EKey::T    key        = EKey::NONE;
    uint32_t   keyMod     = 0;
    std::string text;
    glm::vec2  dragTo     = {0.0f, 0.0f};
    int        dragSteps  = 8;
    uint32_t   width      = 0;
    uint32_t   height     = 0;
    std::string tag;

    /// Widget-targeted input: the host resolves `widget` from the live tree's
    /// laid-out rects at replay time (same lookup as assert.widget) and fills
    /// `point` / `dragTo` before the step is emitted. Empty `widget` = the
    /// step carries raw coordinates. A missing, invisible or covered target
    /// fails the scenario — it never silently degrades to a skipped step.
    std::string widget;
    /// Optional named sub-rect of the target: "handle" = UISplitPane divider,
    /// "close" = UITabButton close hit-zone.
    std::string widgetPart;
    /// Normalized [fx, fy] inside the target rect; {0.5, 0.5} = center.
    glm::vec2  anchor     = {0.5f, 0.5f};
    /// Drag end as a delta from the resolved start point ("by").
    glm::vec2  dragBy     = {0.0f, 0.0f};
    /// Drag end as another widget target ("to_widget") with its own part/anchor.
    std::string dragToWidget;
    std::string dragToPart;
    glm::vec2  dragToAnchor = {0.5f, 0.5f};

    [[nodiscard]] bool bWidgetTargeted() const { return !widget.empty(); }

    /// Serialized JSON assertion payload. Core deliberately treats this as an
    /// opaque contract; the GUI host evaluates it against its tree dump.
    std::string assertion;
};

YA_APP_CONTROL_API std::vector<GuiScenarioStep> parseGuiScenario(std::string_view jsonl,
                                                          std::string* errorOut = nullptr);

YA_APP_CONTROL_API std::vector<GuiScenarioStep> loadGuiScenarioFile(const std::string& path,
                                                             std::string* errorOut = nullptr);

class YA_APP_CONTROL_API GuiScenarioExecutor
{
public:
    using StepFrameFn  = std::function<void(uint32_t count)>;
    using CheckpointFn = std::function<void(const std::string& tag)>;
    using AssertFn     = std::function<bool(std::string_view assertion)>;
    /// Resolves a widget-targeted step to tree-local points at replay time.
    /// The GUI host installs one over its live tree (see
    /// resolveWidgetEventTarget). Returning false fails the scenario.
    using WidgetResolveFn = std::function<bool(const GuiScenarioStep& step,
                                               glm::vec2& outPoint,
                                               glm::vec2& outDragTo,
                                               std::string& error)>;

    GuiScenarioExecutor(IGuiEventSink& sink,
                        StepFrameFn   stepFrame,
                        CheckpointFn  onCheckpoint = {},
                        AssertFn      onAssert = {},
                        WidgetResolveFn onResolveWidget = {});

    bool run(const std::vector<GuiScenarioStep>& steps);
    bool runJsonl(std::string_view jsonl);

private:
    IGuiEventSink&  _sink;
    StepFrameFn     _stepFrame;
    CheckpointFn    _onCheckpoint;
    AssertFn        _onAssert;
    WidgetResolveFn _onResolveWidget;
};

/// Scenario key name to EKey ("Enter", "Space", "Down", "A", ...).
YA_APP_CONTROL_API EKey::T keyFromName(std::string_view name);

/// Emit the pointer/key/drag events of a step through the sink. Frame,
/// Checkpoint, Assert and SetWindowSize are no-ops here: frame stepping,
/// checkpoint dumps, assertions and native-window control stay at the
/// host/kernel layer.
YA_APP_CONTROL_API void emitGuiScenarioStep(IGuiEventSink& sink, const GuiScenarioStep& step);

} // namespace ya
