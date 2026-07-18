# Progress

## Implemented

- `IAppExtension` owns generic attach, input, logic, render preparation, and presentation hooks.
- `EditorAppExtension` now owns `EditorLayer` and ImGui initialization.
- `RenderViewportSnapshot` replaced the Runtime -> `EditorLayer` push dependency.
- `AppState::Editor` was renamed to `Stopped` to represent a play-session state rather than a module identity.
- XMake now builds `ya-runtime` without Editor, ImGui, or ImGuizmo link dependencies, `ya-editor` with the Editor and ImGui integration, and keeps `ya` as a compatibility aggregate.
- `DebugPrimitives` now exposes a deferred settings snapshot consumed by the Editor instead of rendering Runtime ImGui; postprocessing controls likewise flow through `DeferredRenderPipeline::SettingsSnapshot`.

## Current Boundary Gaps

- Excluded legacy Runtime GUI source files still need to move into Editor adapters or be removed after their snapshots and commands are complete.
- `SceneManager` still owns `_editorScene` and clone/restore play-session state.
- Editor scene selection must be forwarded through the generic extension lifecycle before further migration.

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
