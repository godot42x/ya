# RenderGraph 前置依赖评估

更新时间：2026-08-02（执行前审查）

## 结论

当前 RenderGraph 清晰化不需要先重构 Scene、AssetManager、ResourceResolveSystem 或引入独立 render thread。
主要阻塞点集中在 RenderGraph 自身以及它与 `RenderRuntime`、resource registry、command buffer 的边界。

本计划不能作为独立迁移主线执行。仓库已有
`.agent/plan/frame-graph-orchestrator-migration/`，其中已经定义 stable key、transient buffer
reuse、upload arena、FrameResourceSet 和 Deferred-first 迁移顺序。本计划必须作为 graph core
子计划，避免重复实现或改变这些任务的 owner。

## 当前事实

### 已有稳定边界

- `RenderRuntime::renderFrame()` 管理 command buffer 的 acquire、world recording、presentation recording、submit/present。
- Deferred 主路径使用 `prepare() + executeCompiled()`。
- Presentation graph 通过独立 executor 执行。
- Scene/ECS 通过 frame input / draw extraction 向 pipeline 提供渲染意图，不直接拥有 graph 资源。
- AssetManager / ResourceResolveSystem 负责源资源和派生资源，不负责 graph pass 编译。

### 必须修复的 graph 内部问题

1. `execute()` 会做 imported finalization，而 `prepare() + executeCompiled()` 不会，执行契约不一致。
2. setup 只声明 usage，execute lambda 仍决定 attachment、load/store、final layout 和 rendering begin/end，存在双重真相。
3. compiled graph 只有全局 state vectors，executor 执行时按 pass 扫描全表，compiled product 不完整。
4. `RGPass` 没有显式 Raster / Compute / Copy kind，compile 和 validation 只能从 usage 猜 intent。
5. Imported / Persistent / Transient 的稳定 identity 和 replacement contract 还不够明确。

### 本次审查发现的计划问题

1. 原计划把 Forward 描述得过于完整；实际 Forward 主 surface 仍是 Stage 固定顺序。
2. 原计划把 transient physical aliasing 放在“可延后”位置，与主计划 FG-106~FG-110 冲突。
3. `ComputeDispatchPlan` / `CopyPlan` 只是未来形态，当前代码没有对应的 dispatch declaration consumer，
   首轮不应提前设计。
4. “所有 pipeline 统一验证”会把 Forward 迁移误当成 graph core 的完成条件。
5. 原计划缺少与主计划任务的映射，执行时容易出现两个 owner 同时修改 `RenderGraph.h/.cpp`。

### 执行约束

- graph core 任务先改公共 contract 和 core tests，再迁一个真实 Deferred consumer。
- 与 FG-101~FG-111 重叠的资源 identity、buffer reuse、upload arena 改动必须由主计划任务持有；
  本计划只提供接口和编译产物需要的 seam。
- 没有真实 consumer 的 API 不进入首轮公共接口。

## 不是本轮 blocker 的依赖

- Scene clone / PIE dirty 语义
- ResourceResolveSystem 的队列化或派生资源缓存
- AssetManager 的 source cache
- 专用 render thread
- async compute、多 queue、完整 transient alias allocator

这些系统只需要提供稳定的输入、输出和生命周期边界，不需要先做架构重写。

## 判断标准

如果某个改动不能改善以下至少一项，就不应作为本轮前置工作：

- declaration 是否能表达完整 pass intent
- compile/execute 是否语义闭环
- resource identity/lifetime/state contract 是否清晰
