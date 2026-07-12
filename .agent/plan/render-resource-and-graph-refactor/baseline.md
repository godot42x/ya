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

The following scenarios still need repeatable automation/config entries before their TODO items can be marked complete:

- Forward pipeline
- viewport resize
- shadow enable/disable and resolution change
- SSAO enable/disable
- bloom/postprocess/ACES enable/disable
- Forward/Deferred pipeline switch
- screenshot comparison artifacts

## Automation Config Path

The initial audit found that `--automation-config=<path>` was parsed but ignored by `AppAutomation`. Phase 0 now includes a fix that opens the explicit path while preserving `Engine/Saved/Config/Automation.json` as the default. This enables versioned baseline configs without modifying the developer's saved editor/automation state.
