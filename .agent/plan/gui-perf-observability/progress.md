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
