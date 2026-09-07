# GUI Frame Inspector TODO

> 更新时间：2026-09-08
> 状态：`[ ]` 未开始，`[-]` 进行中，`[x]` 完成，`[~]` 条件延后。

## 当前切片

当前激活切片：`GPO-002`。

执行规则：同时最多一个 `[-]`；代码、测试和计划状态同一提交；不吸收共享工作区无关改动。

- [x] `GPO-001` 帧诊断记录 + `YA_PROFILING_*` 裁剪
  - 提交：`[gui/debug] record per-frame rebuild rects`。

- [-] `GPO-002` compose GPU 计数
  - 依赖：GPO-001。
  - 工作：Render2D session 真实 flush/vtx 计数；写入 inspector 包；CPU model flush 并列。
  - 非目标：不画 overlay。
  - 提交：`[gui/compose] export frame draw-call stats`。

- [ ] `GPO-003` HUD + rebuild flash
- [ ] `GPO-004` overdraw occupancy
