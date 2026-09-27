#pragma once

#include "App/Module/Module.h"

namespace ya
{

struct App;
struct AppDesc;
struct FFrameSubmission;
struct IRenderSurfaceContext;
class Event;
struct ICommandBuffer;
struct Scene;
enum class AppState;

/// Interface id for querying IRuntimeModule from a loaded IModule.
inline constexpr FInterfaceId YA_RUNTIME_MODULE_INTERFACE = makeInterfaceId("ya.RuntimeModule");

/// Game-runtime module hooks. A module that participates in the runtime shell
/// lifecycle (scene, render, input, presentation) implements this interface and
/// exposes it via IModule::queryInterface(YA_RUNTIME_MODULE_INTERFACE). IModule
/// itself stays domain-agnostic: it only carries the dynamic load lifecycle, so
/// the module system never references game/runtime types.
struct IRuntimeModule
{
    virtual ~IRuntimeModule() = default;

    virtual void onConfigure(App& app, AppDesc& desc)
    {
        (void)app;
        (void)desc;
    }
    virtual void onAttach(App& app) { (void)app; }
    virtual void onDetach(App& app) { (void)app; }
    virtual bool onBeforeAppStateChange(App& app, AppState previousState, AppState nextState)
    {
        (void)app;
        (void)previousState;
        (void)nextState;
        return true;
    }
    virtual void onAfterAppStateChange(App& app, AppState previousState, AppState currentState)
    {
        (void)app;
        (void)previousState;
        (void)currentState;
    }
    virtual void onSceneActivated(App& app, Scene* scene)
    {
        (void)app;
        (void)scene;
    }
    virtual void onSceneDestroyed(App& app, Scene* scene)
    {
        (void)app;
        (void)scene;
    }
    virtual bool onEvent(App& app, const Event& event)
    {
        (void)app;
        (void)event;
        return false;
    }
    virtual void onLogic(App& app, float dt)
    {
        (void)app;
        (void)dt;
    }
    virtual void onBeforeRender(App& app, float dt)
    {
        (void)app;
        (void)dt;
    }
    /// Called from the application's recording order (`RuntimeRenderContext::record`)
    /// after the world graph and the runtime game UI compose pass, before the
    /// presentation graph is recorded. Modules use it
    /// to record their own viewport composition (e.g. editor overlays) into the
    /// same command buffer. Command recording is already active, so GPU
    /// resources must not be recreated here.
    virtual void onViewportCompose(App& app, ICommandBuffer& commandBuffer, float dt)
    {
        (void)app;
        (void)commandBuffer;
        (void)dt;
    }
    virtual void onBeforePresentation(App& app, ICommandBuffer& commandBuffer, float dt)
    {
        (void)app;
        (void)commandBuffer;
        (void)dt;
    }
    virtual void onPresentation(App& app, ICommandBuffer& commandBuffer, float dt)
    {
        (void)app;
        (void)commandBuffer;
        (void)dt;
    }
    /// Record this module's extra OS windows into the frame's one submission.
    /// Called after the host surface's command buffer is sealed and before
    /// that submission is queued, so an extra window gets its own command
    /// buffer and its own acquire/present sync pair without a second
    /// `submitFrame`. Do not submit or present here.
    virtual void recordExtraSurfaces(App& app, float dt, FFrameSubmission& submission)
    {
        (void)app;
        (void)dt;
        (void)submission;
    }

    /// Display-compose coverage for ONE surface: do this module's own passes
    /// fill that whole window? True means those passes *are* the window's
    /// content, so the renderer must not first copy a View's display image
    /// across the surface -- that copy would be overdrawn and thrown away. The
    /// editor's chrome answers true; a module that only contributes an overlay
    /// leaves it false and keeps the View as the surface's backdrop.
    ///
    /// A query rather than a switch the module flips, so there is no frame
    /// where the answer and what gets recorded can disagree. See
    /// `App::presentsViewDisplayImage` and `ESurfaceBackdrop`.
    ///
    /// It takes the surface because the difference is a property of a window's
    /// content, not a rank: "this window's content IS its viewport image" (a
    /// game viewport filling its window) versus "the viewport is one panel
    /// inside this window" (the editor). A host that presents several windows
    /// gets asked once per window.
    [[nodiscard]] virtual bool fillsSurface(const IRenderSurfaceContext& surface) const
    {
        (void)surface;
        return false;
    }
};

} // namespace ya
