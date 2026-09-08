# MW-001 IRender ownership 矩阵

> 2026-09-08。只读审计，不改 RHI。验收：每个公开 API 标记 device-owned / surface-owned / main-facade；列出可复用、必须 per-window、不能共享的资源。

## 结论

`IRender` 当前把 **device** 与 **唯一 presentation surface** 绑在同一 owner。`VulkanRender::initInternal` 的顺序是：

```text
nativeWindow
  -> VkInstance
  -> VkSurfaceKHR（来自该 window）
  -> findPhysicalDevice（present 能力对该 surface 查询）
  -> VkDevice / queues / VMA
  -> VulkanSwapChain（读 render->getSurface()）
  -> command pool / pipeline cache
  -> sync（按该 swapchain image count）
```

多窗口不能再调 `IRender::create` / `GUIAppHost` 这段 init。目标仍是共享一个 device，每窗一个 surface-context。本文件只分类现有 API，不设计新头。

`IRender::create` 只实例化 `VulkanRender`；OpenGL/DX12/Metal 走 `UNREACHABLE()`。OpenGL 后端源码仍是同一套单窗耦合，本矩阵以 Vulkan 为准；OpenGL 缺口记入 MW-903。

## 1. IRender 公开 API 分类

标记：

- **device**：跨窗可共享，属于 instance/device/queue/allocator/factory。
- **surface**：必须 per-window（或 per-swapchain），不能挂在共享 device 单例上。
- **main-facade**：假定唯一主窗；新多窗口代码禁止依赖。迁移期 `ya::App` / 主 GameRuntime 仍可用。

| API | 分类 | 现有事实 |
| --- | --- | --- |
| `IRender::create` / `init` / `destroy` | main-facade | 一次创建完整 backend + 第一扇窗的 surface/swapchain |
| `setShaderStorage` / `getShaderStorage` | device | 编译/缓存服务，可共享 |
| `begin` / `end` | main-facade | 对唯一 `_swapChain` wait fence → recreate → acquire → submit → present |
| `getWindowSize` | main-facade | 转调唯一 `_nativeWindow` |
| `setVsync` | main-facade | 写唯一 swapchain 的 pending recreate |
| `getSwapchainWidth/Height/ImageCount` | main-facade | 唯一 swapchain 几何 |
| `allocateCommandBuffers` | device | 从 graphics command pool 分配；pool 属 device |
| `waitIdle` | device | `vkDeviceWaitIdle` |
| `beginIsolateCommands` / `endIsolateCommands` | device | 一次性 graphics submit，与 present 无关 |
| `getSwapchain` | main-facade | 返回唯一 `_swapChain` |
| `getNativeWindow` / `getNativeWindowHandle` | main-facade | 唯一 window |
| `getDescriptorHelper` | device | 描述符写工具，绑 device |
| `getResourceFactory` | device | 图像/buffer/view 工厂 |
| `queryTextureFormatSupport` / `isTextureFormatSupported` / `isImageFormatSupported` / `supportsMipGeneration` | device | physical device 能力 |
| `getCapabilities` / `getUniformBufferOffsetAlignment` / `supportsGeometryShader` / `supportsMeshShader` / `supportsComputeIndirect` | device | 同上 |
| `submitToQueue` | device | graphics queue submit；wait/signal semaphore 由调用方传入 |
| `presentImage` | surface / main-facade | 对唯一 swapchain `vkQueuePresentKHR` |
| `getCurrentImageAvailableSemaphore` | surface | `frameImageAvailableSemaphores[currentFrameIdx]`，按唯一 flight 槽 |
| `getCurrentFrameFence` | surface | `frameFences[currentFrameIdx]` |
| `getCurrentFrameIndex` / `advanceFrame` | mixed | `currentFrameIdx` 是 device 上唯一 flight 游标，却驱动唯一 swapchain begin/end |
| `getRenderFinishedSemaphore(imageIndex)` | surface | 按 **该** swapchain image 索引，长度 = 唯一 swapchain image count |
| `createSemaphore` / `destroySemaphore` | device | 通用 VkSemaphore |
| `beginFrameGpuTiming` / `endFrameGpuTiming` / `getLastCompletedFrameGpuTimeMs` | mixed | query pool 按 `flightFrameSize`；与唯一 frame 槽绑定 |
| `queueBeginLabel` / `queueEndLabel` | device | graphics queue debug |

Vulkan 特有、未上 `IRender` 但被 swapchain/host 使用：

| 符号 | 分类 | 事实 |
| --- | --- | --- |
| `VulkanRender::getInstance` | device | 进程级 `VkInstance` |
| `getPhysicalDevice` / `getDevice` / `getVmaAllocator` | device | 共享 |
| `getSurface` | surface | 唯一 `VkSurfaceKHR` |
| `getGraphicsQueues` / `getPresentQueues` | device | queue 可向多个 surface present（需每 surface 再查 `vkGetPhysicalDeviceSurfaceSupportKHR`） |
| `getPipelineCache` / graphics command pool | device | 共享 |
| `onCreateSurface` / `onReleaseSurface` | surface | init 时绑死第一个 `INativeWindow` |
| `_frameIndex` + `DeferredDeletionQueue::flush` | device | `begin()` 里全局 flush；多 surface 仍应共享一条 deferred 队列，按 device frame 推进 |

## 2. 必须 per-window 的状态

不能作为 `VulkanRender` 成员单例：

- `INativeWindow*` / SDL window handle / DPI
- `VkSurfaceKHR`
- `VulkanSwapChain`：`VkSwapchainKHR`、images、extent、format、present mode、`_curImageIndex`、`onRecreate`
- acquire/present 同步：image-available semaphore、per-image render-finished semaphore、in-flight fence
- imported presentation images / image views（`GUIAppHost` 与 `PresentationGraphService` 各自按 **唯一** `getSwapchain()` image count 建一套）
- per-window command buffers（`GUIAppHost` 用 `getSwapchainImageCount()` 分配）
- vsync / present mode（可 per-surface）
- minimized / zero extent / out-of-date 重试状态

`VulkanSwapChain` 已是独立类型，但构造后始终通过 `_render->getSurface()` 取 **那一个** surface。复用该类时必须改为显式传入 surface/window，而不是读 render 单例。

## 3. 可跨窗共享（device-owned）

- `VkInstance`、`VkPhysicalDevice`、`VkDevice`、`VmaAllocator`
- graphics / present `VkQueue`（device 级）
- `VulkanRenderResourceFactory`、descriptor helper、pipeline cache、shader storage
- graphics command pool（串行录制可共享；并行 recorder 才需要 per-thread pool，见 MW-901）
- `TextureLibrary` / `FontManager`（已按 `IRender*` 初始化一次）
- format/capability 查询
- isolate one-time command 路径

**不能共享：** swapchain images、与某次 acquire 绑定的 semaphore/fence、imported present image view、某窗的 WidgetTree/snapshot。

## 4. 不能做成 `IRender[]` 的原因

`GUIAppHost::init` 对每个 host 调 `IRender::create` + `init`：会再造 instance/device/VMA。`RenderRuntime` 同样 `IRender::create`。两个产品入口各持 **一个** 完整 backend，不是 window 数组。

`NativeWindowManager` 已能持有多个 `INativeWindow`，但：

- 只管理 SDL/native 对象；
- 不创建 `VkSurfaceKHR` / swapchain / sync；
- `GUIAppHost` 与 `ya::App` **未使用** 它，各自直接持有一扇 `SDLNativeWindow`。

因此 C1 的 manager 必须消费共享 device + surface-context factory，而不是复制 `GUIAppHost::init`。

## 5. 调用方（main-facade 依赖）

| 调用方 | 用法 |
| --- | --- |
| `GUIAppHost` | `IRender::create`；每帧 `getSwapchain` 导入 present target；`getWindowSize` |
| `PresentationGraphService` | `getSwapchain()` 取 format/image count/`onRecreate`；import 唯一 swapchain images |
| `RenderRuntime` / `RenderRuntimeFrame` | swapchain format、extent、image count、command buffer 数 |
| `EditorSurface::applyWindowMetrics` | `getWindowSize` / `getNativeWindow` / `getSwapchainWidth/Height` |
| `EditorModule::onPresentation` | `getSwapchainWidth/Height` 作 compose extent |
| `AppLifecycle` | `getWindowSize` + `getNativeWindow` 绑 input |
| `RuntimeRenderSettingsSection` | `getSwapchain()->setVsync` |
| `PipelineCoordinator` | `getWindowSize` |

GameRuntime 主窗继续走现有 facade。GUI 辅助窗不得再走这些入口。

## 6. 时序与跨窗约束（给 MW-002）

当前 `begin()`：

1. wait **唯一** `frameFences[currentFrameIdx]`
2. `DeferredDeletionQueue::flush(_frameIndex)`（device 全局）
3. 唯一 swapchain `flushDirtyRecreateAtFrameBegin` / `acquireNextImage`
4. minimized：`imageIndex = -1` 跳过

当前 `end()`：submit（wait image-available，signal per-image finished）→ present 唯一 swapchain → `currentFrameIdx++`。`flightFrameSize` 现为 **1**。

`findPhysicalDevice` 用 **第一个** `_surface` 做 `vkGetPhysicalDeviceSurfaceSupportKHR`。第二扇窗必须对 **自己的** surface 再查 present 支持，不能假设与第一扇相同 family；queue 本身仍是 device-owned。

`PresentationGraphService` 绑定唯一 `onRecreate`，且 `createPresentationRenderTexture` 直接 `as<VulkanSwapChain>()`。它必须继续只服务主 GameRuntime 窗；GUI 辅助窗用 window-local imported target。

## 7. 本 checkpoint 边界

- 保留：现有 `IRender` 主 facade、单窗 `ya::App` / `GUIAppHost` 行为。
- 未完成：MW-002 共享表与双窗 acquire 顺序；不在此文件展开 Render2D/UBO。
- 偏离：无。未改 Engine 源码，未新增 opaque handle 或 surface-context 接口。
