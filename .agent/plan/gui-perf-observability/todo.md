# GUI Frame Inspector TODO

> 更新时间：2026-09-08
> 状态：`[ ]` 未开始，`[-]` 进行中，`[x]` 完成，`[~]` 条件延后。

## 当前切片

当前激活切片：无（mainline 完成）。

执行规则：同时最多一个 `[-]`；代码、测试和计划状态同一提交；不吸收共享工作区无关改动。

- [x] `GPO-001` 帧诊断记录 + `YA_PROFILING_*` 裁剪
  - 提交：`[gui/debug] record per-frame rebuild rects`。

- [x] `GPO-002` compose GPU 计数
  - 提交：与 GPO-003/004 同提交 `[gui/debug] frame inspector overlay`（overlay 模块共用）。

- [x] `GPO-003` HUD + rebuild flash
  - extraContent 画 overlay，不写入 `snapshot.items`。
  - GPU 计数取自 product replay 之后、overlay 之前的 session。

- [x] `GPO-004` overdraw occupancy
  - 64×64 occupancy；HUD 显示 mean/max/factor；heatmap 走 Overdraw 通道。

## 延后（不在本计划 mainline）

- [~] Editor overlay：`replayUIFrameSnapshot` 之后同一套 `emitGuiFrameInspectorOverlay`，不做 EditorPanel。
