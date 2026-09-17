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
  `Engine/Content/Editor/Gizmos/*`.
- Geometry: `EEngineMesh`, `EngineMeshBuilder`, `PrimitiveGeometryFactory::createEngineMeshData(EEngineMesh)`,
  `PrimitiveMeshCache::getEngineMesh`, `MeshSource::_engineMesh`.
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
