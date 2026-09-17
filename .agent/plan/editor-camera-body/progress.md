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
