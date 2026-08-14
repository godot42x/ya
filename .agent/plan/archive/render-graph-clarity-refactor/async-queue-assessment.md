# RG-0605：Async Compute / Multi-Queue State Model Assessment

> 结论日期：2026-08-02

## 结论

当前 RenderGraph 不应直接实现 async compute 或 multi-queue。
现有 graph core 可以继续作为单 queue（graphics queue）执行模型使用，但还没有足够的底层契约承载跨 queue 调度。

这不是“再加一个 pass kind”就能解决的问题。跨 queue 会同时改变资源状态、compiled plan、提交同步和资源退休边界。

## 当前代码证据

### 1. Resource state 没有 queue ownership

- `ImageResourceState` / `BufferResourceState` 只记录 stages、access、layout、range。
- `ResourceStateTracker` 只按 image subresource 跟踪状态。
- `RenderGraphExecutor` 的 buffer state map 以 `IBuffer*` 为 key，没有 queue domain 或 submission epoch。
- graph barrier plan 只描述同一 command buffer 内的 state transition。

因此当前模型无法表达：

- graphics/compute queue family ownership release/acquire；
- 同一资源在不同 queue 上的 last writer / next reader；
- semaphore wait 之前和之后的 resource state；
- queue-specific completion 用于 deferred deletion。

### 2. Command buffer / submit 层只有单一 graphics 提交路径

- `VulkanRender::submitToQueue()` 固定提交 `_graphicsQueues[0]`。
- `VulkanQueue::submit()` 使用 binary semaphore 参数，没有 timeline value 或 per-queue submission token。
- wait stage mask 是单个固定的 `VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`，不能表达 compute-only wait。
- `VulkanCommandBuffer` 绑定一个 `VulkanQueue*`，但 RenderGraph executor 没有 queue domain 输入。

Present queue 的现有 semaphore 只解决 swapchain acquire/present，不构成通用 graph multi-queue contract。

### 3. 生命周期与回收也还没有跨 queue 语义

当前 deferred deletion、imported resource keep-alive 和 graph registry retirement 都以现有 frame/submit 边界为准。
如果资源被 compute queue 使用，graphics fence 完成并不能证明 compute queue 已经完成，直接复用或退休会产生 Vulkan lifetime 风险。

## 后续真正需要的前置契约

在实现 async compute 前，主计划至少需要先完成：

1. `QueueDomain` / queue capability 描述，以及每个 pass 的 queue assignment。
2. compiled pass plan 的 cross-queue dependency 和 wait/signal plan。
3. resource state 增加 queue family/domain owner。
4. submit token（优先 timeline semaphore）和 per-queue completion tracking。
5. executor / runtime 能够按 queue domain 录制、提交并等待。
6. transient alias / pool / deferred deletion 使用跨 queue completion token。
7. wait stage mask 从单值改为 per-wait dependency 的 stage mask。

## 决策

- `RG-0605` 在本子计划中以 assessment-only 完成。
- 本轮不新增 `QueueDomain`、timeline semaphore 或 async executor API。
- 在 `FG-107~FG-111`、Deferred frame resource owner 和 submit/lifetime contract 收口前，不启动 async compute 实现。
- 当前 RenderGraph 继续明确定位为 graphics-queue graph；Compute pass kind 只表示 command semantics，不表示独立 compute queue。
