# RDG 上方的管线组合层收敛

> 建立 / 更新：2026-10-13
> 状态（2026-10-13 复审后）：**重新打开**。C1–C5 已落地的结构删除与行为证据保留（旧 orchestrator/passes/resources/ViewResources bag 退出、target 单源、SSAO/Bloom 关闭无资源、共享 Bloom/EntityId/Finalize、M04 与 blur last-call 修复）；但 §1 的成功定义 1–4 未达成，且 §8 第一条停止条件已触发（设置多源、录制期重建 shadow 资源）。M01/M02/M05/M21 降回 in_progress。下一 checkpoint：**D1**（§10）。
> 复审前的完成记录与 C 系列命名对应（C1 = "C1-foundation + 集成切片"）见 progress.md，不在此重复。
> **D1 已落地**（2026-10-13）：两步安全点进 `prepareFrameRecord`，录制路径无设置应用、无 shadow 资源重建；M24/M02 verified。**D3 已落地**：invocation tuple + fold（M26 verified，M01/M05 证据更新）。**D4 已落地**：typed 发布（GraphOutputs→RenderViewOutput 一处映射，publish 零 export 名字）+ family 原子失败（M27/M13/M10/M21 verified），并修复 C4a 丢失的 Forward depth 导出。下一 checkpoint：**D5**（feature 吸收 Stage，分批）。

## 1. 目标与成功定义

解决的不是 Bloom 算法，也不是 RDG barrier / dependency compiler，而是：一个纵向渲染 feature 被切成配置、target request、角色、allocation、import、bindings bag、pass 顺序、pass 体等横向层；管线既有集中 append helper，又有 stage 自带 graph 入口；没有可直接阅读的完整组合地图。

最终达到：

1. **管线地图一处可读**：Forward / Deferred 各有一个 typed C++ recipe，明确 feature、连接、策略、发布及必要的语义顺序。
2. **feature 契约局部完整**：本次启用决策、输入输出、资源要求、bindings 准备和 graph 贡献归同一个 feature 入口及其实现。
3. **同一份 resolved composition 驱动全链路**：安全点 target 准备、录制期 bindings、import / transient、graph build 和发布不再各自维护 feature 清单。
4. **减少 plumbing 而非搬文件**：不再因为普通 feature 增加而扩展 pipeline 级 BuildInputs / PassContext / FrameResourceSet 槽位。
5. **行为与生命周期不变**：现有渲染结果、颜色编码、picking、debug、View / flight 隔离、owner reuse 和 GPU 保活有前后证据。

扩展边界：新增普通功能允许改 feature、recipe 接线、必要的公开设置 / 输出 role / 产品 UI；不应被迫修改 executor、registry、集中 bindings bag 或无关 feature。新增 RHI 能力是另一种任务，不能套用这个承诺。

## 2. 开工阅读顺序

| 文件 | 用途 |
| --- | --- |
| `plan.md` | checkpoint、范围、停止条件 |
| `design.md` | 对象、API 草案、typed ports、单源资源、生命周期 |
| `migration.md` | 现有符号 / 文件迁入何处，逐阶段删除清单，双管线地图 |
| `validation.md` | 可运行命令、测试用例、截图和性能证据要求 |
| `todo.md` | 人可快速接续的当前任务 |
| `feature_matrix.json` | 稳定验收项、状态和证据；不能以改描述降低验收 |
| `progress.md` | 本轮事实、偏离、未完成和交接 |
| `session_checklist.md` | 每轮开工 / 收尾步骤 |

这些是本次重构参照，不冒充已实现的 API 或当前稳定架构规范。命名可在实际代码中微调；协议、owner、行为边界改变必须先记录原因并复审。

## 3. 已核对的起点

- `DeferredRenderPipeline::appendDeferredViewToGraph()` 计算 `bUseSSAO`，准备 SSAO CPU UBO，调用 FrameResourceSet、统一 bindings 分配函数，再组装 orchestrator inputs。
- `DeferredFrameGraphOrchestrator::BuildInputs` 和 `DeferredFrameGraphPassContext` 再传同一个 bool；`importFrameBuffers()` / `appendSSAO()` 各自判断。
- `SSAOStage` 已有 `appendGraphPass()`，但 AO request、UBO / DSL / upload、import 和 input DS 仍在外层。
- `DeferredFrameResourceSet::Binding/ViewResources` 混合共享 frame/light 与 SSAO、skybox、lighting、entity-id、overlay、sprite、post 的绑定；Forward 有对应 bag。
- Forward / Deferred 的 Bloom request、import、DSL getter 调用和 output 字符串收集重复；它是症状证据，不是本计划的主题。
- RDG 已有 import scope/key、依赖编译、状态转换、transient 池与保活。P2 历史测试结果不算本轮基线。
- `RuntimeRenderContext::record()` 当前先 `prepareFrameRecord(plan)`，再 begin command buffer，再 `recordViewFamilies(plan)`；准备结果未显式传入录制。
- 当前 Forward Transparent 与 Deferred Forward Opaque helper 的执行体没有实际 draw；不得在本线顺手补透明渲染，也不得直接删掉其 attachment load / depth / 顺序语义。

代码锚点及具体归属见 `migration.md`。行号易漂移，开工按符号搜索。

## 4. 范围和不变量

### 本线负责

- typed feature 协议、recipe、resolved / prepared 调用实例及组合诊断。
- Forward / Deferred world family graph 的现有贡献全部迁入同一种协议，包括 Shadow 接入、GBuffer / Lighting、Skybox、Sprite、Overlay、EntityId、颜色效果和 Finalize。
- View request / import / publication 与 feature 资源声明接通。
- feature-local bindings、共享 View / Scene GPU 输入与 device 级布局的职责拆分；随职责迁移删除混合 bag。
- prepare → record 间显式传递本次准备结果所需的最小接口变化。

### 必须保留

- 一 Scene family 一张顶层 world graph、一个 executor；feature 不执行子图。
- View target 唯一 owner 为 `ViewTargetStore`，安全点 exact reuse / replacement、整体 lease、成功后发布。
- Shadow map 归 shadow strategy，不变成 View attachment；Scene skinning 归现有 Scene cache。
- descriptor / upload allocation 使用现有 `RenderSubmission`；池与 fence 语义不重写。
- graph callback 消费 immutable snapshot / typed params，不查询 active Scene、ECS、当前设置或可变全局状态。
- UI compose / surface presentation 继续在应用侧 world graph 之后；不并入 recipe，不进入 Bloom。
- 原有 Forward / Deferred 不同效果插入位置、display encoding 和公开配置兼容。
- shader-facing 类型使用 Slang 生成物；不手改 `Generated/*`。

### 明确不做

- 外部 DSL、动态插件注册 / 热插拔、字符串 blackboard、万能 service locator、全局资源管理器。
- 第二套 pass scheduler、barrier compiler、transient allocator 或 import replacement 策略。
- 新渲染算法 / 视觉纠错 / 补空 pass 功能、Bloom mip 优化、Forward+ / CSM 等灯光线任务。
- 本次顺带改 View target 为逐附件增量 replacement；保持现有整组 allocation 语义。
- 全项目命名清理、目录大搬迁、`RenderSubmission` / flight / extraction 类型的整体重命名。
- 本线另建 shader descriptor schema；Slang layout 生成仍由 `rdg-cache-dx` P5 负责。

## 5. 与其他计划的唯一归属

| 既有线 | 本次关系 |
| --- | --- |
| `rdg-cache-dx` P0–P2 | 保留历史成果，作为机制基线；不重做 |
| `rdg-cache-dx` P3 | 转交本线 C1–C5；不是只完成 Bloom 整理 |
| `rdg-cache-dx` P4 | 转交本线 C1–C4；按 feature / 生命周期实际拆分，不强制保留 `*GpuResourceLibrary` 大容器 |
| `rdg-cache-dx` P5 | 保留：生成 descriptor layout；组合层只能消费真实 layout / 生成物，不能声称替它完成 |
| `render-view-family` | 保留时间轴、extraction / submission 名称收敛；旧 orchestrator / passes / resources 被本线替换后，不能再给同一旧类型安排改名批次 |
| `render-application-boundary` | 保留应用侧整帧顺序。本线只接入准备值，不把整帧编排收回 Framework |
| `shadow-and-light-line` | 保留灯光算法、shader 和预算语义。资源搬迁必须继承其最新实现 / 用例，禁止照旧布局复制 |
| `perf-baseline` | 使用已有 benchmark 机制，不建立平行性能框架 |

2026-10-12 工作区存在灯光线未提交改动，覆盖 `DeferredFrameResourceSet`、Forward、Shadow、Slang 和 extraction。C1 前必须核对 diff 与该线最新进度；未隔离这些改动不得删除 / 覆盖。必要时选择独立 worktree，基线以实际整合后的版本记录。

## 6. 执行 checkpoints

### C1 — 单源组合贯通真实 SSAO → Lighting 链

**单一验收目标**：SSAO 的开关、AO requirement、UBO / DS 和 graph 贡献由一个调用实例决定，Lighting 明确消费可选 AO；新模型进入真实 Deferred，不是只建立空框架。

实施：

1. 先收集 `validation.md` V0 基线，不计独立代码 checkpoint。
2. 落地最小 `PipelineComposition`、typed refs、feature concept、resolved / prepared storage；只实现本目标用到的机制。
3. `prepareFrameRecord()` 返回 move-only 本次准备结果，record 显式消费；冻结设置并在录制前完成持久资源 / PSO variant 准备。在安全点开始本 tick publication，准备失败不查询到旧 flight 输出。
4. SSAO 入口拥有参数解析、AO target 要求、generated UBO、frame/input DSL 与 bindings 准备、pass append。
5. Lighting 入口拥有 graph contribution 和自己的 sampled/shadow bindings，消费 `OptionalAO`；共享 frame/light 暂通过明确 typed 输入保留，不重复上传。
6. 未迁移部分只允许一个明确的 `LegacyDeferredContribution` 桥接实现，分成 prelude（GBuffer / shared inputs）与 tail（其余旧功能）调用段，允许把 SSAO→Lighting 插在二者之间；资源声明由桥接实现登记一次。不得把整条旧 orchestrator 作为黑盒调用，也不得保留两次独立 target-request 遍历。

删除 / 缩减：`bUseSSAO` 在 BuildInputs / PassContext 的透传；外层 SSAO UBO 准备与 import 分支；FrameResourceSet 的 SSAO 专属 DSL / slice / DS / slot；集中 SSAO / lighting CIS 分配；旧 `appendSSAO/appendLight` 路径。

验证：M01–M08、M10、M13 的 C1 部分；Render3D 全量；Deferred AO 开 / 关分别与旧图对照；准备后改设置不影响本次 composition；失败不发布；双 View 用例。保留 legacy 桥接的具体字段和调用方记入 progress，不宣称 Deferred 已完成。

### C2 — 多 pass 颜色贡献与公共输出闭环

**单一验收目标**：同一资源声明贯通持久目标、graph scratch、独立 pass bindings 和有效输出；多 pass feature 不穿透调用方资源 bag。

实施：Bloom 作为多 pass 验证实例；Finalize 作为必需 display 输出实例。两者遵循 C1 协议；补齐 ColorStream 的原地续写 / passthrough 语义、公共输出声明与收集。Forward / Deferred 的过渡调用都使用相同 feature 契约。

删除 / 缩减：两套 pipeline 的 Bloom attachment 字面量、import、DSL getter 分配、export 名称收集；`post.bloom/toneMap` bag；共享 `_lastBlurPassCount` 作为调用状态；feature 关闭时的 missing-target→transient 隐式兜底。

验证：M09、M11、M12、M14；Bloom 参数边界与独立 DS；grading off 仍有 Finalize；双管线各自与 baseline 一致；新增私有 scratch 不改 pipeline/core。此阶段不新增 Bloom 算法。

### C3 — Deferred 的完整地图取代横向 helper 管线

**单一验收目标**：完整 Deferred world recipe 可直接读出功能与连接；不再靠集中 PassContext / Resources / FrameResourceSet 拼装。

实施：迁入 GBuffer、共享 Deferred frame/light 输入提供者、Skybox、Sprite2D、Overlay、EntityId；接入 Shadow 调用协议，算法不动。recipe 接线与资源声明归属按 `migration.md`；所有已迁移 feature 接受自己的 typed inputs / prepared data。Material preparation 仍在 graph execute 前。

删除：`LegacyDeferredContribution`；`DeferredFrameGraphOrchestrator` / `DeferredFrameGraphPasses` / `DeferredFrameGraphResources` 的旧组合入口与巨型 context；`allocateDeferredViewPassResources`；旧 `DeferredFrameResourceSet::ViewResources` 及 feature 槽位。必要的共享 frame/light layout / 数据提供者保留为明确对象，不伪装全功能 library。

验证：M15、M17、M19；所有现存 draw buckets、skybox / sprite gating、overlay、picking / debug、环境异步完成、当前灯光 SSBO 用例通过；完整 recipe topo 与 baseline 对照。原无 draw 的 attachment 操作如不保留，必须证明无资源语义差异，不能顺手删。

### C4 — Forward 与 Shadow 接入收口，只有一种组合协议

**单一验收目标**：两条 world 管线以相同协议组合，保留不同 recipe；Shadow 不成为例外旁路，混合生命周期资源 bag 在范围内退出。

实施：Forward opaque / skybox / transparent attachment 贡献、共享 frame bindings、debug / direction 输入、EntityId、已共用颜色贡献进入 Forward recipe；Shadow 以 typed prepared/output token 接入两条 recipe。Shadow device resources 与 invocation bindings 分开，保留策略和预算；共享 skinning DSL provider 只提供 layout，不重新拥有 SSBO。

删除：Forward 旧 orchestrator / passes / resources 和 `allocateForwardViewPassResources`、混合 FrameResourceSet；剩余 feature 专属 `ViewPassResources` 集中槽位 / 分配函数；Shadow 绕过 composition 的 prepare/append 调用；迁移期间 adapter 全部退出。

验证：M16–M19；两套 recipe 顺序与资源读写不变；同 Scene 多 View 共享资源不被重建，不同 Scene 不串数据；切 pipeline / reload、resize、unregister、fence 保活。跟随实际职责删除 / 替换旧文件，不建立 forwarding stub。

### C5 — 地图诊断与扩展验收，闭合开发体验目标

**单一验收目标**：接手者从 recipe + feature 可追全功能；实际扩展不再触发集中 plumbing，运行时可核对本次组合与 RDG 的关系。

实施：从真实 resolved/prepared composition 生成诊断（调用、状态/原因、ports、owner、RDG pass 区间、有效输出），接入现有 graph trace 开关；返回诊断使用 owning 字符串，不悬挂在 graph-local 数据上。测试专用 producer / multi-pass transform / consumer 在 recipe 接线后完整经历 request→prepare→append→publish，不加入产品开关。完成最终 performance / replacement 对比及稳定规则吸收。

验收：M20–M23 与全矩阵。普通 feature 扩展不改 RDG core / pipeline context / bindings bag；diag off 不收集重型文本；保留 / 未完成 / 偏离明确记录。测试假 feature 只能验证框架，不替代真实 Forward/Deferred 证据。

## 7. 每个 checkpoint 的提交与证据

- 完整目标闭环后，代码、测试、本计划状态和 progress 同批提交，格式 `[render/composition] <精炼目标>`。
- C1 的接口 / storage / SSAO / Lighting 不拆成多个空壳进度提交；C2–C4 同理按可验收目标而非按文件数拆。
- 本次只写计划，不创建代码 checkpoint commit。未获明确授权不提交工作区其他人的改动。
- feature matrix 状态只能基于实际证据变绿。保留桥接、runtime case blocked、性能未测均应保持对应行非 verified。

## 8. 停止条件

出现以下任一项，停止扩散、记录证据并复审，而不是再加一层补丁：

- C1 仍需把 enable bool / target desc / DS 槽位穿多层，或同一 settings 再解析一次。
- 新建 composition 只包住旧 orchestrator，全局 context / bag 原样存活且没有按阶段退出。
- typed ports 需要业务侧 `std::any`、字符串查找或 feature 枚举 switch。
- 组合器开始推导 barrier / alias、调度子图，或 owner 决策进入 feature 私有缓存。
- AO / Bloom 等关闭后仍因兜底申请资源、准备 DS 或发布旧图。
- 必须变更颜色顺序、灯光算法、GPU 保活或公开设置语义才能推进。
- 实测出现稳态 GPU allocation / import replacement churn 或不能解释的性能回退。
- 相邻线改动无法隔离，或当前 baseline 本身有相关失败而未区分。

## 9. 完成条件

C1–C5 与 D1、D3–D5 完成且矩阵 verified（M25 随 `render-view-family` 交付后验收）；范围内旧横向组合链与迁移 adapter 退出；代码/测试/截图/生命周期/性能/扩展地图证据齐备；稳定规则进入 render-arch，计划才具备归档条件。仅有 design.md、类名或文件移动不构成完成。

## 10. 2026-10-13 复审与 D 系列

### 10.1 复审结论（代码核对，不是文档推断）

| 成功定义 | 现状 |
| --- | --- |
| 1 管线地图一处可读 | 部分：recipe 列出顺序，但共享 buffer import、`beginView` 上传、`postContext`、GBuffer prepare 在 `append*ViewToGraph`；skybox/billboard/gizmo 的启用与内容由 `buildOverlayFrameInputs` 在录制期遍历 live ECS 决定；发布靠每管线 `exportGraphOutputs`/`collectViewOutput` 字符串往返 |
| 2 feature 契约局部完整 | 否：加一个 feature 要改 recipe 的 Config/ResolvedView/PreparedView/GraphOutputs/resolve/prepare/append/collectViewTargets/describe/Stages 及 pipeline 侧 Stages/beginSubmission/export/collect；漏 `collectViewTargets` 只在运行期报 `MissingDependency`。feature 无状态、每次局部构造，状态/设备资源/生命周期仍在 Stage 与 pipeline |
| 3 同一份 resolved composition 驱动全链路 | 否：SSAO 有 `_bEnableSSAO` + Stage setter 第二份状态；shadow 有 resolve、逐 View `captureShadowSettings`、`applyShadowFrameSettings` 三个来源；`recordFamily` 逐 View 调 `applyPendingResourceRefreshes()`，录制期 destroy/init shadow 资源（违反核心规则 6 与 C1 第 3 步；`prepareDevice` 未实现） |
| 4 减少 plumbing | 否：`PrepareContext`/`AppendContext` 携带 overlayInputs（prepare 写、EntityId append 读的隐式数据流）、postContext、frameBinding、environmentLighting；源码净增约 2,900 行 |
| 5 行为不变 | 是：逐像素证据成立 |

协议层面另有：`resolve` 的 `Inputs` 是 RG handle、resolve 时恒为空；同一 `Facts` 在 resolve 与 append 填不同字段；typed ports 为裸 `RGTextureHandle`（M01/M05 原 evidence 以"设计修正"替代验收，不符合 matrix status_rules）；M21 测试未经 recipe/request/publish；append 中途失败时已加入 family graph 的 pass 无法撤回。

### 10.2 用户决策（2026-10-13）

- Feature **吸收 Stage**：设备对象、自身 Settings、beginFrame/refresh 生命周期归 feature；可按 feature 分批迁移（D5）。
- 录制期 overlay / skybox / billboard / gizmo 改为 extraction 快照：**交给 `render-view-family`**（已登记其 todo），本线 M25 blocked_on 该项，不在本线改 extraction。
- View append 失败：**整个 family 失败**，不执行该 graph；不给 RDG 加 rollback。
- 在本目录重新打开，不另起新线。

### 10.3 D 系列 checkpoints（顺序 D1 → D3 → D4 → D5；D2 外部交付）

#### D1 — 安全点冻结设置，录制期不重建持久资源

**单一验收目标**：一帧内所有 feature 启用与参数只来自安全点 resolved `Config`；持久设备资源（shadow images、PSO variant）在 `prepareFrameRecord` 内准备完毕。

实施：`applyPendingSettings` / `applyPendingResourceRefreshes` / shadow resource refresh 移入安全点；删除逐 View `captureShadowSettings`、`syncFrameSettings` 与其死代码；`_bEnableSSAO`、SSAO Stage setter 不再作为录制期决策来源，`beginSubmission` 的 warmup 按 resolved composition 决定；补 design §2 的 device-prepare 步骤（最小形态，只覆盖现有需要的 shadow 与格式 variant）。

验证：M24、M02；`recordFamily`/录制路径无 `applyPending*` 调用；准备后改设置不影响本帧（单测）；shadow 开关切换 trace 中录制期无 destroy/init；三帧逐像素对照。

#### D2 — feature 输入来自快照（外部交付：`render-view-family`）

skybox 可用性、billboard、direction gizmo 进 extraction / `frameData`；删 `RenderPipelineFrameContext::derivedScene`；`overlayInputs` 不再是可变共享包。本线只在交付后把 Overlay/Skybox/EntityId 的 Facts 改读快照并验收 M25。

#### D3 — recipe 以单条 invocation 记录组织，接线遗漏编译期暴露

**单一验收目标**：新增 feature 的触点 = feature 本身 + recipe 一处登记 + `appendGraph` 连线；`collectViewTargets` / `describe` / prepare 不再手工逐 feature 列举。

实施：`Invocation<F>{Resolved, Prepared, GraphOutputs}` 与 recipe 级 `std::tuple`；concept 增加 `targets(const Resolved&)` 与名称；`collectViewTargets` / `describe` 为 fold；拆 `ResolveInputs` 与 `GraphInputs`，删除 resolve 恒空参数；`Facts` 按阶段拆分或只保留真实字段。不引入 `std::any`、type_index、字符串查找。

验证：M01、M05、M21、M26；M21 测试改走真实 recipe 的 request → prepare → append → publish；负向：去掉某 feature 的 targets 登记编译失败。

#### D4 — 输出直接发布，family 失败原子

**单一验收目标**：`GraphOutputs` 到 `RenderViewOutput` 一处映射，不经 pipeline 内部 export 字符串往返；任一 View 的 prepare/append 失败使整个 family 不执行、不发布。

验证：M13、M10、M27；failure injection（append 中途失败）后 graph 不执行、该 family 无发布、其他 family 不受影响。

#### D5 — feature 吸收 Stage（按 feature 分批）

**单一验收目标（每批）**：被迁移 feature 拥有其设备对象、Settings 与 init/beginFrame/refresh/shutdown；pipeline 不再持该 Stage 成员与对应 setter；concept 去掉 `typename F::Stage` / `Stage*` 参数（最后一批）。

顺序建议：SSAO → Skybox/Overlay → Bloom/Finalize → Lighting/GBuffer → Shadow（最后，依赖 D1 的 device-prepare 与 `shadow-and-light-line` 最新实现）。每批独立提交，带逐像素证据。

验证：M28；每批 Render3D 全量 + 三帧逐像素；pipeline 头文件中被迁移 Stage 成员数递减可核对。

### 10.4 D 系列停止条件

除 §8 外：D3 需要类型擦除容器或运行期查找才能 fold；D5 某批迁移需要改变 shader / 灯光算法 / 公开设置结构；D1 发现安全点无法拿到某个 device-prepare 所需的事实（应先报告缺哪个事实，而非退回录制期准备）。
