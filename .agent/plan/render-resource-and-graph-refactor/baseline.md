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

- screenshot comparison artifacts

The current versioned postprocess/SSAO smoke entry is:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/postprocess-smoke.automation.json --exit-after-frame=80 --screenshot=/tmp/postprocess-smoke.png --screenshot-frame=60 --screenshot-target=editor --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

This config exercises Deferred SSAO plus postprocess / bloom / tone-mapping-curve overrides without mutating the developer's local `Editor.json`.

SSAO-disabled smoke also has a versioned automation entry now:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/ssao-disabled-smoke.automation.json --exit-after-frame=220 --screenshot=/tmp/ssao-disabled-smoke.png --screenshot-target=editor --screenshot-frame=180 --screenshot-settle-frames=1 --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

This config keeps the Deferred default path but explicitly disables SSAO through automation overrides, so the same fixed camera can be reused to verify the AO toggle without mutating local editor settings.

Observed result on 2026-07-16:

- local artifact: `/tmp/ssao-disabled-smoke-2026-07-16.png`
- sha1: `80d72f99f32432782c4e6e14fae2a0f4f1980d2f`
- visual check: Deferred frame rendered normally with the AO-darkened ground contact noticeably relaxed relative to the default baseline

Viewport resize smoke also has a versioned automation entry now:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/viewport-resize-smoke.automation.json --exit-after-frame=220 --screenshot=/tmp/viewport-resize-smoke.png --screenshot-target=editor --screenshot-frame=180 --screenshot-settle-frames=1 --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

This config exercises the existing editor pending viewport-resize path at frame 50 and captures a post-resize frame after the new extent has settled.

Observed result on 2026-07-16:

- local artifact: `/tmp/viewport-resize-smoke-2026-07-16.png`
- sha1: `bd77997218ebe8ba88e97b80903626b1b6e57381`
- visual check: editor viewport reached the resized frame and the scene rendered normally in Deferred

Shadow toggle/resolution smoke also has a versioned automation entry now:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/shadow-smoke.automation.json --exit-after-frame=220 --screenshot=/tmp/shadow-smoke.png --screenshot-target=editor --screenshot-frame=180 --screenshot-settle-frames=1 --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

This config drives shadow automation through `shadow.quality=high`, `shadow.directionalEnabled=false`, and `shadow.resolution=3072`, so the runtime path exercises both the toggle and the shadow-resource resize/rebuild edge in one repeatable run.

Observed result on 2026-07-16:

- local artifact: `/tmp/shadow-smoke-2026-07-16.png`
- sha1: `5f5c72326d8027282481ef82b0e2d9aec828843d`
- visual check: Deferred frame rendered normally with directional-ground shadowing removed while local lighting and reflections remained intact

Forward pipeline switch smoke also has a versioned automation entry now:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/pipeline-switch-smoke.automation.json --exit-after-frame=220 --screenshot=/tmp/pipeline-switch-smoke.png --screenshot-target=editor --screenshot-frame=180 --screenshot-settle-frames=1 --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

This config keeps Deferred as the startup default, switches to Forward at frame 50, and captures the settled Forward frame later in the same run.

Observed result on 2026-07-16:

- local artifact: `/tmp/pipeline-switch-smoke-2026-07-16.png`
- sha1: `24578fd8b15569e66fbcfb53be888a3cf04b7bbd`
- visual check: screenshot shows the runtime settled in Forward mode and the PBR sphere still carries visible environment reflection

Editor screenshot readback + graceful shutdown smoke also has a versioned automation entry now:

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/shutdown-readback-smoke.automation.json --exit-after-frame=220 --screenshot=/tmp/shutdown-readback-smoke-info.png --screenshot-target=editor --screenshot-frame=180 --screenshot-settle-frames=1 --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=info --log-detail-level=warn"
```

This config keeps the default Deferred path, requests an editor-target screenshot from the presentation image at frame 180, then exits at frame 220 so the same run also exercises the graceful shutdown chain after readback completion.

Observed result on 2026-07-16:

- local artifact: `/tmp/shutdown-readback-smoke-info-2026-07-16.png`
- sha1: `33ecc9cf14d4eb085dea24c5288a93b70d63fef3`
- log evidence: `Saved screenshot`, `Automation requested graceful shutdown after frame 220`, and `Application exited successfully`
- validation check: no `Validation Error`, `VUID-`, or `[Error]` entries in `Engine/Saved/Logs/YA-2026-07-17_05-06-03.log`

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
