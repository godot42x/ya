# Render Resource / Graph Migration Baseline

## Environment

- Date: 2026-07-13
- Host: macOS arm64, Apple M5
- Backend: Vulkan 1.3 through MoltenVK
- Validation: `VK_LAYER_KHRONOS_validation` enabled
- Build mode: debug, unity build enabled
- Target: `HelloMaterial`
- Default active pipeline: Deferred

## Build Baseline

Command:

```bash
make b t=HelloMaterial
```

Result: success.

Known non-blocking warnings:

- unused parameters in `Core/Reflection/ContainerProperty.h`
- XMake ignores `/Zc:preprocessor` on this platform

## Runtime Baseline

Command:

```bash
make r t=HelloMaterial r_args="--exit-after-frame=120"
```

Result:

- Vulkan instance/device/swapchain initialized
- shader preload and pipeline creation completed
- GBuffer, viewport, shadow, SSAO and postprocess resources created
- environment irradiance/prefilter jobs completed
- automation requested graceful shutdown after frame 120
- application exited successfully
- no `Validation Error` or `VUID-` entry in the session log

Session log:

```text
Engine/Saved/Logs/YA-2026-07-13_00-50-57.log
```

The session log is a local artifact and is not part of the committed baseline. Re-run the command above when validating a migration commit.

## Missing Baselines

The following scenarios still need repeatable automation/config entries or successful capture runs before their TODO items can be marked complete:

- Forward pipeline
- screenshot comparison artifacts

The current versioned postprocess/SSAO smoke entry is:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/postprocess-smoke.automation.json --exit-after-frame=80 --screenshot=/tmp/postprocess-smoke.png --screenshot-frame=60 --screenshot-target=editor --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

This config exercises Deferred SSAO plus postprocess / bloom / tone-mapping-curve overrides without mutating the developer's local `Editor.json`.

## Default Screenshot Baselines

Deferred default-scene baseline has a repeatable command and a captured local artifact:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/deferred-baseline.automation.json --exit-after-frame=1510 --screenshot=/tmp/deferred-baseline.png --screenshot-target=editor --screenshot-frame=1500 --screenshot-settle-frames=1 --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

Observed result on 2026-07-16:

- local artifact: `/tmp/deferred-baseline.png`
- sha1: `6d6073190c39007cd70d5fd9dc1481a99ce67e64`

Forward default-scene baseline now also has a versioned automation entry:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/forward-baseline.automation.json --exit-after-frame=1510 --screenshot=/tmp/forward-baseline.png --screenshot-target=editor --screenshot-frame=1500 --screenshot-settle-frames=1 --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

Observed result on 2026-07-16:

- local artifact: `/tmp/forward-baseline.png`
- sha1: `5f69831a3228c6fd752fdc5893457bf529f82c49`

The current `main` worktree now reaches the capture frame and records the baseline successfully. The unblock batch that made this possible stayed deliberately narrow:

- aligned skinned Forward Unlit with the actual pipeline-layout set order
- removed the stale `normalMat` field from `Test/Unlit.glsl` push constants so shader/C++ size matches again
- made Forward Phong always bind a valid skybox/fallback descriptor set during baseline capture
- disabled geometry-shader-dependent debug pipeline creation on Apple M5 / MoltenVK when the backend reports no geometry-shader support

This means `todo.md` can now mark `记录 Forward 默认场景截图基线` as complete. The remaining unchecked screenshot item is the later `关键截图基线对比`, not the existence of the Forward baseline itself.

## Automation Config Path

The initial audit found that `--automation-config=<path>` was parsed but ignored by `AppAutomation`. Phase 0 now includes a fix that opens the explicit path while preserving `Engine/Saved/Config/Automation.json` as the default. This enables versioned baseline configs without modifying the developer's saved editor/automation state.
