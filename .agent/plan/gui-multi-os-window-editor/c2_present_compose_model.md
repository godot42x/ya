# Present ≠ Viewport：compose 对象模型

> 2026-09-09。冻结 present surface、swapchain image、离屏 Camera RT 的边界。
> 方向索引在 [`plan.md`](plan.md)「冻结：device / present / camera」。本文件是 compose 细节；与代码冲突时先改代码对齐这里，不要再把 Camera 绑回 swapchain。

## 问题

`IRender::primarySwapchain()` / `primaryFrameIndex()` 把三件不同的事绑成一件：

1. 共享 GPU device
2. 世界/viewport 画到哪里
3. 这一帧某个 OS 窗口 present 哪一张 image

viewport 不是 present surface。surface 在 present 时只决定 **这一扇窗要把哪些 swapchain image compose 上去**。那些 image 是该窗 swapchain 创建出来的。真正的渲染目标是另外创建的 `RenderTexture`。

## 三层对象

```text
IRender（device）
  instance / physical / logical device / queues / VMA
  resource factory / pipeline cache / command pool
  DeferredDeletionQueue（device 时钟，不是“最后 acquire 的那扇窗”）

IRenderSurfaceContext（一扇 OS 窗口）
  INativeWindow + VkSurfaceKHR + ISwapchain
  acquire / present / per-surface fence+semaphore
  只是 present 目的地
  begin() 得到 imageIndex：本帧要写入并 present 的那张 swapchain image
  不决定 world 分辨率、不决定 viewport 格式、不持有 GBuffer

Camera / WorldView（RenderRuntime pipelines）
  一台相机一条链：graphics → UI → view compose，写该相机的离屏 RT
  尺寸来自该 Camera 的 extent（host 写入 `CameraFrameInput.viewportRect`）
  不 acquire、不读 swapchain extent/format、不订阅 onRecreate
  见 [`c2_view_model.md`](c2_view_model.md)
```

`IRender::getPrimarySurfaceContext()` 只是 **device pick 用的第一扇 surface**（`createSurface()` 仍在 `findPhysicalDevice` 之前）。它不是 “viewport 住在这里”。

Fullscreen 游戏 “viewport == 窗口” 是 **host 策略**：把窗口 framebuffer 尺寸写进 `viewportRect`。pipeline 禁止自己去读 swapchain 来猜 viewport。

## 已有、应对齐的事实

世界图已经画到 pipeline 离屏 RT（`getViewportDisplayImageShared()` = postprocess 或 raw viewport）。
`PresentationGraphService` 已经把 swapchain image import 成 `RenderTexture`，再把 viewport 图 composite 上去。
`GUIRenderSurface` 已经区分 owned offscreen 与 wrapExternal(imported swapchain)。
`GUIAppHost` 已经是：acquire → compose snapshot 到 `swapchain[imageIndex]` → present。
`EditorViewportCompositor` 已经另建 editor viewport RT，再被 chrome UI sample。

真正的 bug 源是 **错误共享时钟和尺寸**：

| 错误绑定 | 正确归属 |
| --- | --- |
| `_commandBuffers[swapchain.imageIndex]` | 录制 flight：`CameraFrameInput.flightIndex` / `MAX_FLIGHTS_IN_FLIGHT` |
| viewport 空时 fallback 到 swapchain extent | host 写 `CameraFrameInput.viewportRect`；空则跳过 world，不要猜 |
| pipeline `bPostprocessOutputIsSRGB` 读 swapchain format | 离屏 RT 自己的 format；swapchain sRGB 只属于 compose 到 present image 的那一 pass |
| `PresentationGraphService` 经 `_render->primarySwapchain()` 找图 | 构造时注入 `IRenderSurfaceContext*`；`onRecreate` 只重建 **这一扇** imported images |
| Render2D / debug primitives 读 `primaryFrameIndex()` | 录制 flight / pass slot；禁止 “最后 acquire 的那扇窗” |
| cmdbuf 数量 = swapchain image count | world：flight 数；present import：该 surface 的 image count |

## 帧（一个 AppKernel，串行）

单窗、world + present 仍可共用一条 command buffer（acquire 必须在这条 buffer 开始录制之前，否则无法 wait `imageAvailable`）。这 **不** 表示 world 画到了那张 swapchain image。

```text
optional: device DeferredDeletion flush（等过 device-frame owner 的 fence）

for each Camera that renders this frame（无 acquire）:
  graphics → 该相机离屏 RT
  UI pass → 仍写该 RT（不进 bloom）
  view compose → overlay / gizmos，仍写该 RT

for each surface that will present:
  context.begin → imageIndex
  display compose（选出的 Camera RT 和/或 chrome snapshot）→ swapchain[imageIndex]
  context.end
```

多窗以后 world 与 present 可以拆成两次 submit（world 不 wait imageAvailable）。本阶段不实现第二次 submit，但类型上必须已经分清 **flight** 与 **imageIndex**。

## 三套时钟（禁止混用）

1. **Surface present flight**：`IRenderSurfaceContext::getCurrentFrameIndex`、swapchain `imageIndex`、`onRecreate`、extent/format。消费者只有 importer：`PresentationGraphService`、`GUIPresentationTarget`。
2. **Camera / WorldView extent**：该相机离屏 RT。迁移期 `ViewportState` / `FrameInput.viewportRect` 等于 `WorldView[0]`。消费者：pipelines、GBuffer、该相机 UI pass。
3. **Recording flight + DeferredDeletionQueue**：device/session / Render2D pass slot。不是 “哪扇窗刚 acquire”。

单窗碰巧 `flightIndex == present.getCurrentFrameIndex() % MAX_FLIGHTS`（begin 前读、end 才 advance）。这是巧合，不是 API。

## 谁拥有 compose

具名录制入口（R-3）：`recordCameraViewCompose` 写 Camera 离屏 display RT；`PresentationGraphService::recordDisplayCompose` 写该窗 `swapchain[imageIndex]`。`GUIRenderSurface` 只是 compose target（`isDisplayComposeTarget()` = `PresentSrcKHR`），不 acquire、不 present、不读 live WidgetTree。

acquire / present / recreate / zero-extent（R-4）：只出现在 `IRenderSurfaceContext` 与 host `FPresentFrame` coordinator（`GameRuntimeFrameOrchestrator`、`presentGuiSnapshot`、`GUIAppHost`）。`RenderRuntime` 只在已 acquire 的 `PresentFrameInput` 上录制。

| 产品路径 | 离屏内容 | present 时 compose 到 |
| --- | --- | --- |
| 独立 GUI app | 无 world；可选 offscreen parity RT | 该窗 `swapchain[imageIndex]`（`GUIPresentationTarget`） |
| GameRuntime 主窗 | world + game UI → viewport display RT | `PresentationGraphService` 写该窗 swapchain images |
| GameEditor 主窗 | world → viewport RT → `EditorViewportCompositor` RT → chrome snapshot | 同上，presentation graph 的 source 是 chrome+viewport 合成结果 |
| 额外 GUI 窗（未接 present） | 自己的 WidgetTree snapshot | 将来：自己的 surface + imported swapchain images；**不**走 `PresentationGraphService` |

`PresentationGraphService` 继续只服务 **主 world 窗**。辅助 GUI 窗走 window-local imported target + GUI compose。多 world window 另开 checkpoint。

## 本轮编码切口（MW-201 follow-up：compose model）

做：

- `PresentationGraphService` 注入 `IRenderSurfaceContext*`，禁止 `_render->primarySwapchain()`
- world cmdBuf 按 `MAX_FLIGHTS_IN_FLIGHT` / `flightIndex` 分配，不再按 swapchain image
- viewport 不再 fallback 到 swapchain extent；host 在 rect 为空时用窗口尺寸做 fullscreen 策略
- pipeline 初始尺寸来自 host 给出的 viewport，不读 `primaryWindow()`
- GUI host 经自己持有的 `IRenderSurfaceContext*` 取 swapchain，不经 `IRender::primarySwapchain()`

不做：

- extra GUI present、Feature Gallery `Windows`、ES-1
- 把 world 与 present 拆成两次 submit
- 把 `IRender` 改成数组，或复制 `IRender::create`

MW-202 已做：`VulkanSwapChain::recreate` 不再 `vkDeviceWaitIdle`；见 `IRenderSurfaceContext::waitInFlight`。

## 验证

- `ya-rhi-vulkan-smoke` 的 `RHISurfaceContext.*`
- `ya-gui-host`、`ya-game-runtime` 能编过
- `ya-gui-headless-host-test`
