# Progress

## Round 1 — commit `[render/companion] generate camera body and light icons as companions`

Landed (see `feature_matrix.json` for the per-capability state):

- Contract: `ManagedChildComponent.host`, `Scene3D/CompanionRegistry.{h,cpp}`,
  `Render/Adapters/Companion/CompanionManager.{h,inl,cpp}`,
  `Render/Adapters/Companion/RenderCompanionSpecs.{h,cpp}`.
- Feature gate: `Render3D/Common/RenderFeatures.h`, `RenderDrawItem.features` /
  `hostEntityId`, per-view `viewFeatures` through `ViewPrepareInput` /
  `RenderFrameData` / `CameraFrameInput`, filter in `prepareView`'s bucket
  binding, billboard gate in `DeferredRenderPipeline`.
- Removed: `CameraMeshLinkageRule`, `LightBillboardLinkageRule` (classes +
  shims), `EComponentLifetime` / `_intrinsicTo` / `markIntrinsic` / `isIntrinsic`
  / `isEditorOnly`, `bManagedByLight`, `bAppStopped`, `shouldRenderBillboard`,
  the staged gizmo assets (`camera.obj` / `camera.blend`).
- Geometry: the camera body is engine content
  (`Engine/Content/Editor/Gizmos/camera_body.obj`) referenced through
  `MeshSource::setModelPath`. An earlier revision generated it procedurally
  (`EEngineMesh` + `EngineMeshBuilder`) and dropped it: gizmo meshes are
  ordinary mesh sources, and a dedicated mesh axis bought nothing.
- Editor: `PropertyGraph::markAllReadOnly`, disabled hierarchy rows
  (`UITreeView::FNode::bEnabled` + `FTreeViewStyle.disabledTextColor`), no
  manipulator on a companion, `View > Show Editor Gizmos`,
  `set_editor_gizmos_visible` automation method.
- Automation contract: `list_billboard_components` / `list_overlay_sprites` now
  report `entity_id` + `host_entity_id` + `is_companion` (the icon is its own
  entity now).

Verified:

- `xmake b ya-render-3d ya-scene-3d ya-resource-runtime ya-resource-core ya-render-ecs-adapters ya-game-runtime ya-game-editor ya-testing` — all build.
- `ya-testing --gtest_filter='LinkageFrameworkTest.*:SceneSerializerTest.*:EditorHierarchyOpsTest.*:DeferredRenderPipelineTest.*:ViewPassResourcesTest.*:SceneNodeLifecycleTest.*'` — 29 ran, 29 passed.
- With `EditorPropertyGraphTest.*` and `ToolControlsTest.*` included: 133 ran, 129 passed, 4 failed (all four are the pre-existing failures listed below).
- `LinkageFrameworkTest` covers the sweep path (`CameraComponentGetsGeneratedBodyCompanion`
  adds the component before the scene is activated), host-mesh coexistence, and
  `ClonedCameraRebuildsItsOwnBodyCompanion` (a clone carries no companion and the
  manager rebuilds it).
- The 2 failures (`EditorPropertyGraphTest.AutoPropertySectionAssetPathCommitBrowseAndUndo`,
  `EditorPropertyGraphTest.TextureAssetRowShowsRetainedPreview`) are pre-existing
  and unrelated to this slice.

Not verified / open:

- Full `ya-testing` run aborts in `WidgetLayoutTest.DockTabBarStripDoubleClickFiresHostCallback`
  on a `WidgetTree` pointer-session assertion. No file in this slice is in that
  path; the GUI/dock area is being refactored concurrently. Needs re-check after
  that work settles.
- Editor manual smoke (click body selects camera, game view hides it, save file
  stays clean) was not run in this session.

## Round 2 — spec + routing

- `.agent/skills/scene-object-boundary/SKILL.md`, memory entry, AGENTS routing.

## Round 3 — legacy billboard placement

Loading `Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json` showed the
migration cost of moving icons onto companions: 7 entities (every point light,
the directional light) carry a serialized `BillboardComponent` on the light
entity itself. Left alone, those would read as authored content -- drawn in every
view, next to the new `Gizmo` icon -- and the file would stay polluted.

`makeLightCompanionSpec`'s `onCreate` now drops a legacy host-side billboard
before building the companion, so the declaration adopts the old placement:

- one icon per light, on the companion;
- the host loses the stale component, so the next save cleans the file;
- `LinkageFrameworkTest.LightCompanionAdoptsLegacyHostBillboard` pins it.

`Camera` in the same scene has no mesh, so the camera body is a pure addition
there and needs no migration.

Verified: `LinkageFrameworkTest` 8/8; `ya-engine`, `ya-game-editor`,
`GUIWorkbench`, `GreedySnake`, `ya-testing` all build.

Still open: the example project was not launched, so the visual result (one icon
per light, camera body visible, game view clean) has not been eyeballed, and the
saved file has not been re-read after a round trip.
+
## Round 4 - the three defects the first eyeball pass found

Launching Example/HelloMaterial surfaced three concrete bugs the plan had not
covered. Two were architecture, one was a declaration mistake.

### 1. The world overlay drew on top of the preview (grid over the preview)

EditorViewProducer declared the preview View with
composeOntoViewId = kPrimarySceneViewId + composeRect, so the runtime blitted the
preview onto the world display RT **before** recordEditorWorldViewportOverlays
ran: the x-z grid, manipulator and frustum wireframe were recorded over it. The
first attempt made the editor compose the inset itself (a public
recordViewDisplayInsets reached through EditorViewportCompositor); it rendered
nothing and was the wrong shape anyway - the editor was still sharing one compose
pass with world content.

Final shape: the preview View keeps composeOntoViewId (so it is not the display
root) but composeRect stays empty, which is exactly "rendered into its own image,
shown by whoever asked for it". The editor reads that image and the GUI composes
it as **viewport chrome**: EditorViewportTab is now a UIOverlay of world UIImage
+ UIBorder-framed preview UIImage, so overlays are under it by construction (they
are content of the world image below), not by recording order.
recordViewDisplayInsets went back to file local, and EditorViewportCompositor lost
the insets parameter it never needed.

The preview also stopped drawing gizmos (features = Game): a preview is what that
camera sees, not an authoring view.

### 2. The body mesh did not follow its host

TransformSystem::updateNodeTree recomputed a dirty node's own world matrix and
nothing else, so a child kept its stale world matrix whenever the parent changed
through a path that did not use TransformComponent's setters (Inspector / undo /
reflection / scene load). The frustum wireframe reads the authored transform and
moved; the generated body did not. Fixed at both ends: children are marked stale
whenever a node is recomputed (the place no writer can forget), and
onPostSerialize notifies children because a reflected write never touches a setter.
See memories/reflected_transform_write_bypasses_child_dirty.md.

### 3. The body looked flat because it *was* flat

The body carried UnlitMaterialComponent with a single tint: a silhouette with no
surfaces, which reads as "missing normals" even though the OBJ has them. It now
carries PhongMaterialComponent and takes the scene's lighting like any other mesh
(UE's ACameraActor.CameraMesh is a real mesh too).

Verified in this round:

- LinkageFrameworkTest 11/11 (incl. the new follower test), EditorViewProducerTest
  5/5, EditorViewportTabTest 3/3, plus 311 tests across 14 suites (WidgetLayoutTest
  / DeclarativeContractTest / UIFrameSnapshotTest / ECSTest /
  SceneManagerLifecycleTest / editor input + viewport overlay contracts) all pass.
- Live: with a camera selected, the preview panel appears at the declared rect with
  a hairline frame; component.set on the camera host moved both host and body to
  [1.5, 6.0, 14.0]; the shaded body shows a lit gradient instead of a flat fill;
  the world grid stops at the panel border (sampled inside the panel: no
  line-coloured pixels).

Still not done: an automated click test that drives Hierarchy selection (the
automation surface has no selection method, so this round drove it with a temporary
scene.create_preset auto-select that was reverted before committing).

## Round 5 - the loaded camera rendered from the origin (owner was null)

Reporting the three-way disagreement live (body mesh / gizmo wireframe /
preview content) turned up a different bug than round 4's: it only reproduced on
a camera that came from a scene file. The body mesh sat at the authored pose
while the preview and the FOV wireframe both sat near the world origin, which is
the orbit fallback in `CameraComponent::getFreeView`.

Root cause: `IComponent::_owner` was assigned in two places (`Entity::addComponent`
after `emplace`, and `Scene::clone`) but the serializer creates components through
a third funnel -- `ECSRegistry::addComponent(FName, registry, handle)` -- that is
name-based and type-erased and therefore never had an `Entity*` to set. A loaded
camera kept `_owner == nullptr`, `resolveOwnerWorldPose()` returned false, and
`getFreeView()` fell back to `lookAt(vec3(0,0,_distance), _focusPoint, up)`.
Everything that reads the view agreed with each other and disagreed with the
mesh, which is exactly the report.

Fixed by making ownership an argument of the one creation funnel instead of a
field a caller patches afterwards: `detail_component_mutation::addComponent(
registry, entity, Entity* owner, ...)` sets it at emplace, and `IComponentOps::
create`, both `ECSRegistry::addComponent` overloads, `Entity::addComponentByName`, 
`Scene::addComponent` (resolves the owner through `getEntityByEnttID`), the
serializer and the `component.add` script API all pass it. `_owner` is now
default-initialized to null. The typed path lost its now-redundant `setOwner`.

Verified: the new `SceneSerializerTest.LoadedCameraViewAndWireframeUseItsAuthoredPose`
fails before the fix (9.34 units of disagreement) and passes after;
`SceneNodeLifecycleTest.CreatingAComponentOnAnEntityAssignsThatEntityAsItsOwner`
pins all three funnels. Live, with entity 55 (the camera loaded from
HelloMaterial.scene.json) selected: the body mesh, the RGB manipulator and the
yellow frustum wireframe sit on the same pose, and the preview contents move
with the camera entity transform.
