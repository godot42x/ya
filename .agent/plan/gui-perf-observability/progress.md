# GUI Frame Inspector 进度

> 建立日期：2026-09-08

## 2026-09-08 — GPO-001 帧诊断记录 + 编译裁剪

### 目标与边界

- 复用 `YA_PROFILING_*`；`release`/`releasedbg` 不采集 rect/名字。
- 不画 overlay，不改 snapshot schema，不吸收无关工作区。

### 本轮完成

- `RuntimeState.guiFrameInspectorEnabled/Channels` + `normalizeRuntimeToggle`。
- `FGuiFrameInspectorRecord`：rebuilt rects（cap 128）、dirty delta、logical→target 变换。
- `UIElement::paint` 经 `YA_GUI_INSPECTOR_RECORD_REBUILD` 写入；关闭时 vector 为空。
- `applyGuiFrameInspectorSpec`；compiled-out 时 WARN 并返回 false。

### 验证

```text
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='GuiFrameInspectorTest.*'
# 4 tests, PASSED
```

### 保留 / 未完成

- GPU flush、HUD、overdraw 未做。

### 偏离项

无。

### 下一接力点

`GPO-002`。

## 2026-09-08 — GPO-002/003/004 overlay

### 目标与边界

- 真实 GPU flush/vtx（不依赖 `bLogFlushBatches`）。
- HUD / rebuild 描边 / overdraw heatmap 走 `extraContent`，不写入产品 snapshot。
- Offscreen parity record 不挂 overlay。
- 不吸收 Editor/Dock/ColorEdit/Workbench 无关脏改动。
- GPO-002/003/004 同提交：overlay 模块与测试共用文件，拆开会制造半成品。

### 本轮完成

- `FRender2dSession` 在 `drawIndexed` 后累加 flush/vtx；`lastFrameStats()` 在 `end()` 拷贝。
- `captureGuiComposeInspector`：CPU model flush（clip-run）+ GPU session。
- Host 在 product replay 之后、overlay 之前读 live session，HUD 不含 inspector 自身 draw call。
- `emitGuiFrameInspectorOverlay`：HUD 三行、rebuild 描边、64×64 heatmap。
- Workbench `--gui-frame-inspector[=hud,rebuild,overdraw]` 与 View 菜单三通道。
- 测试：8 同 clip sprite → `modelScreenFlush == 1`；两层 100×100 → `overdrawFactor ≈ 2`。

### 验证

```text
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='GuiFrameInspectorTest.*'
# 6 tests, PASSED
xmake b ya-gui-host
xmake b GUIWorkbench
```

### 保留 / 未完成

- Editor `replayUIFrameSnapshot` 之后尚未接线（同一 API，非本计划 mainline）。
- GPU HUD 是当前 session 的 product 计数；`lastFrameStats()` 仍含 overlay 自身 flush。

### 偏离项

GPO-002 原计划单独提交且不画 overlay。实现时 overlay 模块同时承担计数写入与绘制，与 003/004 无法无重叠拆分，故三刀合一。功能边界未扩。

### 下一接力点

无。Editor overlay 接线在调用方有需求时再做。
