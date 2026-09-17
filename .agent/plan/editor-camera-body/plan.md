# Editor Camera Body & Companion Contract

## Goal

Give a scene camera a real body in the scene (option B) without teaching the
engine a second ownership model, and state the boundary rules that keep derived
visuals out of scene data.

Delivered:

- A camera body and a light icon are **generated companion entities**: their own
  entity plus `Node3D` under the host node, so they follow the host transform
  through the hierarchy and never occupy the host's mesh/material slots.
- **One declaration per host type** (`CompanionRegistry` / `CompanionSpec`),
  consumed by exactly one lifecycle driver (`CompanionManager`). Serialization,
  cloning, rendering, picking, inspector and packaging ask that declaration
  instead of testing component fields.
- **View visibility is a bit mask per view** (`FRenderFeatureMask`), not a field
  on the component and not a scene-level flag: companions exist in every mode and
  only the view decides whether they draw.
- The WIP component-level `intrinsic` machinery is gone (`IComponent` is back to
  `vptr + _owner`), and the bootstrap `.obj` / `.blend` assets are replaced by a
  procedural engine mesh.

## Boundaries / Non-goals

- Release packaging strip is **not implemented**. `EAssetPackClass` +
  `CompanionManager::eachCompanion` is the seam a future pack filter reads.
- `bShowEditorGizmos` is a single global switch (View menu / automation). No
  per-object gizmo flag exists by design.
- `ModelComponent` mesh children keep going through `ModelInstantiationSystem`
  (it also builds shared materials and the skeleton animator); they are the
  canonical `Content` companion but are not declared through the registry.
- No template DSL for specs. Plain `std::function` hooks until a fourth kind of
  companion appears.

## Phases

1. **Contract layer** — `ManagedChildComponent{host}`, `CompanionRegistry`
   (scene-3d), `CompanionManager` + `RenderCompanionSpecs` (render-ecs-adapters).
   *Verification:* both modules build; queries return the declared boundary.
2. **Feature gate** — `RenderFeatures.h`, `RenderDrawItem.features/hostEntityId`,
   per-view `viewFeatures`, bucket binding as the single filter. *Verification:*
   a gizmo item disappears from a `Game`-only view and returns with `Gizmo` set.
3. **Rule convergence** — the two `ILinkageRule` classes are replaced by
   declarations in the composition root; `bAppStopped` and
   `shouldRenderBillboard` are deleted.
4. **Geometry** — the camera body is engine content
   (`Engine/Content/Editor/Gizmos/camera_body.obj`) referenced by
   `MeshSource::setModelPath` from the declaration.
5. **Editor boundary** — generated rows visible but disabled, inspector fields
   greyed (`PropertyGraph::markAllReadOnly`), no transform manipulator on a
   companion, structural commands still refused.
6. **Spec + artifacts** — `scene-object-boundary` skill, memory entry, routing.

## Acceptance

- `LinkageFrameworkTest` (6) green, including "host exists before the rule sees
  the scene" (the sweep path) and "host mesh and body coexist".
- `SceneSerializerTest.GeneratedCompanionIsNotSerialized` green.
- `python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject`:
  camera shows a body in the editor viewport, clicking it selects the camera,
  a game view does not draw it, `View > Show Editor Gizmos` does, and saving the
  scene leaves no companion entity or node in the file.

## Known debt

- `ModelInstantiationSystem` still duplicates the "child node + marker"
  construction the manager now owns. A future pass can route model mesh children
  through `CompanionManager` once the instantiation system's extra work (shared
  materials, skeleton animator) is expressed as a spec hook.
- Billboard feature bits live on `BillboardComponent` (its own bits) rather than
  being looked up per entity, because `ya-render-3d` cannot depend on the
  adapters layer that owns the manager.
