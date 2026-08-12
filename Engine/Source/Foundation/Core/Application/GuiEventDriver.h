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
    Checkpoint,
};

struct GuiScenarioStep
{
    EGuiScenarioStepKind kind = EGuiScenarioStepKind::Frame;
    uint32_t   frame      = 0;
    glm::vec2  point      = {0.0f, 0.0f};
    glm::vec2  wheel      = {0.0f, 0.0f};
    int        button     = 0;
    EKey::T    key        = EKey::NONE;
    std::string text;
    glm::vec2  dragTo     = {0.0f, 0.0f};
    int        dragSteps  = 8;
    std::string tag;
};

YA_CORE_API std::vector<GuiScenarioStep> parseGuiScenario(std::string_view jsonl,
                                                          std::string* errorOut = nullptr);

YA_CORE_API std::vector<GuiScenarioStep> loadGuiScenarioFile(const std::string& path,
                                                             std::string* errorOut = nullptr);

class YA_CORE_API GuiScenarioExecutor
{
public:
    using StepFrameFn  = std::function<void(uint32_t count)>;
    using CheckpointFn = std::function<void(const std::string& tag)>;

    GuiScenarioExecutor(IGuiEventSink& sink,
                        StepFrameFn   stepFrame,
                        CheckpointFn  onCheckpoint = {});

    bool run(const std::vector<GuiScenarioStep>& steps);
    bool runJsonl(std::string_view jsonl);

private:
    IGuiEventSink& _sink;
    StepFrameFn    _stepFrame;
    CheckpointFn   _onCheckpoint;
};

/// Scenario key name to EKey ("Enter", "Space", "Down", "A", ...).
YA_CORE_API EKey::T keyFromName(std::string_view name);

/// Emit the pointer/key/drag events of a step through the sink. Frame and
/// Checkpoint are no-ops (the host owns frame stepping and dumps). Shared by
/// GuiScenarioExecutor and host frame-loop bridges so event construction
/// lives in exactly one place.
YA_CORE_API void emitGuiScenarioStep(IGuiEventSink& sink, const GuiScenarioStep& step);

} // namespace ya
