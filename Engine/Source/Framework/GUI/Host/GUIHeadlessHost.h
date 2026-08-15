#pragma once

#include "App/Kernel/AppKernel.h"
#include "App/Control/AutomationRun.h"
#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <memory>

namespace ya
{

/// No-window GUI host for deterministic snapshot/automation checks. It uses
/// AppKernel for event/tick/exit policy and stops before any GPU composition;
/// windowed/offscreen paths consume the same immutable snapshot later.
struct FGUIHeadlessHostConfig
{
    Extent2D                logicalExtent{800, 600};
    UIFrameBuildContext     frameBuildContext{};
    IAppEventSource*        eventSource = nullptr;
    AppAutomationRunOptions automation{};
    std::function<void(const UIFrameSnapshot&)> onSnapshot;
    /// Emit per-frame perf telemetry (draw/painted/rebuilt/dirty transitions/
    /// notify visits) for the Workbench performance baseline (GI-004).
    bool bPerfTelemetry = false;
};

class YA_GUI_API GUIHeadlessHost final : public IAppLoopDelegate
{
public:
    GUIHeadlessHost(const FGUIHeadlessHostConfig& config, IGUIAppDelegate& delegate);
    ~GUIHeadlessHost() override;

    GUIHeadlessHost(const GUIHeadlessHost&)            = delete;
    GUIHeadlessHost& operator=(const GUIHeadlessHost&) = delete;

    [[nodiscard]] bool init();
    [[nodiscard]] int  run();
    void               shutdown();

    [[nodiscard]] WidgetTree& getTree();
    [[nodiscard]] const UIFrameSnapshot& getLastSnapshot() const;
    void injectEvent(const Event& event, const glm::vec2& logicalPoint);

    void onInit() override;
    void onEvent(const Event& event) override;
    void onTick(float dt) override;
    void onShutdown() override;
    [[nodiscard]] bool shouldClose() const override;

private:
    void dispatchToTree(const Event& event, const glm::vec2& logicalPoint);

    struct FImpl;
    std::unique_ptr<FImpl> _impl;
};

} // namespace ya
