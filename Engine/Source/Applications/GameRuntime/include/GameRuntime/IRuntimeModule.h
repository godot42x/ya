#pragma once

#include "App/Module/Module.h"

namespace ya
{

struct App;
struct AppDesc;
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
    /// Called by RenderDeviceState::record after the world graph and the runtime game UI
    /// compose pass, before the presentation graph is recorded. Modules use it
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
    /// After the primary surface has been submitted/presented. Extra OS
    /// windows must present here, not during onPresentation (that records
    /// into the primary command buffer). Do not recreate GPU resources that
    /// the just-submitted primary frame still references.
    virtual void onAfterPresent(App& app, float dt)
    {
        (void)app;
        (void)dt;
    }

    /// Display-compose coverage: do this module's own passes fill the whole
    /// primary surface? True means those passes *are* the window's content, so
    /// the renderer must not first copy a View's display image across the
    /// surface -- that copy would be overdrawn and thrown away. The editor's
    /// chrome answers true; a module that only contributes an overlay leaves it
    /// false and keeps the View as the surface's backdrop.
    ///
    /// A query rather than a switch the module flips, so there is no frame
    /// where the answer and what gets recorded can disagree. See
    /// `App::presentsViewDisplayImage` and
    /// `PresentFrameInput::bCopyViewDisplayImage`.
    [[nodiscard]] virtual bool fillsPrimarySurface() const { return false; }
};

} // namespace ya
