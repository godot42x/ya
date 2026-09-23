# Render View 资源所有权与管线编排收口计划

> 建立日期：2026-09-23
> 状态：已完成；五个 checkpoint 全部落地并通过验证（2026-09-23 收尾轮，见 `progress.md`）
> 关联计划：`../render-view-family/plan.md`、`../render-application-boundary/plan.md`

## 1. 当前目标与边界

目标是保留多 Scene、多 View、Forward / Deferred、RenderGraph、编辑器 picking 与调试查看能力，
同时把资源创建、复用、发布、查询、保活和回收收敛成一套符合直觉的模型。

完成后，读者只需要记住五句话：

1. View 是长期存在的逻辑对象，Frame 只决定这一帧是否渲染它。
2. View 的 GPU target 只由 `ViewTargetStore` 持有。
3. Pipeline 只描述渲染策略并录制 graph，不保存上一帧 View 资源。
4. RenderGraph 只拥有本次 graph 的逻辑句柄和可复用临时 scratch。
5. `RenderSubmission` 保证本次提交引用的资源活到 GPU 完成。

本计划不处理以下事项：

- 不改变 Scene / ECS 的产品层所有权。
- 不合并 Forward 与 Deferred 的 pass topology。
- 不引入新的 RHI backend 抽象。
- 不把窗口尺寸重新作为 View 渲染分辨率来源。
- 不在这次收口中引入 TAA、曝光等尚不存在的 history 功能。

## 2. 当前复杂度的真实来源

问题不在于“资源按 View 划分”。View color、depth、GBuffer、entity-id 和后处理输出本来就属于
View。复杂度来自同一份资源同时被多个系统当成自己的状态：

| 当前位置 | 当前职责 | 问题 |
| --- | --- | --- |
| `RenderGraphResourceRegistry::_persistentTextures` | 按字符串 key 创建并复用 View texture | 没有 View 生命周期；persistent entry 只在 executor `clear()` 时整体释放 |
| Pipeline 的 `ViewResourceTable` | 保存上一帧 View 附件，供 debug / picking 查询 | 与 per-flight output 重复；需要额外 reconcile / GC |
| `RenderDeviceState::_viewOutputs` | 保存本 flight 的 View 输出 | 与 pipeline 表持有同一批 `shared_ptr` |
| `RenderSubmission` / command buffer | 保活本次提交引用的资源 | 调用方需要手工遍历并 retain |
| debug catalog / present / picking | 分别从 pipeline 和 device 查询 | 没有统一事实源 |

当前 `RenderGraphResourceRegistry::pruneUnusedResources()` 只删除 graph handle 到 entry 的绑定，
不会删除 `_persistentTextures` 中按 View key 保存的资源。一个 View 消失以后，pipeline 表虽然可以
释放自己的 `shared_ptr`，registry 仍然持有 persistent texture，直到整个 executor `clear()`。这使
View GC 与 graph registry GC 实际上不是同一件事。

另一个根因是 View 的“本帧未声明”同时承担两种语义：

- View 仍存在，只是这一帧不渲染，例如隐藏 tab、暂停 preview。
- View 已销毁，应当删除发布状态和 GPU target。

用 `SceneRenderPlan` 的缺席推断销毁，会迫使 pipeline 每帧做 `reconcilePublishedViews()`，也让
缓存策略、隐藏 View 和真正 GC 混在一起。

## 3. 目标对象模型

```text
ISceneViewProducer / product View owner
  ├─ create / destroy stable SceneViewKey
  └─ each frame: optionally submit SceneViewDesc

RuntimeRenderContext
  ├─ collect and seal SceneRenderPlan
  ├─ ask active Pipeline for ViewTargetRequest[]
  ├─ ViewTargetStore::prepare(requests)       <- command buffer 打开前
  ├─ begin RenderSubmission
  ├─ Pipeline::recordFamily(..., ViewTargetLease[])
  ├─ ViewTargetStore::publish(successful outputs)
  └─ seal / submit / present

ViewTargetStore
  ├─ live View entries
  ├─ View target allocation and exact-desc reuse
  ├─ per-flight RenderViewOutput table (`RenderViewOutputTable`，由 store 收编)
  ├─ debug / picking / present query
  └─ explicit destroy and retirement

RenderGraphExecutor
  ├─ imported resource bindings
  ├─ transient texture / buffer pool
  └─ graph compile and execute

RenderSubmission
  ├─ upload arena
  ├─ descriptor allocation
  ├─ SceneFamilyResources
  └─ resource keepalive until queue completion
```

### 3.1 生命周期分层

| 生命周期 | 内容 | 唯一 owner |
| --- | --- | --- |
| Device | graphics pipelines、pipeline layouts、DSL、samplers、fullscreen mesh | Forward / Deferred pipeline 和 stage recipe |
| View | color、depth、GBuffer、entity-id、display output、可查看的 SSAO / bloom 输出、未来 history | `ViewTargetStore` |
| Scene family / submission | skinning packet、family descriptor、共享 Scene GPU packet | `RenderSubmission::SceneFamilyResources` |
| View / submission | UBO slices、descriptor sets、pass bindings | `RenderSubmission` + `RenderViewBindingTable` |
| Graph | logical handle、barrier、pass dependency、scratch texture / buffer | `RenderGraphExecutor` |
| Asset | mesh、material、texture、environment lighting 派生资源 | Resource system / shared resource provider |

一个资源只能有一个长期 owner。其他位置可以持有本次操作需要的 lease 或 `shared_ptr`，但不建立
第二套索引、GC 和“上一帧当前值”。

## 4. View 的存在与帧声明分开

`SceneViewKey` 已经是稳定且 owner-scoped 的身份，应继续作为唯一 View identity。View producer 的
注册生命周期同时成为 View 生命周期来源：

```cpp
class ViewTargetStore
{
public:
    void registerView(SceneViewKey key);
    void unregisterView(SceneViewKey key);

    void prepare(std::span<const ViewTargetRequest> requests);
    const ViewTargetLease* lease(SceneViewId viewId) const;

    void beginPublication(uint32_t flightIndex, uint64_t frameToken);
    bool publish(uint32_t flightIndex, RenderViewOutput view);
    const RenderViewOutput* findPublication(uint32_t flightIndex, SceneViewId viewId) const;
};
```

规则如下：

- `registerView` 只登记逻辑身份，不立即分配 GPU target。
- 第一次出现 `ViewTargetRequest` 时延迟创建 target。
- 某帧没有 request，表示这一帧没有新输出；View 仍存在，已有 allocation 可继续驻留。
- `unregisterView` 立即取消对外 publication，并删除 store 对 allocation 的长期引用。
- allocation 若仍被 flight publication 或 submission 引用，会自然活到对应 fence 完成。
- View producer 被移除时必须 unregister 它拥有的全部 local View。

这样 GC 由明确生命周期触发，不再由帧计划缺席推断。

如果暂时不方便一次引入显式注册，迁移阶段可以保留 `reconcileDeclaredViews()`，但它只能作为兼容
入口，最终必须由 producer 生命周期替代。

## 5. View target 的统一表示

使用稳定角色而不是 pipeline 私有字符串作为 attachment 身份：

```cpp
enum class EViewAttachment : uint8_t
{
    SceneColor,
    SceneDepth,
    DisplayColor,
    EntityId,

    GBuffer0,
    GBuffer1,
    GBuffer2,
    GBuffer3,
    SSAO,

    BloomExtract,
    BloomBlur,
    BloomComposite,

    Count,
};

struct ViewAttachmentDesc
{
    EViewAttachment role{};
    EFormat::T format = EFormat::Undefined;
    EImageUsage usage{};
    ESampleCount::T samples = ESampleCount::Sample_1;
};

struct ViewTargetRequest
{
    SceneViewId viewId = 0;
    ERenderPipelineKind pipeline{};
    Extent2D extent{};
    std::vector<ViewAttachmentDesc> attachments;
};
```

`ViewTargetStore` 保存：

```cpp
struct ViewTargetAllocation
{
    ViewTargetRequest desc{};
    uint64_t generation = 0;
    std::array<std::shared_ptr<RenderTexture>,
               static_cast<size_t>(EViewAttachment::Count)> attachments{};
};
```

`ViewTargetRequest` 是 allocation key。相同描述直接复用；extent、format、sample 或 usage 变化时在
安全点创建新 generation。Pipeline 不自行比较上一帧格式，也不调用 `refresh...` 来修复某个已经
录制完成的 View。PSO format variant 在创建新 allocation 前准备好。

角色表是有意采用的有限集合，不使用 `unordered_map<string, texture>`。有限 enum 使编译器和测试能
发现遗漏，也让 debug catalog 可以按角色直接生成条目。Forward 不使用 GBuffer role 时对应位置为空。

## 6. Pipeline 的职责

Pipeline 保留：

- pipeline / layout / shader variant。
- strategy settings。
- Forward / Deferred graph topology。
- 把 immutable View 输入和 `ViewTargetLease` 转成 graph pass。

Pipeline 删除：

- `_publishedViews`、`_viewResources`。
- `reconcilePublishedViews()`。
- `buildDebugViews()`。
- `getViewDepthImageShared()` / `getEntityIdImageShared()` 这类 View 资源查询。
- “上一帧哪个 View 的资源”状态。
- 仅为 pipeline 资源表服务的 `ViewResourceKey` / `ViewResourceTable`。

Pipeline 接口收敛为两段：

```cpp
struct IRenderPipeline
{
    virtual void appendTargetRequests(
        const SceneRenderPlan& plan,
        std::vector<ViewTargetRequest>& out) const = 0;

    virtual ViewFamilyRenderResult recordFamily(
        const ViewFamilyRecordContext& ctx,
        const ViewTargetStore& targets) = 0;
};
```

第一段只计算配置，不创建 GPU 资源；第二段只消费已经准备好的 target。由此满足“录制中途不重建
GPU 资源”的规则。

Stage 的 `prepare()` 不能再表示“当前 View”。如果它只做 `pipeline->beginFrame()`，应提升成 pipeline
每 submission 一次的 `beginSubmission()`；如果它生成 View payload，应返回值或写入 View-owned packet。
当前 Deferred 在 family graph build 完成后用 `liveBranches.back().stageCtx` 调用 SSAO / Light / Overlay
prepare，虽然现有实现多数只执行 `beginFrame()`，这个形态仍会误导读者，应消除。

## 7. RenderGraph 的职责

View target 由 store 创建，graph 以 imported texture 使用：

```cpp
auto sceneColor = graph.importTexture(
    makeImportedTextureDesc(targets.require(EViewAttachment::SceneColor)));
```

RenderGraph 继续管理：

- transient scratch texture / buffer 的池化和 alias。
- imported resource 的 layout / barrier。
- graph handle 到执行期资源的解析。
- pass dependency、编译与执行。

RenderGraph 不再管理：

- 按 ViewId 命名的 persistent texture。
- View 是否存在。
- View GC。
- debug / picking / present 查询。

当前代码中 `createViewPersistentTexture()` 的调用逐步改成 store target import。完成后删除
`ViewPersistentResourceKey.h`。仓库没有其他 persistent graph resource 使用者时，再删除
`ERGResourceLifetime::Persistent`、persistent key 和 registry persistent map；不要保留无人使用的通用机制。

## 8. 发布模型

allocation 与 publication 是两个概念：

- allocation 表示 View 拥有哪些 GPU target。
- publication 表示某一 flight 的 graph 成功写出了哪些 target，以及哪个 attachment 是最终 display。

实现沿用既有记录类型 `RenderViewOutput`（本计划草案里叫 `PublishedView`，落地时未改名）：
它按 `EViewAttachment` 角色持有 color / depth / display / entityId / ssao / bloom* /
gBufferColors，外加 `targets`（本 allocation 的 `shared_ptr`）与 `allocationGeneration`；
"最终 display 是哪个 attachment" 由 `displayImage()`（有 `display` 用之，否则回落 `color`）
表达，不再单独存一个 `displayRole` 字段。

```cpp
struct RenderViewOutput
{
    RenderViewOutputDesc                          desc{};   // viewId / extent / formats
    std::shared_ptr<RenderTexture>                color, depth, display, entityId;
    std::shared_ptr<RenderTexture>                bloomExtract, bloomBlur, bloomComposite, ssao;
    std::array<std::shared_ptr<RenderTexture>, 4> gBufferColors{};
    std::shared_ptr<ViewTargetAllocation>         targets;
    uint64_t                                      allocationGeneration = 0;
};
```

发布只有一条规则：graph 成功执行后才能 commit。

```text
prepare allocation
  -> build graph
  -> compile
  -> execute
  -> success: ViewTargetStore::publish
  -> failure: 本 flight 没有该 View publication
```

不允许 Deferred 在 execute 前发布，而 Forward 在 execute 后发布。失败时也不允许把旧 View 输出伪装
成本帧输出。调用方需要 fallback 时，由调用方根据本 tick 输入决定。

发布记录收编进 `RenderViewOutputTable`，由 `ViewTargetStore` 持有
（`beginPublication` / `publishView` / `findPublication`），取代 Deferred / Forward 各自
平行的 View 资源表（`ViewResourceTable` 已删除）。

## 9. 查询路径

所有 View 输出查询都进入 `ViewTargetStore`：

```cpp
const RenderViewOutput* published = targets.findPublication(flightIndex, viewId);
```

随后：

- present / view compose 读取 `displayRole`。
- picking 读取 `EntityId`。
- depth inspector 读取 `SceneDepth`。
- deferred debug catalog 读取 `GBuffer0..3`、`SSAO`。
- bloom inspector 读取 `BloomExtract / Blur / Composite`。

Shadow map 不属于 View target。它由 shadow strategy / pipeline 配置持有，继续通过独立的 shadow
diagnostic provider 发布，避免为了统一查询而把不同生命周期的资源硬塞进 View。

Render target catalog 的“查看”和“配置”必须分开：

- catalog entry 描述某个已发布 View allocation。
- pipeline target format 设置描述全局 strategy 配置，不携带 `viewId`。
- 如果未来真的支持 per-View format，配置必须进入 `SceneViewDesc` / `ViewTargetRequest`，不能让 UI
  在 per-View catalog 行上调用一个实际修改全局 pipeline spec 的命令。

## 10. 保活与安全释放

`ViewTargetLease` 持有本次录制会引用的整个 allocation。`recordFamily()` 开始时把 lease 交给
`RenderSubmission::retain()` 一次，不再在帧末遍历每个 `RenderViewOutput` 分别保活 color、depth、
entity-id。

建议让 allocation 本身实现 retained resource bundle：

```cpp
struct ViewTargetLease
{
    std::shared_ptr<const ViewTargetAllocation> allocation;
};
```

只要 submission 持有 allocation，allocation 内全部 texture 都活着。替换或 unregister 时：

1. store 删除旧 allocation 的长期引用。
2. 已录制 submission 和 per-flight publication 仍持有旧 generation。
3. flight fence 完成并复用后，最后一个引用释放。
4. RHI 资源进入已有的安全销毁路径。

这样 store 不需要自己维护 fence、帧龄或 deferred deletion queue。

## 11. 缓存、GC 与抖动策略

### 11.1 默认只保留三种缓存

1. `ViewTargetStore`：同一 live View、相同 request 的 exact reuse。
2. Pipeline / RHI：graphics pipeline variant cache。
3. RenderGraph executor：transient scratch pool。

不增加通用 LRU、TTL 或跨 View attachment cache。没有 profile 证据时，缓存数量越少越容易预测。

### 11.2 GC

- 正确性 GC：`unregisterView()`。立即停止发布和查询。
- GPU 安全释放：由 submission / flight 持有旧 generation，等待 fence。
- 内存压力回收：以后可以增加显式 `releaseAllocation(viewId)`，只回收仍注册但暂停的 View target；
  下一次 request 再懒创建。
- 不使用“连续 N 帧没看到就猜测死亡”的 GC。

### 11.3 resize 抖动

Renderer 只接受 owner 已经决定好的 render extent，并按 exact extent 分配。resize debounce 属于知道交互
语义的 owner：

- Runtime render-resolution 设置变化：立即提交新 extent。
- Editor panel 拖动：panel owner 在拖动期间保留最后一次 committed extent，GUI 将旧输出拉伸显示；尺寸
  稳定后再提交一次新 extent。
- Window resize 不修改 View render extent，只影响 presentation stretch。

不要在 `ViewTargetStore` 内引入隐藏的 bucket、grow-only 或 shrink delay。它们会让 requested extent、
camera aspect、实际 attachment extent 再次出现多个事实源。

## 12. Forward / Deferred 排布

保留两个顶层 family renderer：

```text
ForwardViewFamilyRenderer
  shadow -> skybox -> opaque -> transparent -> entity id -> postprocess

DeferredViewFamilyRenderer
  shadow -> gbuffer -> ssao -> light -> forward opaque -> skybox
         -> bloom -> transparent -> entity id -> postprocess
```

它们共享的只有机制：

- `ViewTargetStore`。
- `RenderSubmission`。
- `RenderGraphExecutor`。
- postprocess / shadow 等可复用 stage recipe。

不创建 `BaseRenderPipeline` 来强行统一 topology。重复的 family 循环可以抽取窄 helper，但 pass 顺序、
target request 和输出角色仍由具体 pipeline 清楚写出。

每个 family 可以继续构建一个 graph。多个 View 是否用 predecessor 串行，属于 graph scheduling 策略；
它不应影响资源所有权。未来允许并行分支时，View target 已经彼此独立，不需要再调整 owner。

## 13. 必须删除的旧路径

最终状态不保留以下兼容层：

- `ViewResourceKey.h` / `ViewResourceTable`。
- `DeferredPipelineDebugViews`。
- `DeferredGBufferResources`、`DeferredViewResources` 中仅用于 pipeline publication 的包装。
- `ForwardViewResources` 中仅用于 pipeline publication 的包装。
- `IRenderPipeline::reconcilePublishedViews`。
- `IRenderPipelineDebugOutputs` 中 View attachment 查询方法。
- `DeferredRenderPipeline::buildDebugViews`。
- `RenderDeviceState::getDeferredPipelineDebugViews`。
- `RenderDeviceState::retainPublishedViewOutputs` 的逐 attachment 遍历。
- `ViewPersistentResourceKey.h` 和按 View 字符串 key 的 persistent graph allocation。
- `PostProcessingStage::_preparedOutputImage` 及类似“最后一次 prepare 的输出”状态，如果没有真实调用者。
- `_debugAlbedoRGBView`、`_debugSpecularAlphaView`、`_cachedAlbedoSpecImageViewHandle` 等无生产者或
  无消费者的管线级残留。

删除时遵守项目规则：找不到生产者或消费者的数据通道直接删除，不为它补一个使用者。

## 14. 迁移检查点

每个 checkpoint 对应一个完整可验收目标，不按文件数量拆分。plan 文件的进度更新与对应实现放在同一
提交中。

### Checkpoint A：统一发布事实源

状态：已完成（2026-09-23）。

完成结果：

- `RenderViewOutput` 已覆盖 Deferred GBuffer、SSAO、Bloom、depth、entity-id 与 display 输出。
- Forward / Deferred pipeline 不再保存 per-View publication 表，也不再提供 View attachment 查询入口。
- viewport snapshot、present、picking 与 debug catalog 统一按 flight / ViewId 查询 `RenderViewOutputTable`。
- Deferred 只在 graph execute 成功后把 family result 交给 device publication；失败时本 flight 不发布该 View。
- render-target catalog 只描述 pipeline 的全局格式配置，不再伪装成 per-View 已发布资源目录。

目标：所有 present、picking、depth、bloom 和 Deferred debug 查询都从一张 per-flight publication 表读取。

实施：

- 扩展 `RenderViewOutput`，先纳入 Deferred GBuffer attachment。
- Deferred 改为 graph execute 成功后返回完整输出。
- debug catalog 停止调用 `getDeferredPipelineDebugViews()`。
- 删除 pipeline 级 `_publishedViews` / `_viewResources` 和 reconcile。

验收：

- 多 View 的输出、GBuffer、entity-id 不串 View。
- graph execute 失败后本 flight 查不到该 View。
- View 查询没有 pipeline downcast 或 concrete pipeline 入口。

### Checkpoint B：引入 ViewTargetStore，收回 allocation owner

状态：已完成（2026-09-23）。

完成结果：

- `ViewTargetStore` 按 `ViewTargetRequest` 在 pre-record safe point 创建、精确复用或替换 allocation。
- Forward / Deferred 在录制时只消费 `ViewTargetLease`，所有 View attachment 通过 imported texture 进入 graph。
- Bloom 的交替 scratch 保持 transient；可发布的 extract / blur / composite target 归 store。
- `RenderSubmission` 一次保活完整 allocation，删除逐 attachment retain。
- publication 携带 allocation generation；相同 request generation 不变，resize 只替换一次。

目标：View target 只由 store 创建和持有，pipeline 只消费 lease。

实施：

- 增加 `EViewAttachment`、`ViewTargetRequest`、`ViewTargetAllocation`、`ViewTargetLease`。
- safe point 根据 active pipeline 的 request 准备 allocation。
- Forward / Deferred attachment 改为 imported store target。
- submission retain 整个 lease。

验收：

- command recording 期间没有 target 创建、resize 或 replacement。
- 相同 request 连续帧不分配新 texture。
- resize / format change 只产生一个新 generation。

### Checkpoint C：删除 graph persistent View 资源

状态：已完成（2026-09-23）。

完成结果：

- RenderGraph 的 resource lifetime 收敛为 imported / transient，删除 persistent texture / buffer API、key 与编译期 identity 校验。
- `RenderGraphResourceRegistry` 删除 persistent map 与 replacement / omission 分支，只保留 imported binding、transient texture pool 和 transient buffer alias pool。
- 删除 `ViewPersistentResourceKey.h` 及其专用测试，View graph 名称测试直接依赖 `ViewGraphName.h`。
- export owner、imported keepalive / final state 与 transient pool 相关测试继续覆盖原有执行契约。

目标：RenderGraph registry 只管理 imported binding 与 transient pool。

实施：

- 删除全部 `createViewPersistentTexture()` 调用。
- 删除 `ViewPersistentResourceKey.h`。
- 仓库无其他使用者后删除 graph persistent texture / buffer 机制和 registry map。

验收：

- View 销毁后 registry 不再持有任何按 ViewId 命名的资源。
- transient pool hit / miss 行为保持可观测。

### Checkpoint D：显式 View 生命周期

状态：已完成（2026-09-23）。

完成结果：

- `ISceneViewProducer` 新增 `ownedViewLocalIds()`；runtime display root、editor authoring / preview 各自枚举自己拥有的固定 local View。
`App::addSceneViewProducer` / `removeSceneViewProducer` 在注册边界调用 `RenderDeviceState::registerSceneView` / `unregisterSceneView`，转交 `ViewTargetStore::registerView` / `unregisterView`。
- `ViewTargetStore` 的 publication 表收编 `RenderViewOutputTable`（`beginPublication` / `publishView` / `findPublication`），unregister 通过 `dropView` 立即清空所有 flight 的可见发布；present / picking / debug 的查询入口不变。
- `prepare()` 只为已注册 View 分配 target；某帧没有 request 的注册 View 保留 allocation。迁移期 "plan 缺席即建 / 即删" 语义移除。
- 已录制 submission 的 allocation keepalive 不受 unregister 影响，测试覆盖旧 generation 在 store 释放后仍可用。

目标：View GC 不再依赖 SceneRenderPlan 缺席。

实施：

- producer 注册 / 移除时同步 register / unregister View key。
- runtime display root、editor authoring、preview 各声明自己固定的 local View。
- 删除迁移期 `reconcileDeclaredViews()`。

验收：

- 暂停一帧不会销毁 View target。
- 移除 producer 会立即取消查询，并在 fence 安全后释放 target。
- owner / local key 冲突继续在 producer 注册时断言。

### Checkpoint E：收敛 stage 与诊断残留

状态：已完成（2026-09-23）。

完成结果：

- `IRenderPipeline` 新增 submission 级 `beginSubmission()`；Forward / Deferred 的 PSO warmup（含 SSAO、Light、Overlay、GBuffer stage 的 `beginFrame()`）每 submission 调用一次，删除 Deferred 用 `liveBranches.back().stageCtx` 调 SSAO / Light / Overlay prepare 的形态。GBufferStage 拆分后 per-View material flush 仍在 prepare()，PSO warmup 移入 beginFrame()。
- `ViewFamilyRenderResult` 携带本 family 的 `RGTopologyDescription`，只在 graph 执行成功时填充；device 按 flight 收集 `_frameGraphTopologies`，present 面板按整帧 family 列表汇总。删除 `IRenderPipeline::getLastFrameGraphTopology()` 与两个 pipeline 的 last-frame 缓存。
- 删除 `PostProcessingStage::_preparedOutputImage`、capture/clear prepared API 和 Bloom 的 `_extractImage` / `_blurPingImage` / `_blurPongImage` / `_compositeImage`（全部无真实消费者）；Deferred 删除 `_debugAlbedoRGBView` / `_debugSpecularAlphaView` / `_cachedAlbedoSpecImageViewHandle`（只 reset、从未赋值）。
- 删除 `ViewResourceKey.h` / `ViewResourceTable` 与其测试；删除已无引用的 `ForwardViewResources.h` / `DeferredViewResources.h`。

目标：pipeline / stage 中没有 current View 或 last View 资源状态。

实施：

- `beginFrame()` 提升为每 submission 一次。
- View payload 通过返回值、snapshot 或 View-owned binding 传递。
- topology diagnostics 按本 frame 的 family 列表发布，不再只保存最后一个 family。
- 删除无生产者 / 无消费者的 prepared/debug 字段。

验收：

- 同一 tick 调换 family / View 顺序，输出身份和资源不变。
- `getLastFrameGraphTopology()` 不再把最后 family 冒充整个 frame。

## 15. 测试与观测

需要保留少量但能验证生命周期的测试：

- 两个 View、不同 extent、同一 Scene：allocation 和 publication 独立。
- 两个 Scene、多个 family：不会覆盖输出，topology 按 family 保存。
- View 连续帧相同 request：allocation generation 不变。
- View resize：generation 增加一次，旧 generation 保活到 flight 完成。
- View 一帧不渲染：逻辑 View 和 allocation 不被误删，本 flight 没有新 publication。
- unregister View：查询立即为空，旧 submission 仍可安全完成。
- graph execute 失败：没有 publication commit。
- pipeline switch：旧 pipeline target layout 释放，新 layout 在安全点创建。

建议增加诊断计数：

```text
liveViews
residentViewAllocations
viewAllocationCreates
viewAllocationReuses
viewAllocationReplacements
publishedViewsPerFlight
transientTexturePoolHits / Misses
```

这些计数由实际 owner 提供，不通过扫描多个 registry 拼装。

## 16. 最终判据

架构收口完成时，应能对任意一张 View texture 直接回答：

- 谁创建：`ViewTargetStore`。
- 谁长期持有：`ViewTargetStore` 的 live View allocation。
- 谁在本帧使用：`RenderSubmission` 持有的 `ViewTargetLease`。
- graph 如何访问：imported texture handle。
- 谁对外发布：`ViewTargetStore` 的 per-flight `RenderViewOutput`。
- 谁查询：present、picking、debug 都通过同一个 `findPublication(flight, viewId)`。
- 什么时候替换：safe point 上 request 变化。
- 什么时候回收：View unregister，最后一个 flight / submission 引用结束。

任何资源如果需要跨越这套规则，应先证明它属于另一种生命周期，例如 device 级 shadow 配置、
Scene 级 environment lighting 或未来 View history，而不是继续给 View publication 增加例外。
