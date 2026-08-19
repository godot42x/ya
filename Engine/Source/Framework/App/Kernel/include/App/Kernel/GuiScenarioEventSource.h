#pragma once

// Scripted event source for the shared app loop: replays a parsed JSON-lines
// GUI scenario (frames / pointer / keys / drags / checkpoints / assertions)
// through the same pollEvents contract the SDL source uses, so windowed and
// headless hosts run identical scenarios. Host-specific callbacks (checkpoint
// dumps, tree assertions, window resize, final capture, done) are injected by
// whoever owns the scenario.

#include "App/Kernel/AppKernel.h"
#include "App/Control/GuiEventDriver.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct GuiScenarioEventSource final : IAppEventSource
{
    std::vector<GuiScenarioStep> steps;
    size_t index = 0;
    uint32_t remainingFrames = 0;
    bool bPendingDoneAfterLastFrame = false;
    bool bDone = false;
    std::function<void(const std::string&)> onCheckpoint;
    std::function<bool(std::string_view)> onAssert;
    std::function<bool()> onAssertValidationClean;
    std::function<void(uint32_t, uint32_t)> onSetWindowSize;
    std::function<void()> onCaptureFinal;
    std::function<void()> onDone;

    void finishLastFrameIfNeeded()
    {
        if (remainingFrames == 0 && index == steps.size()) {
            if (onCaptureFinal) {
                onCaptureFinal();
            }
            bPendingDoneAfterLastFrame = true;
        }
    }

    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        if (bDone) {
            return;
        }
        if (bPendingDoneAfterLastFrame) {
            bPendingDoneAfterLastFrame = false;
            bDone                      = true;
            if (onDone) {
                onDone();
            }
            return;
        }
        if (remainingFrames > 0) {
            --remainingFrames;
            finishLastFrameIfNeeded();
            return;
        }
        while (index < steps.size()) {
            const GuiScenarioStep& step = steps[index++];
            switch (step.kind) {
            case EGuiScenarioStepKind::Frame:
                remainingFrames = std::max(step.frame, 1u) - 1;
                finishLastFrameIfNeeded();
                return;
            case EGuiScenarioStepKind::SetWindowSize:
                if (onSetWindowSize) {
                    onSetWindowSize(step.width, step.height);
                }
                break;
            case EGuiScenarioStepKind::Checkpoint:
                if (onCheckpoint) {
                    onCheckpoint(step.tag);
                }
                break;
            case EGuiScenarioStepKind::Assert:
                if (onAssert && !onAssert(step.assertion)) {
                    bDone = true;
                    if (onDone) {
                        onDone();
                    }
                    return;
                }
                break;
            case EGuiScenarioStepKind::AssertValidationClean:
                if (onAssertValidationClean && !onAssertValidationClean()) {
                    bDone = true;
                    if (onDone) {
                        onDone();
                    }
                    return;
                }
                break;
            case EGuiScenarioStepKind::MouseMove:
            case EGuiScenarioStepKind::MousePress:
            case EGuiScenarioStepKind::MouseRelease:
            case EGuiScenarioStepKind::MouseWheel:
            case EGuiScenarioStepKind::KeyPress:
            case EGuiScenarioStepKind::KeyRelease:
            case EGuiScenarioStepKind::KeyTyped:
            case EGuiScenarioStepKind::Drag: {
                struct ScenarioSink final : IGuiEventSink
                {
                    const std::function<void(const Event&)>& emitFn;

                    explicit ScenarioSink(const std::function<void(const Event&)>& inEmitFn)
                        : emitFn(inEmitFn)
                    {
                    }

                    void dispatch(const Event& event, const glm::vec2& /*logicalPoint*/) override
                    {
                        emitFn(event);
                    }
                } sink{emit};
                emitGuiScenarioStep(sink, step);
                break;
            }
            }
        }
        bDone = true;
        if (onDone) {
            onDone();
        }
    }
};

} // namespace ya
