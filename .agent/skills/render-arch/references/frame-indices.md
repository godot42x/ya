## 帧索引与缓冲规范（多窗口世界的五个计数器）

单窗口时代 `hostTick ≈ deviceFrameIndex ≈ surfaceSlot`，混用不出错；多窗口后影子消失，
每个量必须用在自己的问题上。判据只有一条：**资源的复用由谁守卫，就用谁的索引。**

| 量 | Owner | 环大小 | 推进时机 | 合法消费 |
| --- | --- | --- | --- | --- |
| `hostTick`（`App::_hostTick`） | 应用 | 单调无环 | 每次 `iterate` | UBO `frameIdx`、动画、frame token、automation 对账 |
| device frame index（`IRender::recordedFrameIndex()`） | 渲染设备 | 单调无环 | `beginRecordedFrame()`（唯一推进点） | frame fence 槽、deferred deletion 世代、GPU timing 环 |
| flight index（= deviceFrameIndex % framesInFlight，纯函数） | 设备帧序的派生 | `kFramesInFlight`（device 环，唯一环大小源） | 不自己推进 | per-frame command buffer、submission 槽与 keepalive、View 发布表 |
| surface frame index（`currentFrameIdx`） | **每个 surface 自己** | `flightFrameSize`（= `kFramesInFlight`） | 每次 `surface->begin()` | image-available semaphore 环、present-complete fence 环 |
| swapchain image index（acquire 返回值） | swapchain / driver | image count（driver 协商） | 每次 acquire | 该图本体、per-image render-finished semaphore、GUI per-image command buffer / compose target、present fallback |

规则：

1. **flight index 是 device 的量，不是窗口的量。** 它是 device frame index 的纯函数
   （`resolveFlightIndex`），守卫它的 fence 是 device frame fence。任何"用 device 计数器
   推 surface 环"或反过来的写法都是旧单窗口遗留，多窗口下静默错位。
2. **surface 环各走各的。** 窗口最小化/acquire 拒绝时该 surface 的环停走（不 acquire 就
   不推进），device frame 环照常走，其他窗口不受影响——这正是分层的目的，不是要修的坑。
   多窗口后不存在"全局的 per-swapchain frame index"，一窗口一个。
3. **image index 只在 acquire→present 之间有效**，由 driver 决定（可乱序可重复）。录制期
   禁止回查 swapchain 取图（用 plan 携带的 acquired token，见 `PresentFrameInput::imageIndex`）。
   GUI 按 image index 组织 per-image 资源是合法模式（守卫 = 图被重新领走）。
4. **`hostTick` ≠ device frame index。** 前者是产品节拍（UBO/动画/frame token 的轴），
   后者是 GPU 帧世代。今天 1:1，但设备建立前、headless、跳渲染时分叉；字段名必须区分
   （`FramePacket::hostTick` / `RenderStageContext::hostTick`）。
5. **缓冲深度的旋钮只有一个：`kFramesInFlight`**（`RenderDefines.h`，device 的 CPU/GPU
   重叠深度，全引擎共享）。`MAX_FLIGHTS_IN_FLIGHT` 只是 per-frame 表的容量上界（有
   `static_assert` 保证环 fits）。swapchain image count 不归这个旋钮管——它与 presentation
   engine 协商（`minImageCount` 夹 capabilities），索引的是图不是帧。调大
   `kFramesInFlight` 前先读 `temporal_semantics.md` M4：所有 per-frame 表要一起验证轮转。


