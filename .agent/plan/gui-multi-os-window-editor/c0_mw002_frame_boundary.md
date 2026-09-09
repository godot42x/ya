# MW-002 frame-boundary / 资源共享表

> 2026-09-08。只读审计，不改 RHI，不实例化第二套 Render2D session。依赖 [`c0_mw001_irender_ownership.md`](c0_mw001_irender_ownership.md)。

## 结论

> 2026-09-09：现状以 [`plan.md`](plan.md)「冻结」节为准。下文「`IRender::begin/end`」是审计当时的调用链；代码已迁到 `IRenderSurfaceContext::begin/end`。

当前帧循环把 **device fence、唯一 swapchain acquire/present、全局 DeferredDeletion flush、静态 Render2D session** 串在一次 surface `begin/end` 里。双窗必须拆成「每 surface 自己的 acquire/submit/present」，device 级资源仍共享；CPU 在一个 `AppKernel` 里 **串行** 录制各窗。`Render2D::session` 继续静态单例，但 **每窗必须有唯一 pass slot**，否则同 flight 的 UBO/vertex 会互相覆盖。

`flightFrameSize`（VulkanRender，现为 1）、`MAX_FLIGHTS_IN_FLIGHT`（RenderDefines，2）、`DeferredDeletionQueue::init(1)`（仅 RenderRuntime）三者不一致。GUIAppHost **从不 init** 延迟删除队列。

## 1. 现有时序（单窗）

```text
INativeWindow
  -> VkInstance
  -> VkSurfaceKHR
  -> VkPhysicalDevice（present 对该 surface 查询）
  -> VkDevice / queues / VMA / factory
  -> VulkanSwapChain（读 render->getSurface()）
  -> createSyncResources(swapchainImageCount)
       image-available × flightFrameSize
       frame fence     × flightFrameSize
       render-finished × swapchainImageCount
  -> host import swapchain images（GUIAppHost 或 PresentationGraphService）
  -> allocateCommandBuffers(imageCount)
```

每帧（`GUIWindowHost` / `RenderRuntime::beginFrameCommandBuffer`）：

```text
resize pending? -> swapchain->requestRecreate()
minimized?      -> waitIdle, skip
IRender::begin
  wait frameFences[currentFrameIdx]
  DeferredDeletionQueue::flush(_frameIndex)   // GUI 路径队列通常未 init
  flushDirtyRecreateAtFrameBegin
  imageCount==0? imageIndex=-1, return
  acquireNextImage(image-available, fence)
  OUT_OF_DATE? deviceWaitIdle + recreate + acquire 再试
rebuild imported targets if handle/extent/count changed  // GUI: waitIdle 整 device
updateUI + buildSnapshot
cmdBuf[imageIndex] record
  FontManager::flushPendingGlyphs   // 录制前
  Render2D::begin(session, passSlot by compose kind)
  ...
  Render2D::end
IRender::end
  submit wait image-available, signal render-finished[imageIndex], fence
  present unique swapchain
  currentFrameIdx = (currentFrameIdx+1) % flightFrameSize
```

`Render2D::init` 在 `GUIAppHost` 里对 **唯一** swapchain format 调一次。`composePassSlot(kind)` 静态拿 5 个 slot（`kMaxPassSlots = 8`），按 compose **种类** 隔离，不是按 window。

## 2. 双窗目标顺序（仍一个 loop）

```text
for each GUIWindowHost (not closing):
  if minimized: skip present; do not acquire
  surface.begin                    // wait 该 surface 的 fence
  if imageIndex < 0: skip
  if this swapchain recreated: rebuild 该窗 imported images/cmdbufs
       只 wait 该 surface fence；禁止 vkDeviceWaitIdle（会卡住其他窗 in-flight）
  tick + snapshot（该窗 WidgetTree）
  Render2D::begin({cmdBuf, extent, uniquePassSlot})
  record
  Render2D::end                    // session 必须空，再进下一窗
  surface.submit + present         // 该 swapchain
device.advanceFrame / DeferredDeletion flush once per kernel tick
  在所有本 tick 已 wait 的 surface fence 之后
```

禁止：每窗一个 while-loop；嵌套 `Render2D::begin`；两个窗共用 `RuntimeUIComposite` 那一个 slot 且同时 in-flight。

## 3. 异常窗对其它窗

| 事件 | 该窗 | 其它窗 |
| --- | --- | --- |
| minimized / zero extent | 不 acquire；`imageIndex=-1` | 照常 acquire/present |
| OUT_OF_DATE / SUBOPTIMAL | 只 recreate 该 surface 的 swapchain + 其 imported images | 不碰其 swapchain；不要 `vkDeviceWaitIdle` |
| resize | frame-boundary 标 dirty；下一 `surface.begin` 再 recreate | 同左，互不共享 dirty 标志 |
| close | wait 该 surface in-flight → retire 其 swapchain/surface/sync/imported images → destroy native window | tree/session 继续；device 保留 |
| 关最后一扇 / 关主窗 | 现产品：退进程 | n/a |
| GPU validation use-after-free | 根因几乎总是 waitIdle 全局化或共享 pass slot | — |

当前 `GUIWindowHost` 最小化跳过 present；swapchain recreate 只 wait 该 surface fence（MW-202）。禁止再引入 `vkDeviceWaitIdle` 卡住其他窗。

## 4. 共享 / 独占表

### 共享（device）

- `VkInstance` / `VkPhysicalDevice` / `VkDevice` / VMA / pipeline cache
- graphics + present queues（第二 surface 仍要自己查 present support）
- `IRenderResourceFactory`、descriptor helper、shader storage
- graphics command pool（串行录制）
- `TextureLibrary`、`FontManager` atlas（glyph flush 仍在各窗录制前，device 安全点）
- `DeferredDeletionQueue` **一条**，按 device frame 推进；GUI 多窗必须 `init`，不能继续走未初始化的立即销毁
- `Render2D` 静态 session **串行复用**（begin/end 配对，不并行、不嵌套）

### 独占（per surface / per window）

- `INativeWindow`、`VkSurfaceKHR`、`VulkanSwapChain`、swapchain images
- image-available / render-finished / frame fence
- command buffers、imported present `GUIRenderSurface`
- WidgetTree、snapshot、focus/capture、DPI 逻辑 extent
- Render2D **pass slot**（以及该 slot 下 `MAX_FLIGHTS_IN_FLIGHT` 份 UBO / vertex / descriptor）
- vsync / present mode / minimized / pending recreate

### 不能共享

- 一次 acquire 得到的 image index 与配套 semaphore
- 某窗 command buffer 录制期引用的 image view（至少活到该窗 queue submit 完成）
- `getCurrentFrameIndex()` 若仍表示「唯一 swapchain 的 flight」，不能当所有窗的 UBO 下标；要么每 surface 自己的 flight，要么 device flight + 每窗 slot 已隔离

## 5. Flight / UBO / descriptor / pass slot

| 计数器 | 值 | 绑定 |
| --- | --- | --- |
| `VulkanRender::flightFrameSize` | **1** | 唯一 swapchain 的 fence / image-available |
| `MAX_FLIGHTS_IN_FLIGHT` | **2** | Quad/Line/Forward/Deferred UBO 与 vertex 数组 |
| `DeferredDeletionQueue` | RenderRuntime `init(1)`；GUI **未 init** | `VulkanRender::begin` 仍 `flush(_frameIndex)` |
| `FQuadRender::kMaxPassSlots` | **8** | 现占用 5（compose kind）；多窗 GUI 每窗至少再占 1 |
| `composePassSlot(kind)` | 静态 5 slot | 按 kind 不是按 window |

`FQuadRender::begin` 用 `_render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT` 选 flight 资源。今日 `flightFrameSize==1`，frame index 几乎恒 0，第二 flight 槽闲置。多 surface 若仍共享 `currentFrameIdx` 且共享 pass slot，两窗同一 CPU 帧会写同一 UBO。

C2/`MW-203`：每窗唯一 pass slot；静态 session 仅串行；不在本计划实例化第二 `Render2D` owner（MW-901）。

World graph UBO（Forward/Deferred `MAX_FLIGHTS_IN_FLIGHT`）只服务主 `RenderRuntime` 窗；GUI 辅助窗不碰。

## 6. Render2D session 边界

`FRender2dSession` 是进程静态：`curCmdBuf`、extent、clipStack、pending batch。`end()` 断言无未 flush 几何并把 `curCmdBuf` 置空。

双窗合法用法：窗 A `begin/end` 完成后再窗 B `begin/end`。非法：两窗同时 `session.curCmdBuf` 非空；或 B 开始时 A 的 vertex 还在同一 slot 的 in-flight GPU 上。

`FontManager::setActiveDpiScale` 是进程全局。两窗不同 DPI 时，串行录制下应在该窗 snapshot/glyph 前改 DPI，录完再切下一窗；不能假设全局 DPI 稳定。不在 MW-002 改代码。

## 7. OpenGL

`IRender::create` 对 OpenGL `UNREACHABLE()`。`OpenGLState::makeCurrent` / `swapBuffers` 绑 **一个** `SDL_Window`。若未来启用：每窗 record/present 前 `MakeCurrent` 该窗 context。记入 MW-903，首轮 Vulkan。

## 8. RenderRuntime / PresentationGraphService

`RenderRuntime::beginFrameCommandBuffer` 现用 `getPrimarySurfaceContext()->begin()`；world 写离屏 Camera RT。`PresentationGraphService` 注入该 surface，只做 display compose。C0 当时写的 `IRender::begin` / `getSwapchain()` 已删除。辅助 GUI 窗仍不得复用这套 service。N Camera 见 `c2_view_model.md`，C2 前冻结。

## 9. 本 checkpoint 边界

- 保留：静态 Render2D session、主窗 PresentationGraphService。当时 `IRender::begin/end` 仍存在；2026-09-09 已迁到 `IRenderSurfaceContext`（见第 8 节补记）。
- 未完成：MW-003 tab 分类、MW-004 层契约冻结；C1/C2 实现。
- 偏离：无。未改 Engine，未新增 surface-context 接口，未实例化第二 Render2D session。
