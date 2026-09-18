# 不可上屏 surface：最小化 / zero extent / delay present

> 2026-09-09。方向索引：[`plan.md`](plan.md)「冻结」节。**MW-206** 已落地（排在 extra `renderAll` 之前）。

## 问题（曾是债）

MW-202 把「最小化就不 `vkDeviceWaitIdle`」做对了，但仍然把 **不可上屏** 当成 **暂停这扇窗甚至整进程**：

| 现状 | 错在哪 |
| --- | --- |
| `GameRuntimeFrameOrchestrator`：`_bMinimized` → `sleep(100)`，整帧 logic+render 都跳 | 多窗时一扇最小化不应停 AppKernel、其它窗、Camera 链 |
| `GUIWindowHost`：`bWindowMinimized` 直接 `return`，连 snapshot 都不建 | 跳过的应是 display compose + present，不是 tree tick |
| `GUIWindowManager::tickAll`：`bMinimized` 则 continue | extra 窗最小化仍应 tick/input；只是将来 `renderAll` 不 acquire |
| `RenderRuntime::beginFrameCommandBuffer`：swapchain 0 extent 则整段 world 不录 | Camera 写离屏 RT，不该绑 swapchain 是否可 acquire |
| `VulkanSwapChain::recreate`：extent 0 跳过并留下 dirty | 这一条是对的：**delay recreate** 到恢复 |

「delay 渲染」不是把跳过的帧攒起来补 present，而是：

1. **Delay swapchain recreate / acquire / present** 直到 extent > 0。
2. **不 delay** AppKernel、其它窗、未绑定这扇 swapchain 的 Camera。
3. 恢复后从当前帧继续，不追帧。

## 三层（与 present / Camera 冻结对齐）

```text
AppKernel / logic tick          进程级。最小化不是 pause。
Camera / WidgetTree             不依赖 swapchain。最小化默认仍 tick；
                                仅当 host 策略说「没有消费者」才跳过 GPU graphics。
PresentSurface                  不可上屏：不 acquire、不 display compose 到 swapchain、不 present。
```

`IRenderSurfaceContext::begin` 返回 `imageIndex == -1` 只表示 **这扇窗本帧不能 present**，不是「这帧什么都别做」。

Fullscreen 游戏「唯一窗口最小化」是 **host 策略**：可以停 Camera GPU 省电；策略写在 GameRuntime/GUI host，不写进 `VulkanSwapChain`，也不写进 `RenderRuntime` 的 acquire 前置条件。

## 本窗 vs 其它窗

| 事件 | 该 PresentSurface | 该 Camera / WidgetTree | 其它窗 / AppKernel |
| --- | --- | --- | --- |
| minimized / zero extent | 不 acquire、不 present；recreate 保持 dirty | 默认仍 tick/snapshot；graphics 仅按 host 策略跳 | 照常 |
| restore | 下一帧 begin 再 recreate + acquire | 照常 | 照常 |
| 关窗 | wait 该 surface in-flight 后销毁 | 随 window 销毁 | 照常 |

禁止：用进程级 `_bMinimized` 代替 per-surface 不可上屏；最小化时 `waitIdle` 整 device；把跳过的 present 做成队列。

## 编码切口（MW-206）

已做：

- `IRenderSurfaceContext::isPresentable()`：native `isMinimized` 或 window/surface extent 0。`begin` 不可上屏时不 reset fence、返回 -1；delay recreate 保持 dirty，且不覆盖上次成功的 swapchain extent。
- GUI host：不可上屏仍 `updateUI` + snapshot；只跳过 compose/present。`GUIWindowManager::tickAll` 最小化 extra 仍 tick。
- GameRuntime：去掉 `_bMinimized` + `sleep(100)`；logic 照常。`RenderRuntime::begin` 返回 -1 时 skip GPU（单 cmdBuf 迁移，注释已写清）。
- 测试：`RHISurfaceContext.ExtraWindowUnpresentableDoesNotBlockPrimaryPresent`；`GUIWindowManagerTest.MinimizedExtraStillTicksAndSnapshots`。

不做：N Camera、extra `renderAll`、把 WidgetTree 改成按 minimize 冻结输入路由。

## 验证（MW-206）

- 双窗：最小化 extra，primary 继续 present；extra 恢复后下一帧 recreate 成功。
- 不可用 `sleep` 冒充最小化节流。
- 不回归 MW-202：recreate 仍不得 `vkDeviceWaitIdle`。
