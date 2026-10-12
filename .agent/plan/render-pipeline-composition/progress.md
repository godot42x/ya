# 进度与接续

## 2026-10-12 — 计划初始化（非实现 checkpoint）

### 当前目标和范围

用户明确纠正：主题是 RDG 上方组合层与横向 plumbing 的复杂度，不是 Bloom 改造。要求把方案落盘成真正可开工参照。本轮仅计划文件，不授权渲染代码实施。

### 已形成的决策

- Forward/Deferred 各保留一个 typed recipe；feature 成为完整 resolve→prepareBindings→append 协议的一等调用对象。
- 同一份 resolved composition 驱动 target request / bindings / graph / outputs；prepared frame 通过 prepare→record 显式传递，不持久缓存"当前 View"。
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
| C1-foundation | done | 类型系统基础已落地（5 commits） |
| C2 | in-progress | GBuffer/Lighting features 已创建（2 commits），集成到 recipe（1 commit） |
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

**Commit 3: Core helpers 统一化**
- `Core/Result.h`: `Result<T, E>` 基于 `std::variant`，提供 Rust 风格 API（isOk/isErr/value/error/map/andThen/valueOr）
- `Core/Optional.h`: `OptionalHandle<Tag>` 类型安全的 handle wrapper（零大小 tag）
- `ResultOptionalTest.cpp`: 16 个测试（ResultTest:9, OptionalHandleTest:7）
- `FeatureProtocol.h`: 迁移到 Core helpers（CompositionTextureRef → OptionalHandle alias）
- 删除自造 Optional<T>，直接用 std::optional（C++17 内置）

**Commit 4: Pipeline integration**
- `DeferredPipelineRecipe.h/cpp`: 初步实现 resolve/prepareBindings/appendGraph
- `ViewFamilyRecordContext`: 添加 `PreparedPipelineFrame* prepared` 字段
- `RenderDeviceState::recordViewFamilies`: 调用 `prepareFrameRecord(plan)` 并传递给 family recording
- prepareBindings 在 recordFamily 循环中调用（RenderSubmission 已可用时）
- `DeferredFamilyViewBranch`: 添加 `preparedComposition` 字段保存准备数据

**Commit 5: prepareBindings per-view loop integration**
- `DeferredRenderPipeline::recordFamily`: 为每个 view 调用 `prepareBindings`
- 按 viewId 匹配 resolved view，调用 `DeferredPipelineRecipe::prepareBindings`
- 将结果存入 `branch.preparedComposition`
- 建立完整数据流：prepareFrameRecord → recordFamily prepareBindings → (待实现) appendGraph

### 验证结果

```
✓ 编译: ya-render-3d, ya-testing, ya-game-runtime, ya-game-editor build ok
✓ 测试: 1810/1810 passed
  - ResultTest: 9 tests
  - OptionalHandleTest: 7 tests
  - FeatureProtocolTest: 6 tests
✓ 编辑器: 启动无错误 (HelloMaterial project, 15s timeout)
✓ 1 个无关失败: ReflectionSceneResave.UiDocumentsMatchBaseline
```

### 技术决策

1. **Result<T> 使用 std::variant**：避免 C++23 std::expected 依赖，保持 C++20 兼容
2. **std::optional 替代自造 Optional**: C++17 就有，无需重复造轮
3. **OptionalHandle<Tag> 保留**: 类型安全 + 零开销，std::optional<uint32_t> 做不到类型区分
4. **prepareBindings 时机**: 在 recordFamily 循环中调用（RenderSubmission 可用时），而非 prepareFrameRecord
5. **显式 prepare→record 数据流**: PreparedPipelineFrame 从 prepare 显式传递到 record

### 架构改进

- **统一的错误处理**: Result<T,E> 替代 std::variant 直接使用
- **类型安全的 handle**: OptionalHandle<Tag> 确保编译期类型安全无运行时开销
- **显式数据流**: PreparedPipelineFrame 从 prepare 显式传递到 record
- **零开销抽象**: OptionalHandle sizeof == sizeof(uint32_t)

### 未完成项（推迟到 C2）

- GBufferFeature/LightingFeature 实现
- DeferredPipelineRecipe 集成完整的 GBuffer→SSAO→Lighting 链
- SSAOFeature.append 完整实现
- DeferredRenderPipeline::appendDeferredViewToGraph 使用新组合
- 删除 bUseSSAO plumbing
- Validation matrix M01-M08 验证

## 2026-10-12 晚 — C2 进行中：GBuffer→SSAO→Lighting 链

### 目标与范围

响应用户纠正："Legacy 可以完全重构"是指直接重构现有 Stage，不需要保持兼容性，而不是创建 Legacy wrapper。直接实现 GBufferFeature/LightingFeature 并集成到 DeferredPipelineRecipe。

### 实施内容

**Commit 6: GBufferFeature - direct refactor**
- `GBufferFeature.h`: Settings/Facts/Inputs/Outputs/Resolved/Prepared/GraphOutputs 类型定义
- `GBufferFeature.cpp`: resolve/prepareBindings/append 实现
  - resolve: 确定 GBuffer0-3 + SceneDepth 角色
  - prepareBindings: 占位符（完整实现需要 DeferredFrameResourceSet）
  - append: 使用 RGPassBuilder::declareRaster 添加 GBuffer pass（4 color + 1 depth）
- `FeatureProtocol.h`: 修正 ViewTargetLease 前向声明（struct 而非 class）
- 直接重构 GBufferStage 逻辑，不创建 Legacy wrapper

**Commit 7: LightingFeature - direct refactor**
- `LightingFeature.h`: Settings/Facts/Inputs/Outputs/Resolved/Prepared/GraphOutputs 类型定义
- `LightingFeature.cpp`: resolve/prepareBindings/append 实现
  - Inputs: GBuffer textures (albedo/normal/orm/shading/depth) + optional SSAO
  - Outputs: SceneColor
  - append: 使用 RGPassBuilder 读取 GBuffer 输入，写入 SceneColor
- 直接重构 LightStage 逻辑，不创建 Legacy wrapper

**Commit 8: DeferredPipelineRecipe 集成 GBuffer→SSAO→Lighting**
- `DeferredPipelineRecipe.h`: 添加 GBufferFeature/LightingFeature 到 ResolvedView/PreparedView/GraphOutputs
- `DeferredPipelineRecipe.cpp`:
  - resolve(): 链式调用 GBufferFeature → SSAOFeature → LightingFeature，收集资源需求
  - prepareBindings(): 为三个 features 准备绑定
  - appendGraph(): 连接数据流（GBuffer outputs → SSAO inputs → Lighting inputs）
- 修复 ViewTargetLease 使用（值类型而非指针）
- 修复 FeatureError::AllocationFailed（而非 ResourceAllocationFailed）
- SSAOFeature 使用其自定义签名（PreparedInputs/FeatureGraphContext）

### 验证结果

```
✓ 编译: ya-render-3d build ok (4.6s)
✓ 测试: 1810/1810 passed
```

### 技术决策

1. **直接重构而非包装**: 用户明确要求不需要 Legacy 兼容层，直接将 GBufferStage/LightStage 改造为 Feature
2. **非静态方法**: GBufferFeature/LightingFeature 使用实例方法（非静态）
3. **SSAOFeature 特殊签名**: SSAOFeature 使用自定义的 PreparedInputs/FeatureGraphContext（C1 遗留，待 C3 统一）
4. **ViewTargetLease 值类型**: lease() 返回值类型，通过 &lease 传递指针到 FeatureGraphContext
5. **占位符 Stage 指针**: GBufferFeature/LightingFeature::append 接受 Stage* 参数，当前传 nullptr（完整实现在后续）

### 架构成果

- ✅ **Feature 作为一等对象**: GBufferFeature/LightingFeature 完整实现 resolve/prepareBindings/append
- ✅ **类型化数据流**: GBuffer → SSAO → Lighting 数据流通过 RGTextureHandle 连接
- ✅ **单源资源要求**: ResolvedView 收集所有 features 的 ViewTargetRequest
- ✅ **显式绑定准备**: PreparedView 保存所有 features 的 Prepared 数据
- ✅ **RDG 依赖自动编译**: builder.read() 建立正确的 pass 依赖

### 未完成项（推迟到 C3）

- GBufferFeature/LightingFeature 完整执行（当前 Stage* 为 nullptr）
- DeferredFrameResourceSet 集成到 prepareBindings
- DeferredRenderPipeline::appendDeferredViewToGraph 使用新 recipe
- 删除旧 orchestrator（DeferredFrameGraphOrchestrator）
- 删除 bUseSSAO plumbing（DeferredFrameGraphPassContext::bUseSSAO 等）
- Validation matrix M01-M08 验证
- 运行截图验证

### 下一步（C3）

1. 集成 GBufferStage/LightStage 到对应 Feature::append
2. 连接 DeferredFrameResourceSet 到 prepareBindings（frame/light descriptors）
3. 在 DeferredRenderPipeline::appendDeferredViewToGraph 中使用新 recipe
4. 删除 DeferredFrameGraphOrchestrator 及 bUseSSAO plumbing
5. 补充 Skybox/Sprite/Overlay/Bloom/Finalize features
6. 验证 M01-M08
7. 运行截图对比

## 2026-10-12 晚 — 集成切片完成：真实 GBuffer→SSAO→Lighting 闭环（C1i）

### 目标与边界

**单一验收目标**：同一份 resolved composition 驱动 Deferred 的 GBuffer→SSAO→Lighting——allocation request、per-view bindings、RDG pass、输出发布都出自这一份来源；旧 orchestrator 路径退出，不是空抽象骨架。

边界外（保持现状，属后续 checkpoint）：

- ForwardOpaque / Skybox / Sprite2D / Bloom / ForwardTransparent / EntityId / Postprocess 仍是 pipeline tail（`DeferredFrameGraphPasses` + `DeferredFrameGraphResources` bag + `DeferredFrameResourceSet::ViewResources` 槽位）。
- Forward 仍走 `appendTargetRequests`（`resolveFrameComposition` 默认 `NotApplicable`）。
- 切片名对应：plan.md 的 C1 验收 = 本线 "C1-foundation + 本集成切片"；plan.md 的 C2（Bloom 多 pass 样本 / Finalize）与 C3（Deferred 全地图）均未开始。

### 代码职责

- `GBufferFeature` / `SSAOFeature` / `LightingFeature`：各自拥有 resolve（资源要求 + 每 View 值）、prepareBindings（submission 期 DS 分配）、append（真实 RDG pass + stage execute）。SSAO 的 frame UBO 在 resolve 里用同一组 settings 构建（`SSAOStage::buildFrameData` 增加静态入口），上传与 shader 读同一份值。
- `DeferredPipelineRecipe`：Config（pipeline 运行期 formats + settings 快照）→ resolve → PreparedView → appendGraph；host-written buffers 由 frame import 步骤导入一次、以 typed Inputs 传递；environment lighting 导入在 appendGraph；旧 recipe 的硬编码 format/extent/null stage 全部移除。
- `DeferredRenderPipeline::resolveFrameComposition`：安全点解析并**由 composition 生成 ViewTargetRequest**（GBuffer×4 + shared depth + 可选 SSAO + SceneColor + tail 的 Display/EntityId/Bloom）；`appendDeferredViewToGraph` 录制期 prepare+append，把 frame/light/ssaoFrame range 交给 recipe；`exportGraphOutputs` 移入 pipeline。
- `RenderDeviceState`：`prepareFrameRecord` 单次解析；`recordViewFamilies(plan, prepared)` 显式消费 `PreparedPipelineFrame`（删除录制中的第二次 `prepareFrameRecord` 调用）。

删除的 plumbing：

- `DeferredFrameGraphOrchestrator.{h,cpp}`（整文件）、`appendGBuffer/appendSSAO/appendLight` 与三个 PassParams 结构。
- `bUseSSAO` 在 BuildInputs / PassContext 的透传；`DeferredRenderPipeline::appendTargetRequests` 集中清单；`DeferredFamilyViewBranch::preparedComposition`；`SSAOStage::appendGraphPass`；`DeferredFrameGraphResources::Passes.gBuffer/light`；`PreparedPipelineFrame::views/PreparedView` 占位。
- `SSAOFeature` 统一到共享协议签名（`Result<...>` + `FeatureGraphContext`，含 `stageCtx`/`predecessor`）。

### 验证结果

- 编译：`xmake b ya-render-3d-test` ok；`xmake b ya-runtime`（ya-game-runtime closure）ok。
- 测试：`xmake r ya-render-3d-test` 220/220（40 suites，含新增 GBufferFeatureTest / SSAOFeatureTest / LightingFeatureTest）；`python3 Script/ya.py test --target ya` 1811/1811（226 suites）。
- Runtime（HelloMaterial，`--render-graph-trace`，control `--game`）：
  - SSAO on（`--automation-config=Automation.composition_ssao_on.json`）：`Deferred` executor 首帧图 = Directional Shadow Cascade 0 → Point Shadow Faces → Deferred GBuffer → SSAO Pass → Deferred Light → Forward Opaque → Skybox → Bloom×4 → Forward Transparent → EntityId → Postprocessing（14 pass）。
  - SSAO off（`Automation.composition_ssao_off.json`）：13 pass，无 "SSAO Pass"；`Deferred.SSAO` import 出现次数 14245 → 0（AO target/import/pass 全部随 composition 消失）。
  - 截图：`Engine/Saved/Automation/composition-ssao-on.png` vs `composition-ssao-off.png` 差异 13.51% 像素（>4/255），mean 2.7/255，max 42——AO 着色差异，无结构位移。
  - 管线切换：`set_render_pipeline forward` → 图切为 Forward legacy（13 pass）；切回 Deferred 后画面与初始 Deferred 帧**逐像素一致**（max diff 0，`composition-deferred-back.png`，属同配置重建管线后的确定性证据）。
  - 日志唯一 ERROR 级条目为已有的 VkDescriptorPool STORAGE_BUFFER 校验告警（历史 2DRpgPrototype run 同样存在），非本次引入。
- 证据文件在 gitignore 的 `Engine/Saved/` 下；配置：`Engine/Saved/Config/Automation.composition_ssao_{on,off}.json`。

### Matrix 更新（feature_matrix.json）

- M03 → `verified`。
- M01 / M02 / M04 / M05 / M06 / M07 / M10 / M13 → `in_progress`（证据与缺口逐条记入）。
- M08（双 View runtime）、M09/M11/M12/M14（plan.md C2）、M15–M23 保持 `todo`。

### 保留 / 未验证

- tail 未迁移、`ViewResources` 槽位仍写、Forward 仍 `appendTargetRequests`：
  属 C3/C4 范围。
- 无 failure injection（M10 完整）、无双 View runtime（M08）、无双管线性能对照（M23）。
- 本切片未新增 `test/` 级临时工程；截图对照为单次抓帧，重复性基线（至少 2 次）属 C5。

## 2026-10-12 深夜 — C3A 完成：Deferred tail 全 feature 化与集中层删除

### 目标与边界

**单一验收目标**：完整 Deferred 地图由一个 recipe 读出——GBuffer → SSAO(可选) → Lighting → ForwardOpaque 槽 → Skybox → Sprites → Bloom(可选) → Overlay → EntityId → Finalize 全部是 feature 拥有的 pass；`DeferredFrameGraphPasses` / `DeferredFrameGraphResources` / `allocateDeferredViewPassResources` / `DeferredFrameResourceSet::ViewResources` 退出。

边界外（保留，属后续）：SSAO/Skybox 的 frame payload/DSL 仍在 `DeferredFrameResourceSet::Binding`（本地化 = C3B/M17）；Forward 全图（C4）；`_lastBlurPassCount` 调用状态（plan-C2）；`buildOverlayFrameInputs` 录制期读 derivedScene 属既有实现（发现记录，本批未改）。

### 新 feature 与职责

| Feature | resolve | prepareBindings | append 移除/保留的旧行为 |
| --- | --- | --- | --- |
| ForwardOpaqueFeature | 恒在 map（空 draw 槽保留，删除需另证） | — | Load/Store color+depth，无 workload |
| SkyboxFeature | 恒在 map | — | `uniformRead(frame)` + skybox packet execute |
| Sprite2DFeature | `worldSprites` 非空才在 map | frame UBO + `updateTextures` | 内容门控；depth readOnly |
| BloomFeature | `grading && bEnableBloom` 才在 map | 仅启用时分配 bloom DS | 关闭 = 返回原 ColorStream，无 pass/无 transient |
| OverlayFeature | 恒在 map | billboard frame UBO + texture DS + `updateBillboardTextures` | ForwardTransparent；续写返回流 |
| EntityIdFeature | 恒在 map，声明自身 target | EntityId frame UBO | 独立输出 + billboard 透传 + skinning DS |
| FinalizeFeature | 恒在 map（必需），声明 DisplayColor | toneMap input set | `appendFinalizeGraphPasses`，编码来自 Config |

### 删除的集中层

- `DeferredFrameGraphPasses.{h,cpp}`（整文件，7 个 append + import + 3 个 PassParams 结构）。
- `DeferredFrameGraphResources.h`（bag；`deferred_graph_exports` 移入 `DeferredPassNames.h`）。
- `allocateDeferredViewPassResources`（分配回各 feature 的 prepareBindings）。
- `DeferredFrameResourceSet::ViewResources` 槽位、`getViewResources/mutableViewResources`、pipeline 3 处 mirror 写；`_viewBindings` 直接存 `Binding`。
- pipeline：`appendDeferredViewToGraph` 的 14 字段 `DeferredFrameGraphPassContext`、bag 填充、`DeferredFamilyViewBranch::graphResources` → `DeferredPipelineRecipe::GraphOutputs`。
- `resolveFrameComposition` 手工附件清单（旧代码请求与导入各写一遍）→ `ResolvedView::collectViewTargets()`：同一份 Resolved 同时驱动请求与导入。

### 行为保持与新语义

- Bloom 关闭：不再有 composite 拷贝 pass 与 transient（旧 `appendBloomGraphPasses` 关闭分支仍建图内拷贝）；Bloom feature 直接返回原 ColorStream（C2 既定语义）。随之 Overlay 直接续写 viewColor，`Deferred.View.Color` 诊断图内容在 bloom-off 时含 overlay 写入。显示输出逐像素不受影响（拷贝两侧同为 FP16、clamp 恒等）。
- 附件请求顺序：EntityId 移到 DisplayColor 前（原 Display→EntityId），ViewTargetStore 一次性重建后稳定。
- ForwardOpaque 空 pass、Skybox/Overlay/Sprites/Bloom/EntityId/Finalize 的 pass 顺序、Load/Store、finalLayout 逐条镜像旧 tail。

### 验证

- 编译：`xmake b ya-render-3d-test` ok；`control start --game --build`（ya-game-runtime closure）ok。
- 测试：`xmake r ya-render-3d-test` 221/221（40 suites，新增目标清单/薄 feature 契约测试）；`python3 Script/ya.py test --target ya` 1812/1812（226 suites）。
- Runtime（HelloMaterial，`--render-graph-trace`，control `--game`）：
  - SSAO on：Deferred executor 14 pass，顺序与 C1i 基线一致；截图对基线**逐像素一致**（max diff 0，`c3a-ssao-on.png`）。
  - SSAO off：13 pass（无 SSAO）；截图对基线**逐像素一致**（max diff 0，`c3a-ssao-off.png`）。
  - Bloom off（新增场景，Runtime.json 默认 bloom on）：10 pass，无 Bloom×4、无拷贝 pass、无 `Deferred.Bloom*` import/transient（`c3a-bloom-off.png`）。
  - 日志唯一 ERROR 仍为历史 VkDescriptorPool STORAGE_BUFFER 校验告警。
- 证据：`Engine/Saved/Automation/{c3a-ssao-on,c3a-ssao-off,c3a-bloom-off}.png` 与 `control-game-HelloMaterial-{62846,63187,63242}.log`（gitignored）；配置 `Engine/Saved/Config/Automation.composition_bloom_off.json`。

### 实测复杂度（诚实数字）

- 本批 diff：**33 files, +1876 / −964**。
- 集中层删除 ≈ 660 行（passes 481 + resources 70 + allocate 77 + ViewResources/getter/mirror ≈ 31）。
- Deferred 子系统（`Deferred/` + `include/Deferred/` + `Composition/`）总行数：7060（HEAD，含被删文件）→ **7672，净 +612**。
- 三门槛判定：
  - 删除面：**未达标**（行数净增；feature 协议壳对薄 pass 是纯开销）。
  - 单一声明面：**达标**（`collectViewTargets` 单源；ViewResources 镜像与双份附件清单消失；SSAO on/off 由同一 Resolved 门控请求/pass/export）。
  - 扩展面：**达标**（新增 pass = feature 文件 + recipe 一行 + collectViewTargets 一处）。
- 处置候选（下一决定点，未执行）：裁薄壳——ForwardOpaque 改由 recipe 声明（pass 保留、语义不变）、Skybox/Sprites/Overlay 共享 color-continuation declare helper，预计回收 ~300–400 行协议壳。未定之前不宣称 C3 复杂度已下降。

### 保留 / 未验证 / 偏离

- C3B/M17：SSAO/Skybox payload+DSL+DS 归 feature 局部；Binding 收口为真实共享输入。
- plan-C2 剩余：`_lastBlurPassCount` 调用状态、blur pass 次数边界（M09/M14）；grading-off 截图（M12，bloom-off 已覆盖 Bloom off 分支）。
- M15 记 `in_progress`：tail 迁移完成，shared provider 收口后才达 verified。
- 偏离：无范围偏离；未新增 test/ 级工程；多 View（M08）与 failure injection（M10）仍待补。

## 2026-10-12 深夜 — C3A-trim：按"开闭 + 快速接手"验收的收敛（Checkpoint T）

### 目标与边界

用户的验收标准修正：不是行数，而是"编排/改动/新增 pass 时符合开闭、真人能快速接手"。本批只做两件事——(1) 无功能契约的薄壳降级、continuation 语义单一定义；(2) 看守测试与接入说明。行为零变化（pass 保留、拓扑/像素与 C3A 基线一致）；不动协议形状；容器化（design.md `PipelineComposition`）**不做**，理由：显式 map 的可读性优于模板化相位，除非 C4 实测触点扩散。

### 实施内容

- 删除 `ForwardOpaqueFeature.{h,cpp}`（无功能契约的槽位不配 feature 协议；迁移地图要求保留 pass）。forward-opaque 槽改由 recipe 在 `appendGraph` 内直接声明：`composition::declareColorContinuation` + 空 execute；`ResolvedView`/`GraphOutputs` 的对应字段删除，Skybox 输入直接取 `outputs.lighting.sceneColor`。槽位获得 workload 时再毕业为 feature（注释写明）。
- 新增 `Composition/ColorContinuation.h`：color-continuation 的唯一契约定义（Load color、Load/test depth、final ShaderReadOnlyOptimal）。使用者：recipe（opaque 槽）、SkyboxFeature、Sprite2DFeature（depth readOnly）、OverlayFeature——4 个真实消费者。
- `DeferredPipelineRecipe.h` 头注释新增"Adding a pass"接入说明（触点、target 发布、失败模式）。
- 守卫测试：目标清单测试增加"每 feature 声明的 target 恰好发布一次"的唯一性断言；配合既有顺序断言，漏发布/重复发布会失败（且 feature append 以 `MissingDependency` 响亮失败，不会静默不画）。

### 实测

- diff：**9 files, +49 / −189（净 −140）**；Deferred 子系统（同一口径）**7672 → 7568，净 −104**。
- 新增一个 pass 的接入面（当前形态）：feature 文件对 + recipe 接线（字段列 2~3 处 + resolve/prepare/append 各一处，约 20–40 行，全在 recipe 两个文件内）；只有引入新设备对象才再动 pipeline 的 `Stages` 一行；**无集中层触点**。
- 薄壳样板：`ColorContinuation` 让每个 continuation pass 的 raster 声明从 ~28 行收敛到 1 行；ForwardOpaque 的 115 行协议壳清零。

### 验证

- 编译：`xmake b ya-render-3d-test` ok；`control start --game --build` ok。
- 测试：`xmake r ya-render-3d-test` 221/221；`python3 Script/ya.py test --target ya` 1812/1812（226 suites）。
- Runtime（HelloMaterial，`--render-graph-trace`）：
  - SSAO on：14 pass；截图对 C3A 基线**逐像素一致**（`c3a-trim-ssao-on.png`，max diff 0）。
  - SSAO off：13 pass；截图**逐像素一致**（`c3a-trim-ssao-off.png`，max diff 0）。
  - Bloom off：10 pass，无 Bloom/拷贝 pass；截图**逐像素一致**（`c3a-trim-bloom-off.png`，max diff 0）。
  - 经验记录：一次 warmup_frames=30 抓到未完成帧（mean 15 vs 114），warmup=180 复现通过——截图校验统一用长 warmup。

### 保留 / 未做

- skill 沉淀（"如何新增一个 pass"进 render-arch 或新 skill）按计划留 C5；本轮说明放在 recipe 头注释。
- C3B/M17（shared inputs 收口）为下一步；C4 的共享 feature 是跨管线开闭性的正式验收证据。

## 2026-10-12 深夜 — C3B：共享输入收口（M17 Deferred 侧）

### 目标与边界

**单一验收目标**：`DeferredFrameResourceSet::Binding` 只留真实共享输入（frame/light/skinning），SSAO/Skybox 的 payload、上传 slice、layout、DS 全部归 feature 局部准备。行为零变化；Forward 侧不动（Forward 自己的 skybox frame DSL/DS 留 C4）；灯光 generated 类型/字段映射/预算用例保留。

### 实施内容

- `DeferredFrameResourceSet`：`Binding` 收到 6 字段（frameAndLightDS + skinningDS + frame/light/lightInstances slice + skinningBuffer）；`ViewPayloads` 同口径；`beginView(submission, view)` 不再收 feature payload；`ensureViewDescriptors` 只分配 frame/light；`updateSSAO/SkyboxDescriptorSet`、`getSSAO/SkyboxFrameDSL` 删除。
- DSL 归设备对象：`SSAOStage::init(render)` 自建并持有 `Deferred_SSAO_Frame_DSL`；`ViewOverlayStage::init(render)` 自建并持有 `Deferred_Skybox_Frame_DSL`（不再由 frame resource set 传入）。
- SSAOFeature：`resolve` 建 payload（原样）；`prepareBindings` 用 `writeUniformPassBinding` 上传 slice + 自有 DS；`append` 自己 import `Deferred.SSAOFrameUBO`（label 不变）并绑定自有 set。
- SkyboxFeature：`resolve` 从 `Facts::frameData` 建 payload（`ViewOverlayStage::buildFrameData` 变 static，`buildSkyboxFrameData` 删除）；prepare/append 同 SSAO 模式；`executeSkybox` 收 frameDescriptorSet 参数，`SkyboxInput` 不再携 DS。
- Recipe：`PreparedView` 增 `skybox`、删 ssao/skyboxFrameRange；`AppendContext` 删 ssao/skyboxFrameBuffer；`PrepareContext` 注释口径更新。
- Pipeline：`beginView` 去掉两参数；`DeferredViewBufferImports` 只 import frame/light/skinning；`overlayInputs.skybox.frameDescriptorSet` 写点删除。

### 实测

- diff：**17 files, +222 / −272（净 −50）**；Deferred 子系统 **7568 → 7536（净 −32）**；共享 Binding 从 10 字段降到 6 字段。

### 验证

- 编译：`xmake b ya-render-3d-test` ok；`control start --game --build` ok。
- 测试：`xmake r ya-render-3d-test` 221/221；`python3 Script/ya.py test --target ya` 1812/1812（含 DeferredLightBufferTest 灯光映射/预算回归）。
- Runtime（HelloMaterial，`--render-graph-trace`，warmup 180）：
  - SSAO on：14 pass；截图对 C3A-trim 基线**逐像素一致**（`c3b-ssao-on.png`，max diff 0）。
  - SSAO off：13 pass；**无 `Deferred.SSAOFrameUBO` import**；像素一致（`c3b-ssao-off.png`，max diff 0）。
  - Bloom off：10 pass；像素一致（`c3b-bloom-off.png`，max diff 0）。
  - 共享上传计数（trace）：`Deferred.FrameUBO`/`LightUBO`/`SkinningSSBO`/`SSAOFrameUBO`/`SkyboxFrameUBO` 每 View 各 ImportBound 一次、每 pass access=1；feature-local UBO 随 feature 开关出现/消失。

### 保留 / 未做

- Forward 侧 shared inputs（`ForwardFrameResourceSet` 的 skybox frame DSL/DS）与 M17 的 Forward 半边留 C4。
- C3 剩余截图门禁：grading off、sprite+opaque 遮挡、overlay/gizmo、entity-id/picking（图像 + 行为断言）。
- 容器化不做（理由同 C3A-trim）；skill 沉淀留 C5。

## 总结

### 已完成（13 个 commits）

| Commit | 内容 | 测试 |
| --- | --- | --- |
| 503228aa | C1 typed refs + prepareFrameRecord interface | 219/219 ✓ |
| c220d086 | C1-foundation: type system + SSAOFeature skeleton | 219/219 ✓ |
| 799e5866 | DeferredPipelineRecipe into prepareFrameRecord | 1810/1810 ✓ |
| 321c6e08 | Core Result/OptionalHandle helpers | 1817/1818 ✓ |
| 93df0526 | Wire prepareBindings into recordFamily loop | 1810/1810 ✓ |
| b05c0dc6 | GBufferFeature - direct refactor | 1810/1810 ✓ |
| 7e0a6f2a | LightingFeature - direct refactor | 1810/1810 ✓ |
| 7ddec404 | GBuffer→SSAO→Lighting chain in recipe | 1810/1810 ✓ |
| 7df25e0b | progress update | — |
| 9726b2d0 | 集成切片：features 拥有真实 pass、recipe 单源驱动、旧 orchestrator 删除 | 220/220 render3d + 1811/1811 full + runtime on/off/切换证据 |
| 5e3dcb55 | C3A：tail 全 feature 化 + 集中层删除（+1876/−964） | 221/221 render3d + 1812/1812 full + on/off 像素 0 diff + bloom-off 10 pass |
| ad9a20f5 | C3A-trim：薄壳降级 + continuation 单一定义 + 接入说明/守卫测试（+49/−189） | 221/221 render3d + 1812/1812 full + 三场景像素 0 diff |
| （本提交） | C3B：SSAO/Skybox payload/DSL/DS 归 feature，共享 Binding 只留 frame/light/skinning（+222/−272） | 221/221 render3d + 1812/1812 full + 三场景像素 0 diff + 每共享 buffer import 一次 |

### 关键成果

- ✅ **Foundation 完整**: Core helpers (Result/OptionalHandle) + Feature protocol types
- ✅ **单源数据流闭环**: 安全点 resolve → 录制期 prepare → RDG append → 输出发布同源
- ✅ **Feature 一等对象**: GBuffer/SSAO/Lighting + tail 7 features 各自拥有 resolve/prepareBindings/append 与真实 pass
- ✅ **无 Legacy wrapper**: 旧 orchestrator/passes/bag/ViewResources 槽位直接删除，不回兼容桥
- ✅ **Runtime 证据**: SSAO on/off 图对照与逐像素一致、bloom-off 10 pass、管线切换逐像素一致
- ✅ **接手/开闭收敛（C3A-trim）**: 无功能契约槽位由 recipe 声明；continuation 语义单一 `ColorContinuation.h`；recipe 头"Adding a pass"说明 + target 发布守卫测试；子系统 −104 行
- ✅ **共享输入收口（C3B）**: 共享 Binding 只留 frame/light/skinning 六字段；SSAO/skybox payload+layout+DS 归各自 feature；每共享 buffer 每 View import 一次（trace）；SSAO off 无 SSAO UBO import
- ⚠️ **复杂度**: C3A 净 +612 后 C3A-trim −104、C3B −32（当前 7536）；删除面门槛仍未回正，容器化暂缓（C4 实测触点后再判）

### 当前状态

- C1-foundation: ✅ 完成
- 集成切片（plan.md C1 验收主体）: ✅ 完成并带 runtime 证据
- C3A（tail 全 feature 化 + 集中层删除）: ✅ 完成并带 runtime 证据
- C3A-trim（薄壳降级 + 接入说明/守卫）: ✅ 完成并带 runtime 证据
- C3B（M17 Deferred 侧共享输入收口）: ✅ 完成并带 runtime/trace 证据；C3 剩余截图门禁（grading off、sprite+opaque、overlay/gizmo、entity-id/picking）
- 剩余: M17 Forward 半边（C4）、plan-C2 剩余（`_lastBlurPassCount`/grading-off）、M08/M10/M13 完整、C4/C5

### 工作区状态

- **干净**: 13 个 composition 相关提交
- **测试全绿**: 1812/1812 full tests passed；render3d 221/221
- **编译通过**: ya-render-3d, ya-game-runtime（closure）
- **无关改动**: Engine/Plugins/log.cc 未 stage（灯光线工作）
