# Progress

## Implemented

- `IAppExtension` owns generic attach, input, logic, render preparation, and presentation hooks.
- `EditorAppExtension` now owns `EditorLayer` and ImGui initialization.
- `RenderViewportSnapshot` replaced the Runtime -> `EditorLayer` push dependency.
- `AppState::Editor` was renamed to `Stopped` to represent a play-session state rather than a module identity.
- XMake now builds `ya-runtime` without Editor, ImGui, or ImGuizmo link dependencies, `ya-editor` with the Editor and ImGui integration, and keeps `ya` as a compatibility aggregate.
- `DebugPrimitives` now exposes a deferred settings snapshot consumed by the Editor instead of rendering Runtime ImGui; postprocessing controls likewise flow through `DeferredRenderPipeline::SettingsSnapshot`.
- Render target inspection now lives in `Editor/RenderTargetInspector.cpp`; Runtime publishes the target catalog and queues typed format commands for application before the world pass.
- The retired App GUI dashboard and profiling facade no longer live in Runtime. `App::onRenderGUI` and the corresponding no-op sample overrides are removed; the Runtime/Render/Core/ECS/Platform source scan now has zero ImGui includes.
- `ya-runtime` now runs an XMake pre-build guard over non-Editor source files and rejects direct Editor, ImGui, ImGuiHelper, or ImGuizmo includes.
- Editor configuration now enters through `IAppExtension::onConfigure` after generic config initialization and before Runtime startup; Runtime no longer opens `Editor.json` or derives its startup scene from it.
- Shadow settings are Runtime developer settings stored in `Runtime.json`: Runtime loads them before pipeline initialization and applies `Automation.json` as a temporary override. Editor edits the same typed Runtime settings through deferred pipeline commands and migrates legacy Shadow values from `Editor.json` once when needed.
- Deferred render, post-processing, SSAO, and IBL settings now follow the same Runtime Developer Settings ownership: Runtime reads `Runtime.json` before stage initialization, while Editor migrates legacy values and writes only the Runtime document.
- Automation screenshot targets now distinguish the Runtime viewport and generic presentation output; the former `Editor` target and unused Runtime editor-camera overrides are removed from the public Runtime contract.
- Material components expose generic property-change synchronization APIs; Editor forwards reflected property paths without defining a Runtime-side Editor protocol.
- `SceneManager` now owns only generic active-scene activation, destruction, loading, and cloning. `EditorPlaySession` owns authoring/play scene references and handles clone/restore through generic `IAppExtension` state-transition hooks; a Runtime-only App retains its active scene across state transitions.

## Current Boundary Gaps

- Pipeline-stage UI remains in Runtime and still needs typed snapshots and commands before Editor can own its presentation.
- Retired Deferred pipeline ImGui implementations have been removed; only no-op compatibility entry points remain until the shared pipeline UI interfaces are removed in the Forward/stage migration.
- `IRenderPipelineSettingsUI` and `IRenderPipelineDebugUI` are removed. Render Target inspection and format requests now use a dedicated non-UI pipeline capability; Forward and Deferred empty GUI entry points are gone.
- `IRenderStage` no longer exposes GUI hooks. Empty Deferred, Forward viewport, Shadow-stage, and post-processing UI entry points are removed; remaining independent Shadow/Vulkan GUI APIs are the next migration boundary.
- Empty Shadow technique/pass, Vulkan pipeline, RenderDiagnostics, and Forward auxiliary GUI entry points are removed. Runtime, Render, and Platform source now have no remaining `renderGUI` APIs.
- Render Target snapshots and format commands are named `RenderTargetCatalog` and `RenderTargetFormatCommand`; Editor consumes the neutral Runtime API without Editor-named Runtime types.

## Runtime UI Cleanup

- Legacy App GUI, profiling facade, and RenderRuntime GUI have no remaining callers and are excluded from the active engine target.
- Render diagnostics remains Runtime-owned for automation/RenderDoc; only its UI adapter remains to be migrated.
- `PostProcessingStage` no longer includes or renders ImGui; its render state remains available for the future Editor adapter.
- SSAO and deferred lighting stages no longer compile ImGui UI. Their data and runtime setters remain until corresponding Editor adapters are introduced.
- Deferred GBuffer, viewport overlay, and shadow stage UI no longer compile into Runtime.
- Forward viewport stage and auxiliary-pass UI no longer compile into Runtime.
- Forward pipeline UI composition and shadow-setting widgets no longer compile into Runtime.
- Basic shadow-map technique and point-pass UI no longer compile into Runtime.
- Forward lit and unlit material pass pipeline UI no longer compiles into Runtime.
- Deferred pipeline now exposes a typed settings snapshot and safe settings request path for the future Editor adapter.
- Editor deferred settings now controls shadow enablement through the same frame-safe settings request path.
- Render diagnostics keeps automation/RenderDoc behavior but no longer includes Runtime ImGui UI.
- Pipeline-stage UI remains in Runtime and is the next migration batch; it needs typed snapshots and commands before Editor can own the presentation.
