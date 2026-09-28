#pragma once

namespace ya
{

struct App;

class GameRuntimeTickOrchestrator
{
    friend class AppModuleTestAccess;

  public:
    /// Run one product frame. Direct callers retain the legacy native event
    /// pump; AppKernel-backed run() passes false because its event source has
    /// already delivered the events for this frame.
    static int      iterate(App& app, float dt);

  private:
    static void     tickLogic(App& app, float dt);
    /// Advance the game UI tree. Independent of the renderer: a tick that
    /// presents nothing still runs UI logic.
    static void     tickUILogic(App& app, float dt);
    /// Apply structural changes queued during logic (entity destroys), after
    /// every script of the frame has run.
    static void     flushStructuralChanges(App& app);
    static void     prepareHostViewState(App& app, float dt);
    /// App-shell preparation (module prepare-for-render, the host clock), then
    /// one call into `RuntimeRenderContext::tick`, which owns the frame's whole
    /// render order -- declare → extract → prepare → build → acquire → record →
    /// submit → present extras. The steps live there now; see its class comment
    /// for the sequence and where each fact belongs.
    static void     tickRender(App& app, float dt);
    /// The frame skeleton's last step: hand this tick's produced images and,
    /// when tick automation is enabled, the capture / diagnostic hooks to the
    /// automation plane. Kept out of `iterate` so the skeleton reads as
    /// fps → logic → render → callbacks → tick++ → report.
    static void     reportTickToAutomation(App& app);
};

} // namespace ya
