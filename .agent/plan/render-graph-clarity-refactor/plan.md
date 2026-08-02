# Render Graph 清晰化与能力收敛计划

> 状态：2026-08-02 执行前审查后修订。
>
> 本计划不假设当前 RenderGraph “不可用”，而是把它视为一个已经进入主渲染路径、但仍处于过渡态的第一版实现。
> 目标不是推倒重来，而是在保留现有主链可运行性的前提下，把它从“可用但不清晰”收敛成“结构清晰、语义闭环、可继续扩展”的渲染编排核心。

## 1. 计划定位

本目录是 `../frame-graph-orchestrator-migration/` 的 RenderGraph core 子计划，
不是另一条独立的渲染迁移主线。

- `frame-graph-orchestrator-migration` 负责顶层 Deferred/Forward 编排、FrameResourceSet、
  typed pass parameters、transient buffer physical reuse 和 pipeline 迁移。
- 本计划负责 RenderGraph declaration/compile/execute contract、executor 语义和 graph-local
  resource/lifetime contract。

如果两份计划出现冲突，以主计划的真实 consumer、资源 owner 和提交依赖为准。

## 2. 背景与结论

当前工程中的 RenderGraph 已经承担了真实工作：

- Deferred 主路径已 graph-backed。
- Presentation、Deferred 主图、SSAO/Postprocess/Shadow utility 等路径已接入 graph 或 executor；
  Forward 目前只有 shadow group 和 viewport 外壳部分 graph-backed，主 surface pass 仍由 Stage 固定顺序执行。
- `RenderGraph / RenderGraphExecutor / RenderGraphResourceRegistry` 已经不是实验代码，而是运行时真实依赖。

但当前实现仍存在明显的“过渡态”特征：

1. graph setup 与 pass execute 之间存在双重真相。
2. attachment/load/store/final layout 大量散落在 execute lambda 内部。
3. texture state 更像“layout 切换助手”，不是完整 resource state/barrier 系统。
4. `prepare + executeCompiled` 与 `execute` 的 finalize 语义不一致。
5. per-pass plan 未真正编译成执行友好的局部结构，执行期仍要扫描全局 state plan。
6. raster/compute/copy pass 没有足够清晰的类型边界。
7. subresource、alias、transient physical reuse 仍停留在浅层支持或未完成状态。

因此当前准确结论应为：

> RenderGraph 已经可用，但还不能认为“完善”。
> 它更像是一个已经落地到生产路径的第一版 frame-graph / pass-scheduler，而不是成熟、强约束、单一事实源的渲染图系统。

## 3. 本计划的目标

把当前 RenderGraph 收敛为如下单向数据流：

```text
Pipeline/Stage frame intent
  -> declarative graph pass/resource description
  -> compile:
       pass order
       per-pass barrier plan
       attachment/rendering plan
       imported finalization plan
       transient lifetime metadata
  -> resolve typed pass parameters
  -> execute compiled plan once
  -> export final outputs
```

最终要求：

1. graph declaration 成为 pass 依赖、attachment 关系、resource usage 和最终导出结果的单一事实源。
2. execute callback 只消费编译后的 pass parameters，不再自己暗藏 attachment/rendering 真相。
3. `executeCompiled()` 与 `execute()` 拥有一致的资源收尾语义。
4. RenderGraph compiler 产物以 per-pass plan 为主，而不是执行时反复扫描全局向量。
5. raster / compute / copy pass 具备显式类型边界，减少当前 API 的“半声明、半命令式”混合写法。
6. persistent / imported / transient resource 的身份、lifetime、replacement、finalize contract 可被直接读懂。
7. 后续做 aliasing、subresource 精细化、async compute 时，不需要再重写主模型。

## 4. 非目标

本计划这轮不要求：

- 重写所有现有 pipeline/stage 文件结构。
- 一次性迁移 Forward、Deferred、Shadow、Presentation 到统一超抽象 orchestrator。
- 首轮就完成 texture aliasing、async compute、多 queue 调度。
- 删除所有 `beginRasterRendering()/endRendering()` 风格接口。
- 首轮引入自动 pass merge、render-pass fusion 或全自动 descriptor layout 生成。
- 首轮清理 OpenGL 兼容路径。

如果某项工作不能提升以下三者之一，就不属于本计划有效进度：

- graph declaration 的清晰度
- compile / execute 语义闭环
- resource lifetime / state contract 的一致性

## 4.1 前置依赖与边界

本计划不以 Scene、AssetManager、ResourceResolveSystem 或专用 render thread 重构为前置条件。
这些系统会向 RenderGraph 提供输入或消费输出，但不是当前 graph 双重真相和执行语义不闭环的根因。

真正需要先收口的依赖仅限于 RenderGraph 及其紧邻的 runtime seam：

### 必须先收口

- `prepare()`、`executeCompiled()`、`execute()` 的资源收尾语义必须统一。
- Imported / Persistent / Transient 的 identity、owner、replacement、final-state contract 必须明确。
- compiled graph 必须能承载 per-pass barrier、attachment/rendering、imported finalize 计划。
- pass kind 必须成为编译和验证的一等信息，而不能继续靠 usage 组合推断；
  但只有在当前已有真实 consumer 证明需求后才落地。

### 可以延后

- transient physical aliasing 的具体 allocator（由主计划 FG-106~FG-110 负责）
- async compute / 多 queue 调度
- scene-wide resource orchestration
- AssetManager / ResourceResolveSystem 的整体重构
- dedicated render thread
- OpenGL 路径清理

### 依赖原则

如果某项工作要求修改 Scene 或资源系统，只能作为适配层或验证项进入本计划；
不能以“先把上层全重构完”作为 RenderGraph 清晰化的前置条件。
否则会把 graph contract 问题扩散成跨层重构，并重新产生隐式旁路。

## 5. 当前代码事实

### 5.1 已具备能力

- handle-based resource model
  - `RGTextureHandle / RGBufferHandle / RGPassHandle`
- graph builder
  - `create/import texture/buffer`
  - `addPass`
  - `read/write/useColorAttachment/useDepthAttachment/transfer*`
- compile
  - dependency edge
  - invalid resource / invalid usage
  - read-before-write
  - cycle detection
  - topological order
- runtime registry
  - imported / transient / persistent resource sync
  - retainedResources 生命周期延长
- runtime executor
  - pass order 执行
  - texture layout transition
  - buffer barrier
  - imported resource finalize（但仅在 `execute()` 路径完整）

### 5.2 明确缺口

#### A. 双重真相

现在 graph setup 只声明“读写哪些资源”，但 execute lambda 仍自己决定：

- rendering begin/end
- attachment clear/load/store/finalLayout
- resolve attachment
- 实际需要哪些 resolved texture/buffer

这意味着：

- setup 不足以完整表达 pass 行为
- execute 不是纯消费 compiled plan，而是继续携带隐式编排语义

#### B. compile 产物过粗

当前 compile 结果里虽然有：

- `textureStates`
- `bufferStates`
- `order`

但执行时仍按 pass 扫整份 state plan，缺少：

- per-pass texture barrier list
- per-pass buffer barrier list
- per-pass attachment/rendering desc
- per-pass imported retain/finalize plan

#### C. texture state 语义偏弱

目前 executor 对 texture 主要调用：

- `transitionImageLayoutAuto(...)`

这更像 layout-oriented helper，不是完整的 image state machine。
与 buffer 路径相比，texture 的 stage/access/hazard 语义还不够显式。

#### D. 执行入口语义不一致

当前：

- `execute()` 会调用 imported finalization
- `prepare() + executeCompiled()` 不会自动 finalize imported final states

但 Deferred 主路径使用的正是 `prepare() + executeCompiled()`。
这说明 graph 的 compile/execute contract 还未完全闭环。

#### E. pass 类型不清晰

当前 `RGPass` 基本只是一组 usage + execute lambda。
没有足够清晰地区分：

- Raster pass
- Compute pass
- Copy/Transfer pass

导致 compile 层和 execute 层都只能依赖 usage 推断，代码阅读负担较大。

#### F. 更高阶 lifetime/alias 仍未成为主模型

虽然已有 imported/persistent/transient lifetime 概念，但仍缺：

- 更明确的 persistent identity contract
- compile 后 transient lifetime interval
- physical slot / alias plan
- subresource 粒度依赖模型

## 6. 目标架构

### 6.1 Graph 分层

建议把 RenderGraph 明确拆成三层：

```text
Layer 1: Declaration
  Pass type / attachments / reads / writes / exports

Layer 2: Compile
  order
  per-pass barrier plan
  per-pass rendering plan
  imported finalize plan
       transient lifetime metadata

Layer 3: Execute
  resolve handles
  replay compiled plan
  run pass callback with typed parameters only
```

要求：

- Layer 1 不直接录命令。
- Layer 2 不接触 backend objects。
- Layer 3 不重新推导 graph 语义。

### 6.2 显式 pass 类型

把 `RGPass` 从“通用 usage 容器”收敛为显式 kind：

```cpp
enum class ERGPassKind : uint8_t
{
    Raster,
    Compute,
    Copy,
};
```

并为不同 pass kind 提供更清晰的 builder：

- RasterPassBuilder
  - color/depth attachment
  - load/store/final layout
  - sampled/storage reads
- ComputePassBuilder
  - storage / sampled / indirect / dispatch resources
- CopyPassBuilder
  - transfer src/dst

首轮不要求完全拆类，也可以先保留 `RGPassBuilder`，但必须把 kind 提升为一等公民。

### 6.3 CompiledGraph 以 per-pass plan 为核心

目标形态：

```cpp
struct RGCompiledPassPlan
{
    RGPassHandle pass;
    std::vector<RGTextureStatePlan> textureBarriers;
    std::vector<RGBufferStatePlan>  bufferBarriers;
    std::optional<RasterRenderingPlan> rasterPlan;
};
```

`RGCompiledGraph` 的核心不再只是全局 order + 全局 state lists，而是：

- ordered pass plans
- global issues
- global exported outputs
- imported finalization plan

首轮只要求 Raster rendering plan 和现有 transfer consumer 的声明闭环。
当前代码没有独立的 compute dispatch declaration consumer，因此不要提前引入
`ComputeDispatchPlan` / `CopyPlan` 公共抽象；待真实 consumer 出现后再扩展。

### 6.4 Imported resource contract 统一

统一成：

```text
prepare(graph, compiled)
  -> compile
  -> registry sync
  -> compile imported finalization plan

executeCompiled(graph, compiled, cmdBuf)
  -> replay pass plans
  -> replay imported finalization plan
```

要求：

- `executeCompiled()` 本身就是完整执行闭环。
- `execute()` 只是 convenience wrapper，不应拥有额外语义。

### 6.5 Persistent / Imported / Transient 语义清晰化

明确三类资源：

- Imported
  - 外部 owner
  - graph 只声明访问和 final contract
- Persistent
  - registry 跨帧拥有
  - 需稳定 identity / replacement rule
- Transient
  - 当前图逻辑拥有
  - compile 后有 lifetime，后续可做 physical alias

要求：

- Persistent resource 的 identity 不再隐式依赖 handle 生成顺序。
- 需要时引入 stable key，而不是继续靠“同帧创建顺序稳定”维持复用。

## 7. 分阶段计划

### Gate 0: 运行边界冻结

目标：

- 在修改 graph API 前固定当前提交边界、线程假设和主路径入口。
- 明确 graph 只负责 command recording 期间的 pass/resource 编排；acquire、submit、present 仍由 `RenderRuntime` 管理。

交付：

- 主路径 inventory：
  - Deferred / Forward 的 graph executor 调用方式
  - Presentation graph 与 world graph 的边界
  - command buffer acquire / record / submit / present 顺序
  - imported resource 的 owner 和安全生命周期
- 明确“不需要先改 Scene/AssetManager/ResourceResolveSystem/render thread”的记录。

验收：

- 能从 `RenderRuntime` 追踪一帧的 command buffer 生命周期。
- 任何 graph API 改动都有明确的录制线程和提交边界。

### Phase 0: 评估与冻结当前契约

目标：

- 明确哪些语义已经被主路径依赖，哪些是过渡实现。
- 冻结当前 compile / execute / registry 行为，避免后续收敛时反复返工。

交付：

- inventory 文档：
  - pass kind 现状
  - imported finalize 现状
  - registry replacement / reuse 现状
  - setup / execute 双重真相清单

### Gate 1: 执行闭环

这是第一条硬依赖。Gate 1 未完成前，不引入新的高层 builder DSL。

### Phase 1: 统一执行闭环

目标：

- 修正 `executeCompiled()` 与 `execute()` 语义不一致。

交付：

- imported texture finalization plan 编译并执行
- imported buffer finalization plan 编译并执行
- 所有现有 `RenderGraphExecutor` 调用点共享同一闭环语义；
  不把 Forward 主 surface 迁移误算作本阶段完成条件

验收：

- 所有主路径使用 `prepare + executeCompiled` 时，与 `execute` 等价
- imported 资源 final state 不再依赖调用者“记得走哪条路径”

### Gate 2: 编译产物闭环

Gate 2 未完成前，不推进 transient aliasing 或 async compute。

### Phase 2: 编译 per-pass plan

目标：

- 从全局 state vector 扫描，收敛为 per-pass compiled plan。

交付：

- `RGCompiledPassPlan`
- executor 改为顺序消费 plan
- 旧全局扫描逻辑删除

验收：

- 执行期不再遍历“所有 textureStates / bufferStates 再筛 pass”
- debugDump 能输出每个 pass 的本地 barrier/rendering plan

### Phase 3: 提升 pass declaration 的真相地位

目标：

- attachment/rendering 语义尽量从 declaration 层生成，而不是散在 execute lambda 中。

交付：

- raster pass 的 attachment desc 收口
- render area / layer count / load/store/finalLayout 的 graph-side plan
- execute callback 改为消费 typed pass parameters

验收：

- 看 declaration 基本能知道 pass 的 attachment 行为
- execute 里不再重复一套 attachment 真相

### Phase 4: 显式 pass kind

目标：

- 让 raster / compute / copy 在 API 和 compiled plan 中具备清晰边界。

交付：

- `ERGPassKind`
- builder API / validation / debugDump 相应升级

验收：

- 不再需要靠 usage 组合去猜“这个 pass 本质是什么”

### Gate 3: 可扩展资源模型

Gate 3 在本子计划中只定义 graph core 所需的 metadata seam。
实际 transient buffer lifetime、slot allocation、pool hit 和 reuse ratio 由主计划
FG-106~FG-110 负责，并且仍是完整 RenderGraph 迁移的硬门禁。

### Phase 5: stable identity 与 transient lifetime 收口

目标：

- 为后续 aliasing/复用准备稳定主模型。

交付：

- persistent resource stable key 的 graph API 适配（具体实现由主计划 FG-101/102 负责）
- transient lifetime metadata 与主计划 FG-106 的接口对齐
- 为 FG-107~FG-109 提供 compiled-plan seam，不重复实现 physical slot allocator

验收：

- persistent replacement contract 可直接描述
- transient aliasing 可以在不重写主模型的情况下继续推进

## 8. 验证计划

每个代码阶段至少覆盖：

- `xmake b ya-engine`
- `xmake b ya-runtime`
- `xmake b ya-editor`

日常测试优先使用：

- `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
- `make b t=HelloMaterial` 仅在仓库 alias 可用时作为 localized smoke

渲染冒烟建议：

- Deferred 基线
- Forward 基线
- Bloom on/off
- SSAO on/off
- Shadow on/off
- pipeline switch
- editor viewport resize
- shutdown / scene switch

关注点：

- imported final layout 是否一致
- postprocess / overlay / shadow 是否仍保持原有顺序语义
- registry replacement 是否安全
- RenderDoc / validation 是否出现新的 barrier/layout 错误

## 9. 风险

### 风险 A：过早抽象 builder

如果在没把 compile/execution contract 收敛前就设计一套“很漂亮”的高层 pass DSL，
大概率只是把当前双重真相包装得更深。

策略：

- 先统一闭环，再做 API 美化。

### 风险 B：图语义与现有 stage 语义错位

当前很多 stage 已经带有自身的 frame/view/attachment 假设。
如果不先做 inventory，就直接把 attachment 真相搬到 graph，容易出现一层新抽象叠一层旧抽象。

策略：

- 先盘清楚 setup/execute 双真相清单，再逐类 pass 收口。

### 风险 C：把“清晰化”误做成“功能扩张”

比如同时把 async compute、aliasing、presentation、descriptor 自动生成全部并进来，
会让计划失焦。

策略：

- 以“让 declaration / compile / execute 关系更清楚”为唯一主线。

## 10. 推荐后续工件

建议在本目录继续补：

- `assessment.md`
  - 当前 pass/type/resource 真相清单
- `todo.md`
  - 分阶段任务编号与依赖
- `progress.md`
  - 每轮实际推进记录
- `compiled-plan-sketch.md`
  - `RGCompiledPassPlan` 草案

新增：

- `dependency-gates.md`
  - 记录哪些跨层依赖被明确排除，哪些 runtime seam 已闭环

## 11. 退出条件

满足以下条件后，才可认为这条重构线完成：

1. `prepare + executeCompiled` 与 `execute` 完全同语义。
2. executor 以 per-pass compiled plan 为核心执行单位。
3. declaration 足以表达 pass attachment / access / export 语义。
4. execute callback 不再暗藏第二套 attachment/rendering 真相。
5. pass kind 清晰可读，不再靠 usage 组合猜 intent。
6. persistent / imported / transient contract 可被直接描述，并能支持下一阶段 aliasing/优化。

并且：

7. Scene、AssetManager、ResourceResolveSystem 和 render thread 没有被无必要地耦合进本轮主路径。
8. graph 的 command recording 边界与 `RenderRuntime` 的 acquire/submit/present 边界保持清晰。
9. 本子计划没有重复实现主计划 FG-101~FG-111 的资源复用和 upload arena。
