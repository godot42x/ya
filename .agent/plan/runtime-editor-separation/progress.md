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
- Runtime settings use an Unreal `UDeveloperSettings`-style ownership model: the Runtime consumer owns each typed setting's default/schema/load/validation/application lifecycle, reads `Runtime.json` before consumer initialization, and accepts command-line or `Automation.json` only as temporary overrides. Editor is an authoring adapter that may display, persist, and one-way migrate legacy values, but does not own types or participate in a packaged Runtime load.
- Shadow, deferred render, post-processing, SSAO, and IBL settings already follow this model: Runtime reads `Runtime.json` before stage initialization, while Editor migrates legacy values and writes only the Runtime document.
- Automation screenshot targets now distinguish the Runtime viewport and generic presentation output; the former `Editor` target and unused Runtime editor-camera overrides are removed from the public Runtime contract.
- Material components expose generic property-change synchronization APIs; Editor forwards reflected property paths without defining a Runtime-side Editor protocol.
- Removed unused `Scene` editor update/render hooks; Scene now exposes only generic Runtime update and render entry points.
- Editor profiling preferences now use an Editor-side configuration adapter; Core profiling exposes only Runtime state and automation overrides.
- Lua script preview bookkeeping is now authoring-neutral Runtime data; Editor owns the preview workflow that consumes it.
- Texture-slot reflection uses a generic Runtime editability predicate rather than an Editor-named API.
- `HelloMaterial` now explicitly depends on `ya-editor` and registers the Editor extension, while `GreedySnake` is an explicit Runtime-only sample depending only on `ya-runtime`.
- `YAEditor` is an explicit Editor host program depending on `ya-editor` and registering the Editor extension; it is separate from Runtime-only application entry points.
- `ya-testing` now depends only on `ya-runtime`; its container reflection coverage no longer imports an unused Editor renderer header.
- `SceneManager` now owns only generic active-scene activation, destruction, loading, and cloning. `EditorPlaySession` owns authoring/play scene references and handles clone/restore through generic `IAppExtension` state-transition hooks; a Runtime-only App retains its active scene across state transitions.

## Current Boundary Gaps

- Add direct coverage for `IAppExtension` ordering, event consumption, no-extension dispatch, and deferred Runtime command application; the existing lifecycle tests currently cover scene ownership only.
- Keep `ya` as a compatibility aggregate until the designated breaking-change window. All active samples, tests, and `YAEditor` already use explicit module dependencies.
- Audit the remaining authoring-neutral uses of the word `editor` (`IS_EDITOR` script compatibility, profiling metric names, and comments) individually. They are not source or link dependencies and must not be mechanically removed.

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
