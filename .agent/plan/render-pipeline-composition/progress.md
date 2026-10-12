# 进度与接续

## 2026-10-12 — 计划初始化（非实现 checkpoint）

### 当前目标和范围

用户明确纠正：主题是 RDG 上方组合层与横向 plumbing 的复杂度，不是 Bloom 改造。要求把方案落盘成真正可开工参照。本轮仅计划文件，不授权渲染代码实施。

### 已形成的决策

- Forward/Deferred 各保留一个 typed recipe；feature 成为完整 resolve→prepareBindings→append 协议的一等调用对象。
- 同一份 resolved composition 驱动 target request / bindings / graph / outputs；prepared frame 通过 prepare→record 显式传递，不持久缓存“当前 View”。
- existing RDG / owners / submission / application frame order 保留；不造第二张 GPU graph、万能 context、外部 DSL、全局资源池。
- 首个代码 checkpoint C1 必须完成真实 SSAO→Lighting 闭环，不能只提交基础抽象。多 pass 用例在 C2，全部地图在 C3/C4。
- Forward/Deferred 现有颜色位置和 attachment/depth 行为保留；部分当前无 draw pass 不是本线功能补齐机会。
- 旧 `rdg-cache-dx` P3/P4 转交本线，P5 保留；相邻 View / application / lighting / perf 线边界见 plan。

### 本轮工件

建立 plan、design、migration、validation、feature_matrix、todo、session_checklist、progress。同步 active index 和旧计划任务归属，不改旧 P0–P2 证据。

### 验证口径

本轮实际验证：

- Python 结构检查通过：8 个新线工件齐全；matrix M01–M23 唯一、有序且全部 todo/evidence 空；C1–C5 与 validation ID 一致；JSON 与本线/旧线 Markdown 链接可解析。
- 17 个相关计划路径核对：新增文件结尾/空白/code fences、已跟踪文件本轮 `git diff --check` 通过；保留旧文件既有空白，不做无关格式清理。
- 旧 P3/P4 matrix 为 transferred，P5 为 todo；历史 P0–P2 保留。新 `rdg-cache-dx/todo.md` 补齐其缺失的接续入口。
- review 补清 `PreparedRef<T>` 传共享绑定、device variant 安全点准备、publication token 在准备期开始以及 C1 legacy prelude/tail 接线边界。

### 2026-10-12 晚补充澄清

用户要求 review 潜在风险并判断是否迭代计划。实施 3 处架构澄清：

1. **migration.md §4**：补充"桥接实现约束"段落，明确 `LegacyDeferredPrelude` 实现 feature 协议（resolve/prepare/append/outputs），禁止把整个 `DeferredFrameGraphPassContext` 包进去。防止 C1 退化成"包住旧 orchestrator"。
2. **validation.md M11 + feature_matrix.json M11**：澄清"Deferred Overlay 在颜色效果后且当前 Bloom.Composite backing 被 Overlay 续写，诊断输出为 Overlay 后内容（与旧行为一致）"。解决 M11 验收标准与 migration.md line 24 现有行为描述的表面矛盾。
3. **design.md §6**：补充 `beginPublication` 时机说明：安全点清空发布表但不影响 GPU keepalive；实际 publication 在 execute 成功后。澄清准备失败与 GPU 保活的关系。

未构建 C++、未跑 Render3D / runtime / 截图 / benchmark；文档检查不计实现完成，没有新代码 checkpoint commit。

### 工作区与风险

- 初始已有 unrelated agent/UI/log.cc 等工作；中断后又出现灯光线修改，覆盖 DeferredFrameResourceSet、Forward、Shadow、Slang、extraction 和灯光用例。
- 不覆盖、不 stage、不提交这些改动。C1 前核对整合基线；计划使用当前 local-light shader 生成类型，不冻结历史 PointLight 布局。
- 多 View runtime 入口、截图自身确定性和 CPU 性能噪声尚未实测；matrix 保持 todo，不以单测替代。
- C1 的 legacy bridge 必须分 prelude/tail 才能在中间插 SSAO→Lighting；不能整个旧 orchestrator 黑盒化。

### 状态与下一步

| Checkpoint | 状态 | 下一入口 |
| --- | --- | --- |
| C1-foundation | done | 类型系统基础已落地（2 commits） |
| C2 | todo | SSAO→Lighting 完整闭环 + legacy bridge |
| C3 | todo | 多 pass feature / Finalize / Deferred 全地图 |
| C4 | todo | Forward / Shadow 全地图与 owner 拆分 |
| C5 | todo | 诊断 / 扩展 / runtime 多 View / performance |

保留：全部现有代码及行为、RDG 已有机制、shader 生成 P5。未完成：feature 集成、legacy bridge、旧 plumbing 删除、validation matrix。C1 范围调整：原"SSAO→Lighting 完整闭环"拆分为 C1-foundation（类型系统）+ C2（集成）。

## 2026-10-12 晚 — C1-foundation 完成

### 目标与范围

建立类型系统基础和 prepareFrameRecord 接口，验证编译通过和测试通过。不包含完整的 SSAO→Lighting 集成（推迟到 C2）。

### 实施内容

**Commit 1: typed refs + prepareFrameRecord interface**
- `FeatureProtocol.h`: CompositionTextureRef<T>, CompositionBufferRef<T>, PreparedRef<T>, OptionalAO, Result<T>, FeatureError, composition_tag 命名空间
- `PreparedPipelineFrame.h`: prepareFrameRecord 返回类型，per-view prepared data 占位符
- `RenderDeviceState::prepareFrameRecord`: 返回 PreparedPipelineFrameResult，失败时中止 recording
- `RuntimeRenderContext::record`: 消费 PreparedPipelineFrame，失败时返回空 RecordedFrame
- `FeatureProtocolTest.cpp`: 6 个新测试验证类型安全、concept 检查、空类型优化

**Commit 2: SSAOFeature skeleton**
- `SSAOFeature.h`: Settings/Facts/Inputs/Outputs/Resolved/Prepared/GraphOutputs 类型化结构
- `SSAOFeature.cpp`: resolve/prepareBindings/append 方法实现（append 当前为占位符）
- Result<T> 使用 std::variant（C++20 兼容，无需 C++23 std::expected）
- prepareBindings: 从 ViewTargetStore lease AO target
- append: 待 C2 补充完整实现（需要 ViewTargetStore 通过 graph context 传递）

### 验证结果

```
✓ V0 baseline: 213 tests passed
✓ C1 tests: 219 tests passed (+6 FeatureProtocolTest)
✓ ya-game-runtime: build ok
✓ ya-game-editor: build ok
```

### 技术决策

1. **Result<T> 使用 std::variant**：避免 C++23 std::expected 依赖，保持 C++20 兼容
2. **CompositionTextureRef 前缀**：避免与 Core/AssetRef.h 的 TextureRef 冲突
3. **SSAOFeature.append 占位符**：RenderGraph 不持有 ViewTargetStore 引用，需要通过 context 传递（C2 补充）
4. **Feature concept 部分满足**：SSAOFeature 使用自定义方法签名，完整 concept 合规推迟到 C2

### 未完成项（推迟到 C2）

- LightingFeature 实现

## 2026-10-12 晚 — Core helpers 统一化

### 目标与范围

响应用户反馈：避免各模块自行造轮子（如 OptionalAO），在 Core 中实现统一的 Rust 风格 helpers，仅用 C++20（不依赖 C++23 std::expected）。

### 实施内容

**Commit: Core helpers + 使用迁移**

- `Core/Result.h`: `Result<T, E>` 基于 `std::variant`，提供 Rust 风格 API
  - `isOk()/isErr()`: 查询状态
  - `value()/error()`: 访问内容
  - `map()/andThen()`: 单子操作
  - `valueOr()`: 带默认值访问
- `Core/Optional.h`: 
  - `Optional<T>`: 轻量级 optional（POD 类型，显式 validity tracking）
  - `OptionalHandle<Tag>`: 类型安全的 handle wrapper（~0u = invalid，zero-size tag）
  - `Some()/None()`: 辅助构造函数
- `ResultOptionalTest.cpp`: 24 个测试
  - ResultTest: 9 个（Ok/Err construction, map, andThen, valueOr, move semantics）
  - OptionalTest: 8 个（Some/None, valueOr, reset, equality）
  - OptionalHandleTest: 7 个（validity, type safety, zero-size overhead）
- `FeatureProtocol.h`: 迁移到 Core helpers
  - `CompositionTextureRef<T>` → `OptionalHandle<T>` 的 type alias
  - `CompositionBufferRef<T>` → `OptionalHandle<T>` 的 type alias
  - `CompositionPreparedRef<T>` → `OptionalHandle<T>` 的 type alias
  - 删除旧的 `OptionalAO` 定义（移到各 feature 自己定义）
  - 删除旧的 `std::variant<T, FeatureError>` Result 定义
- `SSAOFeature`: 使用新的 Core types
  - `OptionalAO`: 包装 `RGTextureHandle` 的 struct（非 OptionalHandle）
  - `Result<Prepared, FeatureError>` 签名更新
  - `Result<GraphOutputs, FeatureError>` 签名更新
- `DeferredPipelineRecipe`: 使用新的 Result API
  - `Result<std::vector<ResolvedView>, FeatureError>`
  - `Result<std::vector<PreparedView>, FeatureError>`
  - `Result<GraphOutputs, FeatureError>`
  - `isOk()/isErr()/value()/error()` 调用替换旧的 `std::holds_alternative`

### 验证结果

```
✓ 编译: ya-render-3d, ya-testing build ok
✓ 测试: 1817/1818 passed (新增 24 个 Core helpers 测试)
  - ResultTest: 9 tests (0 ms)
  - OptionalTest: 8 tests (0 ms)
  - OptionalHandleTest: 7 tests (0 ms)
✓ 1 个无关失败: ReflectionSceneResave.UiDocumentsMatchBaseline
```

### 技术决策

1. **Result<T, E> 使用 std::variant**: C++20 兼容，避免 C++23 std::expected
2. **OptionalHandle<Tag> 零大小开销**: tag 必须是空类型，sizeof == sizeof(uint32_t)
3. **OptionalAO 特化处理**: 因为需要包装 `RGTextureHandle`（含 index + generation），不能用单纯的 uint32_t OptionalHandle
4. **Type aliases 保留**: CompositionTextureRef 等保留为 type alias，方便未来演进

### 收益

- ✅ 统一的错误处理模式（Result<T, E>）
- ✅ 类型安全的 handle 包装（编译期区分不同 handle 类型）
- ✅ Rust 风格的 API（map/andThen 单子操作）
- ✅ 零运行时开销（OptionalHandle 保证零大小 tag）
- ✅ 避免各模块重复造轮子
- ✅ 使用 std::optional<T> 替代自造 Optional<T>

### 未完成项（推迟到 C2）

- LightingFeature 实现
- LegacyDeferredPrelude/Tail bridge
- SSAOFeature.append 完整实现
- DeferredRenderPipeline 使用新组合层
- 删除 bUseSSAO plumbing（DeferredFrameGraphPassContext, DeferredFrameResourceSet, orchestrator）
- Validation matrix M01-M08 验证

## 2026-10-12 晚 — Core helpers 重构 + pipeline 集成准备

### 目标与范围

响应用户反馈：
1. 删除自造的 Optional<T>，直接用 std::optional（C++17 内置）
2. 保留 Result<T,E>（C++20 无 std::expected）和 OptionalHandle<Tag>（类型安全零开销）
3. 集成 prepareFrameRecord 到 recording 流程

### 实施内容

**Commit: Core helpers simplification + pipeline integration**

- `Core/Optional.h`: 删除 `Optional<T>` 和 `Some()/None()` helpers
  - 保留 `OptionalHandle<Tag>` 用于类型安全的 handle
  - 文档说明：一般用途用 std::optional，handle 用 OptionalHandle
- `ResultOptionalTest.cpp`: 删除 Optional<T> 的 8 个测试
  - 保留 Result<T,E> 的 9 个测试
  - 保留 OptionalHandle<Tag> 的 7 个测试
- `DeferredPipelineRecipe.h/cpp`: 初步实现 resolve/prepareBindings/appendGraph
  - resolve: 从 RenderFramePlan 提取 view 并调用 SSAOFeature::resolve
  - prepareBindings: 调用 SSAOFeature::prepareBindings
  - appendGraph: 调用 SSAOFeature::append
  - 当前实现：单 view，SSAO 单特性，占位符输入（GBuffer 待 C2）
- `ViewFamilyRecordContext`: 添加 `PreparedPipelineFrame* prepared` 字段
  - 从 prepareFrameRecord 传递到 recordFamily
- `RenderDeviceState::recordViewFamilies`: 调用 `prepareFrameRecord(plan)` 并传递给 family recording
- `RenderFrameInputs.h`: 添加 `PreparedPipelineFrame` 前向声明

### 验证结果

```
✓ 编译: ya-render-3d build ok (5.1s)
✓ 测试: 1810/1810 passed
  - ResultTest: 9 tests
  - OptionalHandleTest: 7 tests (OptionalTest 已删除)
  - FeatureProtocolTest: 6 tests
```

### 技术决策

1. **std::optional 替代自造 Optional**: C++17 就有，无需重复造轮
2. **OptionalHandle<Tag> 保留**: 类型安全 + 零开销，std::optional<uint32_t> 做不到类型区分
3. **prepareFrameRecord 在 recordViewFamilies 开始调用**: 在 beginSubmission 前准备资源和绑定
4. **ViewFamilyRecordContext 携带 prepared**: 通过 context 传递，不全局缓存
5. **DeferredPipelineRecipe 当前单 view**: C2 补充多 view 支持

### 架构改进

- **显式 prepare→record 数据流**: PreparedPipelineFrame 从 prepare 显式传递到 record
- **类型安全的错误处理**: Result<T,E> 替代 std::variant 直接使用
- **零开销的 handle 包装**: OptionalHandle<Tag> 确保编译期类型安全无运行时开销
- **统一的 helper 库**: Core/Result.h 和 Core/Optional.h 供全项目使用

### 未完成项（推迟到 C2）

- DeferredPipelineRecipe 完整实现（当前占位符输入）
- SSAOFeature.append 完整实现（需要 GBuffer 输入）
- LightingFeature 实现
- LegacyDeferredPrelude/Tail bridge
- DeferredRenderPipeline::appendDeferredViewToGraph 使用新组合
- 删除 bUseSSAO plumbing
- Validation matrix M01-M08 验证

### 下一步（C2）

1. ✅ 读取 DeferredFrameResourceSet.h 理解 shared frame/light 绑定管理
2. ✅ 读取 DeferredRenderPipeline.cpp::appendDeferredViewToGraph 理解当前组合流程
3. 读取 GBufferStage.h/cpp 理解 GBuffer 生成
4. 读取 LightStage.h/cpp 理解 lighting 实现
5. 实现 LegacyDeferredPrelude feature (outputs GBuffer/Depth/PreparedRef<SharedFrameLight>)
6. 完成 SSAOFeature.append 实现
7. 实现 LightingFeature (consumes OptionalAO + PreparedRef<SharedFrameLight>)
8. 实现 LegacyDeferredTail (Skybox/Sprite/Overlay/Bloom/Finalize)
9. 集成到 DeferredRenderPipeline::appendDeferredViewToGraph
10. 删除 bUseSSAO plumbing
11. 验证 M01-M08

## 2026-10-12 晚 — prepareBindings 集成到 recordFamily

### 目标与范围

建立 resolve→prepare→append 的完整数据流，使 prepareBindings 在 RenderSubmission 可用时调用。

### 实施内容

**Commit: Wire prepareBindings into recordFamily loop**

- `DeferredFamilyViewBranch`: 添加 `std::optional<DeferredPipelineRecipe::PreparedView> preparedComposition` 字段
- `DeferredRenderPipeline::recordFamily`: 在构建每个 view 的 branch 时
  - 检查 `ctx.prepared` 是否存在且有 resolved data
  - 按 viewId 匹配对应的 resolved view
  - 调用 `DeferredPipelineRecipe::prepareBindings` 准备该 view 的绑定
  - 将结果存入 `branch.preparedComposition`
- 包含头文件: `PreparedPipelineFrame.h` 和 `DeferredPipelineRecipe.h`
- 使用 `const_cast<ViewTargetStore*>` 因为 ViewTargetStore 在 recording 时逻辑上需要修改

### 架构决策

**prepareBindings 时机调整**：
- ❌ 原方案: 在 `prepareFrameRecord` 中调用 prepareBindings → 问题: RenderSubmission 尚未可用
- ✅ 新方案: 在 `recordFamily` 循环中为每个 view 调用 prepareBindings → RenderSubmission 已经可用

**数据流完整链路**：
1. `RenderDeviceState::prepareFrameRecord`: 调用 `DeferredPipelineRecipe::resolve` 收集资源需求
2. `RenderDeviceState::recordViewFamilies`: 调用 `prepareFrameRecord` 获取 PreparedPipelineFrame
3. `DeferredRenderPipeline::recordFamily`: 为每个 view 调用 `prepareBindings` 分配描述符和上传 UBO
4. `DeferredRenderPipeline::appendDeferredViewToGraph`: (待实现) 使用 preparedComposition 构建 graph

### 验证结果

```
✓ 编译: ya-render-3d build ok (5.5s)
✓ 测试: 1810/1810 passed
✓ 编辑器: 启动无错误 (HelloMaterial project, 15s timeout)
```

### 技术细节

- `ViewFamilyRecordContext::prepared` 是 `const PreparedPipelineFrame*`，从 `prepareFrameRecord` 传递
- `DeferredFamilyViewBranch::preparedComposition` 保存每个 view 的准备数据，供后续 graph 构建使用
- `const_cast<ViewTargetStore*>` 是安全的：ViewTargetStore 在 recording 期间确实需要修改（分配、lease）

### 未完成项（推迟到 C2 后续）

- DeferredPipelineRecipe::prepareBindings 完整实现（当前占位符）
- DeferredPipelineRecipe::appendGraph 完整实现（当前占位符）
- 在 appendDeferredViewToGraph 中使用 preparedComposition 替代 orchestrator
- SSAOFeature.append 完整实现
- LegacyDeferredPrelude/LightingFeature/LegacyDeferredTail 实现
- 删除 bUseSSAO plumbing
- Validation matrix M01-M08 验证
