# Runtime / Editor Separation

## Goal

Produce a packageable `ya-runtime` that has no Editor, ImGui, ImGuizmo, or editor configuration dependency. `ya-editor` is a separate host layer that depends on Runtime and supplies authoring UI through `IAppExtension`.

## Architecture Decisions

- Runtime play state is `Stopped`, `Simulation`, or `Runtime`; it is not an editor identity.
- Editor-only state includes selection, editor camera, gizmos, docking, undo/redo, authoring config, and scene copy/restore policy.
- Runtime exposes typed render snapshots and deferred commands. Editor does not access render pipeline internals.
- Settings are owned by their runtime consumer: Runtime Developer Settings define typed defaults, schema, loading, and command application from `Runtime.json`; Editor provides only an authoring surface and legacy migration. Editor-private layout and workflow preferences remain in `Editor.json`.
- XMake targets, not broad preprocessor conditionals, enforce package exclusion: `ya-runtime` <- `ya-editor` <- `YAEditor`; game targets depend only on `ya-runtime`.

## Ordered Work

1. Establish and test `IAppExtension` lifecycle, including scene lifecycle forwarding and editor selection restoration. Keep `ya` as a compatibility aggregate while this is introduced.
2. Split `ya-runtime` and `ya-editor` in XMake. Runtime compilation must exclude Editor, `ImGuiHelper.cpp`, Editor extension sources, and all ImGui/ImGuizmo dependencies; add a dependency scan gate. The target split is complete; the source-level dependency guard and removal of excluded legacy GUI sources remain with the subsequent UI migration.
3. Move presentation UI, render diagnostics, pipeline settings, and render-target tooling to Editor adapters driven by Runtime snapshots/commands.
4. Move editor camera, viewport input, Editor.json defaults, and `SceneManager` authoring/play-scene ownership into an Editor play-session service. Runtime scene loading accepts a runtime scene without clone/restore semantics.
5. Add `YAEditor` and runtime-only example/package entry targets. Remove the compatibility `ya` target only after downstream migration.

## Acceptance Criteria

- `xmake b ya-runtime` and a runtime-only sample build without ImGui, ImGuizmo, or `Engine/Source/Editor` inputs.
- `xmake b ya-editor` and `YAEditor` provide current editor workflows.
- Runtime-only smoke does not load Editor.json or register an editor extension.
- Editor smoke preserves scene selection across activation and Play/Stop transitions.
