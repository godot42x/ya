# RenderGraph 依赖 Gate

> 本文件属于 `frame-graph-orchestrator-migration` 的 graph core 子计划。
> transient buffer physical reuse 的完成门禁以主计划 FG-106~FG-110 为准。

## Gate 0：运行边界

允许的边界：

```text
Scene/ECS -> frame intent
RenderRuntime -> acquire / submit / present
RenderGraph -> pass/resource declaration, compile, command recording
ResourceRegistry -> graph resource resolution and ownership
```

Scene、AssetManager、ResourceResolveSystem 不进入 graph 内部状态机。

## Gate 1：执行闭环

必须满足：

- `prepare + executeCompiled` 与 `execute` 同语义
- imported texture/buffer final state 不依赖调用者记忆
- command recording 期间引用的资源保持存活

## Gate 2：编译产物

必须满足：

- 每个 pass 有自己的 barrier/rendering plan
- executor 不再扫描全局 state vector
- callback 不重新推导 graph 依赖

## Gate 3：可扩展资源模型

为后续能力预留：

- persistent stable identity
- transient lifetime interval
- physical slot/alias plan
- subresource 粒度 state
- async compute / multi-queue 扩展点

Gate 3 不是本子计划的最小闭环 blocker，但它仍是完整 RenderGraph 迁移的硬门禁；
本子计划只负责提供 metadata 和 compiled-plan seam。

> 2026-08-03：主计划已完成 FG-101~FG-110，本 Gate 中 persistent stable identity、
> transient lifetime interval、physical slot/alias plan 已由"seam"落地为真实实现；
> 剩余未落地项为 subresource 粒度 state 与 async compute / multi-queue（见
> `async-queue-assessment.md`，均为主计划后续阶段）。
