# GUI Frame Inspector

> 建立日期：2026-09-08
> 状态：mainline 完成（GPO-001..005）。诊断工具，不是 dirty-region present / RetainerBox / measure cache。

配套：`todo.md` / `progress.md` / `feature_matrix.json` / `session_checklist.md`。

## 0. 功能

三个独立观测通道：

1. **CPU rebuild**：哪些 widget 本帧重跑了 `paintSelf`（rect + reason）。
2. **Draw call**：Render2D 一次 `flush` = 一次 `drawIndexed`。
3. **Overdraw**：同一帧半透明覆盖次数，与脏区无关。

YA 每帧仍 replay 整份 snapshot。`rebuiltWidgets == 0` 只表示 CPU 缓存命中，GPU 仍画全部 chrome。

## 1. 编译期隔离

复用 `YA_PROFILING_*`（`xmake` mode：`debug`/`profile` → `CONDITIONAL`；`release`/`releasedbg` → `DISABLED`）。不另造 GUI define，不用 `NDEBUG`。

重数据（rect 列表、occupancy、overlay 绘制）走宏裁剪。已有廉价 `GuiPerfStats` 保留。

## 2. 边界

- 不把 overlay 写入产品 `UIFrameSnapshot.items`。
- 不把 HUD 做成被观测树里的 widget。
- 不吸收 Editor/Dock/ColorEdit/Workbench 无关脏改动。
- Overlay 默认关；`profile` 构建也默认关，避免污染 GPU 时间。
