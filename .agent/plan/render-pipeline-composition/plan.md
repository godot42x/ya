# RDG 上方的管线组合层收敛

> 建立 / 更新：2026-10-13
> 状态：实施进行中。已落地（均带 runtime 逐像素证据）：C1-foundation + 集成切片（GBuffer→SSAO→Lighting 单源贯通）、C3A（Deferred tail 全 feature 化 + 集中层删除）、C3A-trim、C3B（Deferred 共享输入收口）、C3G（C3 剩余门禁）、协议诚实化（Feature concept 由 9 个 feature static_assert 强制）、C4a（Forward recipe + Bloom/EntityId/Finalize 跨管线共享 + M17 Forward 半边 + appendTargetRequests 退出）。
> 下一 checkpoint：C4b（Shadow typed token / 剩余槽位复核）；随后 C5（诊断、扩展用例、多 View、performance）。
> 命名对应：本文件 C1 = 实际执行的 "C1-foundation + 集成切片"；C2 剩余项、C3 遗留 hook 与 C4b 见 todo.md。

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

C1–C5 完成且矩阵 verified；范围内旧横向组合链与迁移 adapter 退出；代码/测试/截图/生命周期/性能/扩展地图证据齐备；稳定规则进入 render-arch，计划才具备归档条件。仅有 design.md、类名或文件移动不构成完成。
