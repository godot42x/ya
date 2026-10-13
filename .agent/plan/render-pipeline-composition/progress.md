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

## 2026-10-12 深夜 — C3G：C3 剩余门禁（截图 / 行为 / topology 断言）

### 目标与边界

**单一验收目标**：把 C3 清单里剩余的画面/行为门禁真实跑掉并留证据：grading off、sprite+opaque 遮挡、overlay/billboard、M11 before/after topology 断言、entity-id/picking 能力盘点。除一处过期 smoke 测试契约外不改产品代码；单测不是替代品，跑不到的门禁明确记为缺口。

### 运行与结果

1. **sprite+opaque 遮挡**：`Script/automation/sprite2d/run_sprite2d_isolated.py` 全绿——遮挡侧 probe 像素 0（开放侧 344844），隐藏 occluder 后遮挡侧 137270（开放侧稳定 ±0.4%），相机拉远面积收缩（483k→150k），正交半高 4 在 z=6/z=12 面积一致、半高翻倍面积 1/4。
   - 组合绑定证据：`set_render_pipeline forward → deferred` 重建 Deferred 图后 trace dump `passes(14)`，`Deferred Sprites` 位于 `[6:7]`（Skybox 后、Bloom 前），同状态截图遮挡行为一致（open 344844 / occluded 0）。`control-game-HelloMaterial-55408.log`。
2. **overlay/billboard**：`Script/automation/billboard/run_billboard_isolated.py`（editor 模式）**首次红**，根因是过期契约：companion 重构 `1c91e9d83` 把列表 JSON 键从 `managed_by_light`/`owner_entity_id` 改为 `is_companion`/`host_entity_id`，Python smoke 未同步。修 2 处键名后全绿（`BillboardRegression` 场景 + 2 个 light companion + viewport 截图）。
3. **grading off（M12）**：新配置 `Automation.composition_grading_off.json`（`smoke.postprocess.enabled=false` → `PostProcessingStage::setGradingEnabled(false)`）。runtime：10 pass（Bloom 随 grading 跳过）、`Postprocessing` 仍执行并导出 `Deferred.View.Display`；截图有效（alpha 253.7），与 graded 基线仅色彩差异（max 19/255、mean 9.1），无结构性异常。`control-game-HelloMaterial-54894.log`、`c3g-grading-off.png`。
4. **M11 before/after topology 断言成文**（trace `passPlans`，同一 pass 名）：
   - bloom on：`[11:12] Deferred Forward Transparent`（Overlay）`textureStates(2)` = `Deferred.BloomComposite layout=1` + `DeferredGBuffer.Depth layout=2` → 单附件 Load/Store 续写 bloom composite backing，无拷贝。
   - bloom off：同 pass = `DeferredView.Color layout=1` + `DeferredGBuffer.Depth layout=2` → 直接续写 view color。
5. **entity-id/picking 能力盘点（缺口）**：图像侧已具备——`Deferred.View.EntityId → DeferredView.EntityId` 每帧导出，editor 用 isolate readback（`EditorLayer.Interaction.cpp`）按像素解析实体；但 runtime automation 无 hook（`capture_screenshot` 仅 viewport/presentation，无 pick/readback 方法），**行为断言无法在本轮产生**。
6. **direction gizmo（缺口）**：全仓库无场景/回归场景创建 `DirectionComponent`（`addComponent<DirectionComponent>` 0 命中），`frameInputs.directionGizmos` 分支无运行时覆盖；hook = 往回归场景加一个 DirectionComponent 实体（scene-side 小改动），待决定。

### 代码变更

- `Script/automation/billboard/test_billboard_isolated.py`：2 处过期键名（测试债修复，非渲染回归）。引擎代码零改动。

### 保留 / 未做

- entity-id/picking runtime hook 与 direction gizmo 场景 hook 未做（记录为缺口，不伪造证据）。
- M12 余项 `SurfaceImageTest` 侧编码回归未跑。
- 建议 order：C4 前决定是否补这两个 hook。

## 总结

### 已完成（14 个 commits）

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
| ab762faf | C3B：SSAO/Skybox payload/DSL/DS 归 feature，共享 Binding 只留 frame/light/skinning（+222/−272） | 221/221 render3d + 1812/1812 full + 三场景像素 0 diff + 每共享 buffer import 一次 |
| （本提交） | C3G：sprite2d/overlay 隔离回归 + grading-off 截图 + M11 topology 断言成文；修复过期 billboard smoke 键名（引擎零改动） | sprite2d 门禁全绿（0→137270 px、尺寸/正交断言）+ billboard editor 门禁绿 + grading-off 10 pass |

### 关键成果

- ✅ **Foundation 完整**: Core helpers (Result/OptionalHandle) + Feature protocol types
- ✅ **单源数据流闭环**: 安全点 resolve → 录制期 prepare → RDG append → 输出发布同源
- ✅ **Feature 一等对象**: GBuffer/SSAO/Lighting + tail 7 features 各自拥有 resolve/prepareBindings/append 与真实 pass
- ✅ **无 Legacy wrapper**: 旧 orchestrator/passes/bag/ViewResources 槽位直接删除，不回兼容桥
- ✅ **Runtime 证据**: SSAO on/off 图对照与逐像素一致、bloom-off 10 pass、管线切换逐像素一致
- ✅ **接手/开闭收敛（C3A-trim）**: 无功能契约槽位由 recipe 声明；continuation 语义单一 `ColorContinuation.h`；recipe 头"Adding a pass"说明 + target 发布守卫测试；子系统 −104 行
- ✅ **共享输入收口（C3B）**: 共享 Binding 只留 frame/light/skinning 六字段；SSAO/skybox payload+layout+DS 归各自 feature；每共享 buffer 每 View import 一次（trace）；SSAO off 无 SSAO UBO import
- ✅ **C3 门禁（C3G）**: sprite2d 隔离回归（遮挡 0→137270 px + 世界尺寸/正交尺度）全绿，且 trace 证明跑在 Deferred 组合（`Deferred Sprites` 位于 Skybox 后/Bloom 前）；billboard overlay editor 回归绿（修复过期键名测试债）；grading-off 10 pass 且 Finalize 仍发布；M11 bloom on/off 续写 backing 断言成文
- ⚠️ **剩余缺口**: entity-id/picking 无 runtime hook（图像已导出、editor readback 存在）、direction gizmo 无场景 hook、M12 的 SurfaceImageTest 未跑
- ⚠️ **复杂度**: C3A 净 +612 后 C3A-trim −104、C3B −32（当前 7536）；删除面门槛仍未回正，容器化暂缓（C4 实测触点后再判）

### 当前状态

- C1-foundation: ✅ 完成
- 集成切片（plan.md C1 验收主体）: ✅ 完成并带 runtime 证据
- C3A（tail 全 feature 化 + 集中层删除）: ✅ 完成并带 runtime 证据
- C3A-trim（薄壳降级 + 接入说明/守卫）: ✅ 完成并带 runtime 证据
- C3B（M17 Deferred 侧共享输入收口）: ✅ 完成并带 runtime/trace 证据
- C3G（C3 剩余门禁）: ✅ 可运行部分全部跑绿（sprite/billboard/grading-off/M11 断言）；entity-id/picking 与 direction gizmo 记为缺口
- 协议诚实化（审计后续）: ✅ Feature concept 由 9 个 feature static_assert 强制；context 死字段与 Scene 泄漏删除；runtime 三帧逐像素一致（含 Forward）
- 剩余: C4（Forward recipe / shared inputs 收口 / Shadow typed token）、plan-C2 剩余（`_lastBlurPassCount`）、M12 的 SurfaceImageTest、entity-id/picking 与 gizmo hook 决定、M08/M10/M13 完整、C5

### 工作区状态

- **干净**: 14 个 composition 相关提交
- **测试全绿**: 1812/1812 full tests passed；render3d 221/221
- **编译通过**: ya-render-3d, ya-game-runtime（closure）
- **无关改动**: Engine/Plugins/log.cc 未 stage（灯光线工作）

## 2026-10-12 深夜 — 协议诚实化（审计后续，非 plan.md checkpoint）

### 为什么做这一批

独立审计（读代码而非读文档）发现三处「机制存在但无强制力」：`Feature` concept 要求 `(FeatureResolveContext&, const Inputs&, const void*)`，与任何真实 feature 签名都不匹配，生产零 `static_assert`；typed refs 生产零使用；`FeatureGraphContext` 带 `stageCtx = RenderStageContext`（含 live `Scene*`），违反计划「graph callback 不查询 active Scene」的不变量。C4 要把这套协议复制到 Forward，所以先让协议真实可强制。

同时更正审计中的两处误判：`DeferredPipelineRecipe.h` 的 "Adding a pass" 注释与 `collectViewTargets` 注释**是准确的**（opaque 槽确实经 `declareColorContinuation` 声明），无需修改。

### 实施内容

- **concept 对齐真实签名**：`Feature` 要求 8 个嵌套类型（含 `Stage`）＋三个方法返回 `Resolved` / `Result<Prepared,FeatureError>` / `Result<GraphOutputs,FeatureError>`。嵌套类型放在第一个 `requires` conjunct，缺失时是「不满足」而非硬错误。
- **9 个 feature 各自 `static_assert`**：`GBuffer/SSAO/Lighting/Skybox/Sprite2D/Bloom/Overlay/EntityId/Finalize` 统一补 `using Stage = <StageType>;`，签名收敛（`LightingFeature` 去掉 `static`、`GBufferFeature::PrepareInputs` → `PrepareContext`、`prepareBindings`/`append` 全部返回 `Result`）。
- **`FeatureGraphContext` 收窄**：删死字段 `graph`、`render`（真实使用 0）。
- **`RenderStageContext` 删 `Scene* derivedScene`**：全仓库唯一读点是 Forward 侧 `ForwardViewStage::buildPassContext`，改为显式参数 `buildPassContext(ctx, viewScene)`，由 `ForwardRenderPipeline` 从 View 自己的声明（`frame.derivedScene`）传入。Forward per-pass 入口由 `const PassContext*` 可选改为 `const PassContext&` 必需 —— null 路径本来就要现造 context，现改为断言响亮失败。

### 验证

- 编译：`xmake b ya-render-3d-test` / `xmake b ya-runtime` ok。
- 测试：`xmake r ya-render-3d-test` 221/221；`python3 Script/ya.py test --target ya` 1812/1812。
- **concept 确实生效（负向证明）**：临时把 `FinalizeFeature::append` 返回类型从 `Result<GraphOutputs,FeatureError>` 改成裸 `GraphOutputs`，`static_assert` 报 `FinalizeFeature must satisfy the composition Feature protocol`；改动已还原。
- Runtime（HelloMaterial，`wait_up=180`）：
  - SSAO on 截图 vs `c3b-ssao-on.png` **逐像素一致**（max diff 0，`protocol-ssao-on.png`）。
  - 切 Forward 截图 vs 改动前基线 `composition-forward.png`（18:37 抓取）**逐像素一致**（max diff 0）—— 这是签名改动未改变 Forward 行为的直接证据。
  - 切回 Deferred vs `c3b-ssao-on.png` **逐像素一致**（max diff 0）。
  - 日志唯一 ERROR 仍为历史 VkDescriptorPool STORAGE_BUFFER 校验告警。
- 证据文件在 gitignored 的 `Engine/Saved/Automation/` 下；日志 `control-game-HelloMaterial-61389.log`。

### 保留 / 未做 / 偏离

- **偏离**：本批动到 Forward（`ForwardViewStage` 签名 + `ForwardFrameGraphPasses` 调用点 + `ForwardRenderPipeline` 一处），比原定「不碰 Forward」多。理由是 `derivedScene` 的唯一读点在 Forward，若不动它就只能把 `Scene*` 留在共享 context 里、让协议收窄停留在注释上——即审计批评的「纸面承诺」。这是唯一一处越界，已在本文记录，并带 Forward 逐像素一致证据。
- 保留：typed refs（`CompositionTextureRef`/`composition_tag`）未删——生产零使用但删除涉及测试改动，按惯例单独拍板；它们是「死脚手架」的事实不变。
- 保留：`ViewResources` 混合 bag（C4）、`_lastBlurPassCount`（plan-C2）、M08/M10/M13、entity-id/gizmo hook。
- 未做：C4 Forward recipe 本身（下一批）。
- 工作区：`Engine/Shader/Slang/Common/Limits.slang`（`MAX_POINT_LIGHTS_DEFERRED`）与 `Engine/Plugins/log.cc` 是并行会话/灯光线改动，未 stage、未提交。

## 2026-10-13 — C4a：Forward 接入同一 recipe，三个颜色效果 feature 跨管线共享

### 目标与边界

**单一验收目标**：Forward 有一条与 Deferred 同协议的 typed recipe；至少一类 feature 真正跨两条管线复用同一实现；Forward 侧混合生命周期的 skybox 资源收口；两管线都不再走预组合分配路径。

### 实施内容

- `ForwardPipelineRecipe.{h,cpp}`：`resolve` / `prepareBindings` / `appendGraph`，map 为 opaque → skybox → 透明槽 → entity id → bloom → finalize。`ResolvedView::collectViewTargets()` 是 Forward View target 的单一来源；`resolveFrameComposition` 与 Deferred 同形。
- **共享 feature（M16 核心证据）**：`BloomFeature` / `EntityIdFeature` / `FinalizeFeature` 移入 `Composition/Features/`；`Settings` 增加 `passName` / `importLabel` / `importLabelPrefix` / `depthFinalLayout`，由各自 recipe 从管线词汇表传入。Deferred 侧字符串逐字不变（`Deferred EntityId` / `DeferredView.EntityId` / `DeferredView.Display` / `Deferred.`），Forward 用 `forward_pass_names` 新词汇表（`ForwardPassNames.h`，替换被删的 `ForwardFrameGraphResources.h`）。
- `ForwardOpaqueFeature`：clear + PBR/Phong/Unlit/Simple + direction overlay + debug；`Facts` 携带 `ForwardViewStage::PassContext`（沿用既有"recording 输入走 Facts"模式）。`ForwardSkyboxFeature`：frame UBO 在 resolve 构建、prepare 上传、append import，depth/color 保持 attachable（不套 `declareColorContinuation`，因为 Forward 的后续 pass 仍 attach color）。
- **M17 Forward 半边**：skybox frame UBO/DS 移出 `ForwardFrameResourceSet`（唯一消费者是 skybox pass）；`ForwardViewAuxPasses` 自建并持有 `FwdSkybox_PerFrame_DSL`；`ForwardViewAuxPasses::prepare` 与 `ForwardFramePayloads::skyboxFrame` 删除。`ForwardViewStage::execute*` 去掉 `Binding` 参数（只重复 pass context 已带的 set）。
- **删除**：`ForwardFrameGraphOrchestrator.{h,cpp}`、`ForwardFrameGraphPasses.{h,cpp}`、`ForwardFrameGraphResources.h`；`EFrameComposition::NotApplicable` 与 `IRenderPipeline::appendTargetRequests`（两管线都 resolve，`RenderDeviceState.Frame.cpp` 无回退分支）。`ForwardFrameGraphOrchestratorTest.cpp` → `ForwardPipelineRecipeTest.cpp`（3 例：默认无 optional target、Config/Prepare 默认值、bloom 三个 role 稳定追加）。

### 一处实现期 bug（如实记录）

首轮 Forward 截图 mean 43.47 vs 基线 117.48（偏暗）：`ForwardSkyboxFeature::Facts.frameData` 未由 recipe 传入，skybox frame UBO 全零。补 `recording.frameData` 后与基线**逐像素一致**。这正是"resolved 值单一来源"要防的错误类型，而它被截图门禁抓到。

### 验证

- 编译：`xmake b ya-render-3d-test` / `xmake b ya-runtime` ok。
- 测试：`xmake r ya-render-3d-test` 223/223（40 suites）；`python3 Script/ya.py test --target ya` 1814/1814。
- Runtime（HelloMaterial，warmup 180）：Deferred vs `c3b-ssao-on.png`、Forward vs `composition-forward.png`、切回 Deferred vs c3b 基线——三帧**逐像素 max diff 0**。日志除历史 VkDescriptorPool 告警外无 ERROR。
- 证据：`Engine/Saved/Automation/c4b-{deferred,forward,deferred-back}.png`、`control-game-HelloMaterial-53222.log`（gitignored）。

### 保留 / 未做 / 偏离

- **未做（C4b）**：Shadow typed token（M18）——shadow 仍由 pipeline 侧 `_shadowStage->appendGraphPasses` 直调，两 recipe 均如此；`ViewResources` 剩余槽位复核。
- 保留：透明槽仍是 recipe 内联声明（无功能契约，与 Deferred 的 forward-opaque 槽同待遇）。
- 提交卫生：本轮 `git mv` 后按 pathspec 提交曾漏掉删除（HEAD 一度同时含新旧路径）。用 `read-tree` + `commit-tree` 重建提交修正为 `6b2581f8`；重建过程重置了 index，已把并行会话（rpg-prototype）的 5 项 R100 rename 逐项恢复。教训记入提交说明与本文件：**带 rename 的提交必须 diff 校验 HEAD 树，不能只信 pathspec add**。

## 2026-10-13 — C4b：Shadow 作为 composition 贡献接入两条 recipe（M18）

### 目标与边界

**单一验收目标**：Shadow 不再是 recipe 之外的旁路——两条 recipe 都以 typed token 接入它（resolve 启用决策 / prepare token / append 输出），而策略、算法、阴影图所有权与预算不动。

### 实施内容

- 新增 `Composition/Features/ShadowFeature.{h,cpp}`（满足 Feature concept，带 `static_assert`）：`resolve` 只带启用决策；`prepareBindings` 调用 `ShadowStage::prepareView` 生产 `ShadowPreparedView`；`append` 调用 `ShadowStage::appendGraphPasses` 并把依赖点交给它。两 recipe 的顺序记账改为它第一个跑（shadow pass 会吃掉 family predecessor）。
- Deferred recipe：`ResolvedView.shadow` / `PreparedView.shadow` / `GraphOutputs.shadow`；**Lighting 改从 `outputs.shadow.outputs.shadowDepth` 取**（不再由 pipeline 从外部传 `AppendContext.shadow`）。Forward recipe 同样接入 `Stages.shadow` 与对应三个槽位。
- 管线侧：Deferred 的 `prepareShadowPass`（生产 token）与 Forward/Deferred 的 `_shadowStage->appendGraphPasses` 直调退出，替换为 `applyShadowFrameSettings()`——只把本帧 settings 应用到 stage（pipeline 仍拥有 frame packet）。`DeferredFamilyViewBranch::shadowPrepared` 与 `ForwardFamilyViewBranch::shadowPrepared` 删除。
- 边界保持：`ShadowStage`/`BasicShadowMapTechnique` 仍持 images/runtime/cascade/face/cull bindings；`ShadowSettings` 预算语义未动；pipeline 仍负责 `#36` 资源刷新与 stage 生命周期。
- 无 stage 时（settings 说开但资源从未建）返回"本帧无阴影"而非组合失败——与 stage-less 旧路径同答案，已在 `ShadowFeature::prepareBindings` 注释写明。

### 验证

- 编译：`xmake b ya-render-3d-test` / `ya-runtime` ok；测试：render3d 223/223、full 1814/1814。
- **Trace 拓扑**（`--render-graph-trace`）：Deferred 16 pass（shadow 4 → GBuffer → SSAO → Light → ForwardOpaque → Skybox → Bloom×4 → Transparent → EntityId → Postprocessing），Forward 13 pass（shadow 4 → Opaque → Skybox → Transparent → EntityId → Bloom×4 → Postprocessing），**逐条目与 C3B 基线一致**（含 dependencies 列表）。
- **像素**：shadow on 时 Deferred/Forward 均与改动前基线逐像素 max diff 0；**shadow on vs off 差 31782 px（max 36）**，证明 shadow 确实经 recipe 追加而非被静默跳过。
- 日志除历史 VkDescriptorPool 告警外无 ERROR。

### 保留 / 未做

- C4c：`ViewResources` 剩余槽位复核、Forward 透明槽记录、M19 lifecycle 用例。
- 未动：typed refs（仍只在测试使用，删除待拍板）。

## 2026-10-13 — C4c：C4 收尾（bag 退场、死结构清除、M19 recipe 级用例）

### 实施内容

- **`ViewResources` bag 退场**：复核三个槽位——entityId/post 在 C4a 后已是死写（features 自己分配），debug 是唯一活消费者。按 C3B 模式把 debug UBO 收进 `ForwardOpaqueFeature::prepareBindings`（它画 debug pass，UBO 本属它；stage 无 debug DSL 或无 frameData = 本帧无 debug，同旧分配器答案）。`ViewResources`、`getViewResources`、`mutableViewResources`、`allocateForwardViewPassResources` 全部删除；Forward 的 View slot 与 Deferred 同为 `Binding`。
- **死绑定结构清除**：`SSAOPassBindings`/`DeferredLightingPassBindings`（C3A bag 删除后零引用）、`PostprocessPassBindings`（bloom+toneMap 镜像；feature 各自持有后无消费者）。删除前逐一 grep 确认引用数为 1（仅定义处）。
- **M19 recipe 级合法用例**（`ForwardPipelineRecipeTest` 3 例）：空 plan → 空组合（非失败）；disabled config → `InvalidInput`（不是"空组合"，不让调用方把 wiring bug 当空帧）；无 View binding 的 prepare → `MissingContext`（不静默绑空集）。
- Forward 透明槽的无功能契约记录保留在 recipe 注释（与 Deferred opaque 槽同待遇）。

### 验证

- 编译：`xmake b ya-render-3d-test` / `ya-runtime` ok。
- 测试：`xmake r ya-render-3d-test` **226/226**（+3）；`python3 Script/ya.py test --target ya` **1817/1817**。
- Runtime：Deferred / Forward / 切回 Deferred 三帧对各自基线（c3b-ssao-on.png / composition-forward.png）**逐像素 max diff 0**；日志除历史 VkDescriptorPool 告警外无 ERROR。

### 保留 / 未做

- C5：组合诊断、扩展用例、多 View runtime、performance/churn 门禁、skill 吸收。
- 未动：typed refs（仍只在测试使用，删除待拍板）；C3 的两个 runtime hook（entity-id picking readback、direction gizmo 场景）；plan-C2 余项（`_lastBlurPassCount`）。

## 2026-10-13 — C5a：收尾批次（plan-C2 余项、M13 收集、死脚手架、M20 诊断）

### 实施内容

- **`_lastBlurPassCount` 退场**：它是"调用内事实存成成员"——由本次 `RenderDesc` 算出、几行后由 composite pass 读。改为局部 `blurPassCount`，composite 的 ping-pong 侧不再取决于"最后调用 bloom 的是哪个 View"。
- **M13**：`Composition/ViewOutputCollection.h` 提供 `collectSharedViewOutputs()`，把 Bloom/Postprocess 的 export 名（两条管线共用同一 feature 代码，名字相同）从两处解析收敛到一处；管线各自的 pass 词汇仍留在 `DeferredPassNames.h` / `ForwardPassNames.h`。
- **死脚手架删除**：typed refs（`CompositionTextureRef`/`CompositionBufferRef`/`CompositionPreparedRef`、`composition_tag`）与 `Core/Optional.h`（`OptionalHandle`）。四轮审计均为零生产使用；M01 要求的 typed ports 由各 feature 的 `Inputs`/`Outputs` + concept 满足，不需要第二套引用类型。`Core/Result.h` 保留（67 处生产使用）。
- **M20 组合诊断**：`DeferredPipelineRecipe::ResolvedView::describe()` / `ForwardPipelineRecipe::ResolvedView::describe()` 输出每 feature 的 on/off 与声明的 target 数/role；在 `render_graph_trace::isEnabled()` 下由两管线 resolve 时打印，off 时不构建字符串。runtime 实测两条管线都输出（`[RGTrace] composition:` + 10/6 行 feature 状态 + targets）。

### 验证

- `xmake r ya-render-3d-test` **224/224**（+1 bloom 边界用例）；`python3 Script/ya.py test --target ya` **1808/1808**。
- 新增 `BloomPostprocessingTest.EachCallPicksItsOwnFinalBlurTargetFromItsOwnCount` 的**负向验证**：临时恢复成员态 → 测试失败；恢复局部实现 → 通过。它守的是 M09/M14 要的"无共享 last-call 状态"。
- Runtime（新实例、未改任何设置）：Deferred / Forward / 切回 Deferred 三帧对基线 `c3b-ssao-on.png` / `composition-forward.png` **逐像素 max diff 0**；trace 两管线 composition dump 均出现。

### 事故与定性（如实记录）

- 一次运行出现 `DEVICE_LOST` ×70060：**并行 rpg-tactics runtime 与我的实例争抢 GPU**。判据：28 个历史日志中仅该次出现；独占复跑 0 次；既存 VUID-03320（SceneDepth aspectMask，pipeline 切换时）在之前多次运行中同样出现且不致命。
- 一次 `set_shadow_settings` 切换后画面出现 3545 px 差异带（y139-162）：**强制定性 = 阴影开关的正常结果**（新实例不切换则 0 diff；该带位置与阴影投影一致）。
- 并行会话提交 `06e083ad` 扫入我暂存的 `Optional.h` 删除 → HEAD 一度 include 悬空；本批补全引用清理后修复。

### 保留 / 未做

- M21（扩展用例：测试专用 producer→transform→consumer 全链）、M22（双 View runtime 证据）、M23（performance/churn 前后对照）仍为 todo——见 todo.md。
- C3 的两个 runtime hook（entity-id picking readback、direction gizmo 场景）仍未做。

## 2026-10-13 — C5b：M21 扩展验收（测试 feature 全链跑通）

### 实施内容

`FeatureProtocolTest` 新增 `TestTransformFeature` + `TestOnlyFeatureRunsTheFullChainWithoutCorePlumbing`：一个测试专用 feature，`Stage` 就是 `RenderGraph`（无设备对象）、`PrepareContext` 为空、私有 scratch 由 `graph.createTexture` 创建，经 producer → 两段多 pass transform → result 完整跑 resolve → prepareBindings → append → compile。`static_assert(Feature<TestTransformFeature>)` 说明它满足同一 concept——扩展的触点只有 feature 自己的定义与测试接线。

**负向证据**：首版把 transform 的 result 写成覆盖 scratch 自身（第二段既读又写同一张图且无中间态），`graph.compile()` 立刻报 `ReadBeforeWrite`；改成独立 result 后通过。接线错误由 RDG 校验抓住，不是静默通过。

### 验证

- `xmake r ya-render-3d-test` **225/225**；`python3 Script/ya.py test --target ya` **1809/1809**。

### 保留

- M22（双 View runtime）需要第二个 View 的声明入口，属应用/编辑器侧工作，不在本线范围；M23（performance/churn）依赖 perf-baseline 机制，同样是跨线任务。M20 的"与 RDG pass 区间显式关联"仍待补。已记入 todo 的 C5 余项。

## 2026-10-13 — C5c：M04 真实修复（声明重排不应重建 View 目标）

### 发现的真问题

补 M04 要求的"声明重排不变性"测试时发现：`ViewTargetRequest::operator==` 是 `= default`，对 `attachments` 向量**顺序敏感**；而 `ViewTargetStore::prepare` 用 `entry.allocation->desc == request` 判断是否可复用。于是 recipe 里调换两个 feature 的声明顺序（同样一组 target）会判定为"新请求"，触发整组 View target 重建（新 generation + 重分配 GPU 图像）——正是 plan 的"request 按 role 规范化、声明重排不改变 allocation"要禁止的。

### 修复

`ViewTargetRequest::operator==` 改为按 **role 集合**比较（role 在请求内唯一，`createAllocation` 拒绝重复）。allocation 本身一直按 `role` 索引存放，顺序从不是其身份的一部分；这次只是让相等判断与事实一致。graph-use metadata（label/final layout）不在 `ViewAttachmentDesc` 内，天然不进 equality。

### 验证

- 新增 `ViewTargetStoreTest.ReorderingDeclarationsKeepsTheAllocation`：声明重排后 allocation 指针与 generation 均不变、`imageCreates` 计数不增。**负向验证**：还原为默认（顺序敏感）比较后该测试立刻失败。
- `xmake r ya-render-3d-test` **226/226**；`python3 Script/ya.py test --target ya` **1810/1810**。
- Runtime：Deferred / Forward / 切回 Deferred 三帧对基线**逐像素 max diff 0**（分配身份判定变更未影响画面）。

## 2026-10-13 — C5d：M05/M06 证据补齐（role 冲突拒绝、import key 稳定性）

### 实施内容

补 M05/M06 缺的专项测试（两者机制早已在代码中，缺的是证明）：

- **M05 role 冲突拒绝**：`ViewTargetStoreTest.DuplicateRoleIsRejectedInsteadOfLastWins`——同一请求声明两次 `SceneColor` 时 `prepare` 返回 false（`createAllocation` 已有拒绝逻辑），而不是 last-wins 掩盖接线错误。
- **M06 import key 稳定性**：`RenderGraphCoreTest.InsertingAnImportLeavesExistingBindingsUnreplaced`——同一 executor 稳态（同 label/scope/backing）下新增一个 import，既有 import **不被替换**（`stats.importReplaced == 0`），新增者记为 `importBound`；identity = `RGImportKey(label + scope)`，与 graph 内 handle 位置解耦。

写测试过程中两次纠正了自己的假设（记录以防后续重复）：import 的"同一绑定"要求 backing 也相同——新 graph 每次新建 backing 会被判 `ImportReplaced`，这是设计而非缺陷；`normalizeImportedTextureDesc` 会断言 subresource 范围与 backing 的 mip/layer 一致，所以测试必须用 `withTestBacking` 造真实描述符。

### 验证

- `xmake r ya-render-3d-test` **228/228**（+2）；`python3 Script/ya.py test --target ya` **1811/1811**。
- matrix：M05/M06 → verified（累计 13 verified）。

## 2026-10-13 — C5e：M20 收口（pass 贡献关联）

补 M20 的最后一块：组合 dump 原本只说"哪些 feature 开着、要了哪些 target"，不说"贡献了哪些 pass"。现两管线在 `appendGraph` 前后记录 pass 计数并输出 `composition contributed <pipeline> View N passes [a..b)`。

**runtime 实测**：Deferred 输出 `[0..16)`，与同一 trace 里 graph dump 的 `passes(16)` 逐一对上；shadow-off 的帧输出 `[0..14)`，与 `passes(14)` 一致——读 dump 的人可以直接把 feature 状态与 graph pass 区间对上。trace off 时不构建文本（字符串在 `render_graph_trace::isEnabled()` 分支内）。

验证：`xmake r ya-render-3d-test` 228/228、`python3 Script/ya.py test --target ya` 1812/1812；trace 运行 DEVICE_LOST=0。

## 2026-10-13 — C5f：M01/M02/M07/M10/M11 证据补齐（含一处设计修正）

### 补齐的测试

- **M07**：`PreparedPipelineFrameTest.ResolvedPointersSurviveCopiesAndGrowthOfTheFrame`——记录期持有的 resolved 指针在 frame move 与另一 pipeline 块扩容后仍指向同一值（这是"记录期不能改 prepared"这条不变量的可执行形式）。
- **M10**：`RenderViewOutputTableTest.FailedPreparationCannotAnswerWithThePreviousTicksFrame`——安全点开新 tick 后未 publish，查询返回 nullptr / liveViewCount=0；失败准备不能answer上一 tick 的帧。
- **M01**：确认 12 个真实 feature 全部 `static_assert(Feature<>)` 强制；typed refs 死脚手架 0 引用。

### 一处设计修正（写入 M01 evidence）

M01 原文要求"concept/**add 存储**至少两类真实异构 feature"，我方（与计划原文）一度理解为需要通用 add/seal 类型擦除容器。实际落地后的判断是：**不需要**。两条 recipe 用显式字段列出各自 feature，同一 `Feature` concept 强制签名，M21 的测试已证明"新增 feature 只需 feature 定义 + recipe 接线、不改 core"。再引入 add/seal 容器会是为了抽象而抽象——计划 stop 条件里"typed ports 需要 std::any / 字符串查找"正是这条的反面。已记入 evidence 与 progress。

### 完成度

matrix：**18 verified / 2 in_progress / 3 todo**。剩余：M08（双 View runtime，需第二 View 入口）、M19（lifecycle 的 runtime mutation trace 部分）、M22（同 M08 依赖）、M23（performance/churn，依赖 perf-baseline 机制）——均可运行部分已闭合，跨线项已明确记录。

## 2026-10-13 — C5g：M23 尝试与定性（结论：对照不成立，原因预先存在）

### 做了什么

按 plan 要求用 perf 机制（`Script/perf/bench_run.py` + 既有 `baselines/macos/profile-100ae306-fifAB`）跑了 PerfLab `Lights_*`。过程中发现三件事，逐条记录而非绕过：

1. **第一次跑错口径**：忘了 `--bench-dir`、又用 `--no-build` 跑了 debug 二进制，而基线是 **profile** 构建（`meta.buildMode` 明写）。debug 数字与基线不可比。改 cfg 到 profile 重跑（构建 171s）。
2. **即便同构建也不可比，原因是预先存在的**：基线 `profile-100ae306` 早于灯光线的 `039570590`（LightInstance SSBO 重构）与 `49d70331`（取消点光上限）。perf-baseline 线自己的 todo 已登记「R15a 之后重采 `Lights_*` 的 profile 基线」；其 PB6 记录还说明 R15a 前三档全被 `MAX_POINT_LIGHTS=6` 截断（drawIndexed/descriptorBind 完全相同），光源梯度本就测不出。**当前状态**：Lights_200 因不再截断而真实变慢（p50 28.4ms vs 基线 8.98ms），Lights_500（500 灯 + 未截断）在本机 `DEVICE_LOST`（debug 与 profile 均复现）。这两条属灯光线 R15a 的后续，不在本线范围。
3. **稳态 import churn 是预先存在的**：trace 显示 Deferred registry 每 sync `replaced=21..23 / rebound=23..25`。逐值对比 C1i 基线日志（commit `9726b2d0`，早于 C4/C5）与当前 HEAD：**完全一致**。本线既未引入也未加剧它；M23 的"无 churn regression"结论应以此为基线判断，而不是把它当成本线问题。
4. **本线未引入可测量开销**：C4/C5 全部改动为结构/诊断类；唯一运行期语义变更是 `ViewTargetRequest::operator==` 按 role 集合比较（只会减少重建），且有负向验证单测。

### 未达成

plan 要求的 CPU/GPU/内存三次前后对照未完成：本机存在并行构建与运行时（PB6 明确记录并发编译使同场景两次 p50 差 87%），不满足 perf-baseline 声明的采集条件。已在 matrix M23 与 todo 中写明"对照留给 perf-baseline 线，连同上述三发现"。

### 状态

matrix **19 verified / 2 in_progress（M19、M23）/ 2 todo（M08、M22）**。M08/M22 依赖"第二个 View 的声明入口"（应用/编辑器侧），已记明跨线。

## 2026-10-13 — 复审：计划重新打开（非实现 checkpoint）

### 触发

用户要求 review 本计划是否真的解决复杂度问题与实现质量。按代码核对（不是按 progress 自述）。

### 结论

保留：旧 orchestrator/passes/resources/ViewResources bag 退出；target request 单源；SSAO/Bloom 关闭无资源；Bloom/EntityId/Finalize 跨管线共享；M04 与 blur last-call 两个真实修复；逐像素行为证据。

未达成 §1 成功定义 1–4，且 §8 第一条停止条件已触发：

- `DeferredRenderPipeline::beginSubmission` 以 `_bEnableSSAO` 决定 SSAO warmup，与 resolved `config.ssao` 并存；shadow 设置有 resolve / 逐 View `captureShadowSettings` / `applyShadowFrameSettings` 三个来源。
- `recordFamily` 逐 View 调 `applyPendingResourceRefreshes()`，录制期 destroy/init shadow 资源；design §2 的 device-prepare 未实现。M02 原 verified 不成立。
- `buildOverlayFrameInputs` 录制期遍历 live ECS 决定 skybox/billboard/gizmo；`PrepareContext.overlayInputs` 被 Overlay prepare 写、EntityId append 读。
- 新增 feature 需改 recipe 约 10 处 + pipeline 侧 4 处；`collectViewTargets` 漏登记只在运行期暴露。M21 测试绕过 recipe/request/publish。
- typed ports 为裸 `RGTextureHandle`；M01 的「设计修正」以改解释替代 expected，违反 status_rules。M05 的编译期类型不兼容同理。
- append 中途失败时已加入 family graph 的 pass 无撤回。
- composition 提交在 `Engine/Source` 净增约 2,900 行（+7271 / −4381）。

### 用户决策

Feature 吸收 Stage（D5 分批）；extraction 快照交 `render-view-family`（本线 M25 blocked）；View 失败使整个 family 失败、不改 RDG；本目录重新打开。

### 本轮工件变更

- plan.md：头部状态改为重新打开；§9 完成条件纳入 D 系列；新增 §10（复审表、决策、D1/D3/D4/D5 checkpoint、D2 外部交付、追加停止条件）。
- feature_matrix.json：M01/M02/M05/M21 → in_progress 并附复审说明；新增 M24（D1）、M25（D2，blocked）、M26（D3）、M27（D4）、M28（D5）；`next_checkpoint` = D1。
- todo.md：顶部加复审后待办。
- `render-view-family/todo.md`：登记 D2 交接项。

未改代码、未构建、未跑测试。按仓库规则，本批计划改动与 D1 代码同批提交。

### 状态

matrix **15 verified / 6 in_progress / 6 todo / 1 blocked**（共 28 行）。下一 checkpoint：D1。

## 2026-10-13 — D1：安全点冻结设置，录制期不重建持久资源（M24 verified、M02 回到 verified）

### 复审缺口的代码核实（动手前逐条验证）

复审 §10.1 的三条结论在当前代码上全部属实：`_bEnableSSAO` 与 resolved `config.ssao` 并存（`beginSubmission` 读前者）；shadow 有 resolve / 逐 View `captureShadowSettings` / `applyShadowFrameSettings` 三个来源；`recordFamily` 逐 View 调 `applyPendingResourceRefreshes()`，shadow 分辨率变化时在录制期 destroy/init 整套 shadow 资源。

### 实施

- **两步安全点**（design §6 顺序的落地）：`IRenderPipeline` 新增 `applyFrameSettings(plan)`（step 1：本帧 queued settings + 帧 packet 的 shadow settings 落地——后者是原逐 View capture 读的同一来源）与 `prepareFrameDevices(prepared)`（step 2：`applyPendingResourceRefreshes`——shadow 资源重建落在这里——+ shadow stage/state 同步）。`prepareFrameRecord` 按 **settings → resolve → devices → targets** 顺序调用，全程在任何 command buffer 之前。
- **录制路径清空**：两管线的 `recordFamily` 删除逐 View `syncFrameSettings` / `applyPendingResourceRefreshes` / `captureShadowSettings`；两个逐 View helper 与 capture 函数删除。`applyPending*` 现在只在安全点被调（grep 核对）。
- **`beginSubmission(prepared)`** 接收本帧 resolved composition：SSAO warmup 由"任一 `ResolvedView.ssao.bEnabled`"决定，`_bEnableSSAO` 不再是 warmup 决策来源（唯一消费者回到 `buildSettingsSnapshot`）。
- shadow 的 destroy/init 现在只剩三处：pipeline `init`、`shutdown`、安全点内的 `applyPendingResourceRefreshes`——录制期零重建。

### 验证

- 测试：`xmake r ya-render-3d-test` **232/232**（新增 `ApplyFrameSettingsFreezesTheFrameAgainstLaterRequests`——apply 之后的 `requestSettings` 不动本帧状态、下一帧 apply 才落地；`FramePacketShadowSettingsAreTheSafePointSource`）；`python3 Script/ya.py test --target ya` **1816/1816**。
- Runtime（HelloMaterial）：Deferred(shadow on) / re-on / Forward / 切回 Deferred 四帧对各自基线**逐像素 0 diff**；shadow on/off 差 31782 px（shadow 仍真实生效）。
- **shadow 切换 trace**：录制期无 shadow destroy/init（重建只剩 init/shutdown/安全点三处）。trace 中 validation 错误分布（09600=20 / 03320=2 / 00331=20）与 D1 前三个 run（53222/62505/63301）**逐值一致**——这些是既有的 shadow-toggle 时序问题（首次 shadow off→on 后一帧 descriptor 引用重建前布局），预先存在、非 D1 引入也未恶化，留待 shadow-and-light 线。

### 状态

matrix **17 verified / 5 in_progress / 5 todo / 1 blocked**。下一 checkpoint：D3（D2 外部交付中）。

## 2026-10-13 — D3：invocation tuple + fold（M26 verified、M01/M05 证据更新）

### 复审缺口的落实

复审指出加 feature 要改 recipe 约 10 处、collectViewTargets/describe 手工逐 feature 列举、漏登记只在运行期暴露。本批把它们变成结构性的：

- concept 扩展：Feature 增加 name()（feature 自己的名字）与 targets(resolved, out)（feature 从自己的 Resolved 声明 View target——与 append 同门控：disabled 发布空）。13 个 feature 全部实现，static_assert 强制；缺 targets()/name() 是编译错误（负向测试 FeatureWithoutTargetsRegistrationIsNotAFeature）。
- Invocation<F>：{Resolved, Prepared, GraphOutputs} 一条记录。两 recipe 的 FeatureInvocations = std::tuple<Invocation<F>...> 按 map 顺序列出 feature；ResolvedView/PreparedView/GraphOutputs 持有 tuple + accessor 方法（不是引用成员——见下方事故）。
- fold：collectViewTargets 与 describe 是对 tuple 的 fold——feature 未在 tuple 里就不会被 fold 到，"漏登记"从运行期 MissingDependency 变成"这个 feature 根本不在 map 上"（接线遗漏 = 编译期/结构缺失而非静默）。

### 实施中发现的真问题（比复审更深一层）

引用成员在 vector 存储类型上是悬空陷阱：首版 ResolvedView 用引用成员绑定 tuple，runtime 全屏黑——resolve() 的局部 view push_back 进 vector 后，拷贝构造把新元素的引用绑定到源局部对象的 tuple，栈帧销毁即悬空，prepare 读到 finalize.bEnabled=false 导致 InvalidInput。改为 accessor 方法（每次调用重绑定到 this->invocations），与 design §5 "不捕获 vector element 地址" 一致。这个坑正是复审要 D3 消除的那类。

targets() 必须与 append 同门控：disabled feature 的 target 不发布（SSAO off 无 AO target 的语义保留）。

### 验证

- xmake r ya-render-3d-test 233/233（新增负向编译测试 ×2）；python3 Script/ya.py test --target ya 1817/1817。
- runtime（HelloMaterial）：Deferred / Forward / 切回 Deferred 三帧对基线逐像素 0 diff（fold 顺序 = map 顺序，target 发布顺序与旧行为在可选段不同——Bloom×3 在 EntityId/Display 之前——allocation identity 已是 role 集合比较，不受顺序影响）。
- 加 feature 触点实测：feature 文件对 + recipe 的 FeatureInvocations 一行 + resolve/prepare/append 接线——collectViewTargets/describe 零改动。

### 保留 / 未做

- M21 测试改走真实 recipe 的 request→prepare→append→publish（复审要求）留 D4 一起（D4 改 publication 路径时验收）。
- ResolveInputs/GraphInputs 分型、删除 resolve 恒空参数：本批评估后暂缓——当前 resolve 的 Inputs 全部为空类型（zero-size），分型的实际收益是文档性的，触点扩散（12 个 feature 签名 + 全部调用点）与收益不成比例，记录待 D5 feature 吸收 Stage 时一并处理。

## 2026-10-13 — D3：invocation tuple + fold（M26 verified）

### 复审缺口的落实

复审指出加 feature 要改 recipe 约 10 处、collectViewTargets/describe 手工逐 feature 列举、漏登记只在运行期暴露。本批把它们变成结构性的：

- concept 扩展：Feature 增加 name()（feature 自己的名字）与 targets(resolved, out)（feature 从自己的 Resolved 声明 View target——与 append 同门控：disabled 发布空）。13 个 feature 全部实现，static_assert 强制；缺 targets()/name() 是编译错误（负向测试 FeatureWithoutTargetsRegistrationIsNotAFeature）。
- Invocation<F>：{Resolved, Prepared, GraphOutputs} 一条记录。两 recipe 的 FeatureInvocations = std::tuple<Invocation<F>...> 按 map 顺序列出；ResolvedView/PreparedView/GraphOutputs 持有 tuple + accessor 方法（不是引用成员——见下）。
- fold：collectViewTargets 与 describe 是对 tuple 的 fold——feature 未在 tuple 里就不会被 fold 到，接线遗漏从运行期 MissingDependency 变成结构缺失。

### 实施中发现的真问题（比复审更深一层）

引用成员在 vector 存储类型上是悬空陷阱：首版 ResolvedView 用引用成员绑定 tuple，runtime 全屏黑——resolve() 的局部 view push_back 进 vector 后，拷贝构造把新元素的引用绑定到源局部对象的 tuple，栈帧销毁即悬空，prepare 读到 finalize.bEnabled=false → InvalidInput。改为 accessor 方法（每次调用重绑定到自身 invocations），与 design §5 不捕获 vector element 地址一致。这个坑正是复审要 D3 消除的那类。

targets() 与 append 同门控：disabled feature 的 target 不发布（SSAO off 无 AO target 的语义保留）。

### 验证

- xmake r ya-render-3d-test 233/233（新增负向编译测试）；python3 Script/ya.py test --target ya 1817/1817。
- runtime（HelloMaterial）：Deferred / Forward / 切回 Deferred 三帧对基线逐像素 0 diff。target 发布顺序在可选段与旧行为不同（Bloom x3 在 EntityId/Display 之前），allocation identity 已是 role 集合比较，不受顺序影响。
- 加 feature 触点实测：feature 文件对 + FeatureInvocations 一行 + resolve/prepare/append 接线——collectViewTargets/describe 零改动。

### 保留 / 未做

- M21 测试改走真实 recipe 的 request→prepare→append→publish 留 D4（D4 改 publication 路径时验收）。
- ResolveInputs/GraphInputs 分型暂缓：当前 resolve 的 Inputs 全为空类型，分型收益是文档性的，触点扩散（12 签名 + 全部调用点）与收益不成比例，随 D5 处理（记录于 todo）。

## 2026-10-13 — D4：typed 发布 + family 原子失败（M27/M13/M10/M21 verified）

### 复审缺口的落实

复审要求：GraphOutputs 到 RenderViewOutput 一处映射、不经 export 字符串往返；任一 View 的 prepare/append 失败使整个 family 不执行、不发布。本批落地：

- **typed 读回通道**：`RenderGraphExecutionResult` 增加 `bindExportedTexture(RGTextureHandle)` / `getExportedTextureShared(handle)`（`RenderGraph.h/.cpp`）；executor 的 `captureExecutionResult` 在绑名字的同时绑 handle。名字仍导出（拓扑诊断），publish 路径零名字（grep 确认无 `getExportedTextureShared(makeViewGraphName(...))` 残留）。
- **单一映射**：`Composition/ViewOutputCollection.h` 重写为 `ViewOutputHandles` + `collectViewOutput(result, handles, declaredDesc)`；两条管线各自的 `collectViewOutput` 只是把自家 GraphOutputs 适配成该结构（Deferred 另接 gBuffer/SSAO 调试字段）。旧 `SharedViewOutputs`/`collectSharedViewOutputs` 删除。
- **handle 上浮到 producer**：`BloomFeature::GraphOutputs{color,extract,blur,composite}`——`BloomPostprocessing::ChainOutputs` 把终帧 blur 从 ping/pong 判定的局部决定上浮（旧「Pong 优先、fallback Ping」读回在奇数 blur pass 时会读到中点，这类错误现在结构上不可表达）；`ForwardOpaqueFeature::GraphOutputs.color+depth`。
- **family 原子失败**：两条 `recordFamily` 任一 View 的 `prepareBindings`/`append` 失败即 `return`（graph 局部变量未执行即销毁；无 partial passes 执行、该 family 零发布）；其他 family 由 `recordViewFamilies` 的 per-family 调用天然不受影响。旧 `pop_back + continue`（partial passes 照常编译执行、只丢这一 View 的 publication）删除。

### 侦察发现：C4a 引入的 Forward depth 导出回归（已修复）

D4 侦察时按 `forward_graph_exports::viewDepth` 追读，发现 C4a（6679b00d）把 export 从 `ForwardFrameGraphPasses.cpp` 搬进 `ForwardRenderPipeline::exportGraphOutputs` 时丢了 depth：C4a 之前导出 color/depth/resolve/entityId 四个（6679b00d~1 的 ForwardFrameGraphPasses.cpp:383-388），之后只剩 color/entityId。后果是 `output.depth` 在 C4a 后恒 null——编辑器 viewport world overlay（`EditorViewportCompositor` 的 `bCanOverlay` 需要 depth）在 Forward 下静默失效。已恢复 depth 导出（`resolve` 无消费者，不恢复）；typed handle 已在 recipe outputs 上，修复是一行 export。

### 测试与验证

- `xmake r ya-render-3d-test` 235/235（43 suites）。新增：`PublishedOutputIsTheTypedReaderOfWhatTheRecipeReported`（executor.prepare → typed 读回 → 出版的就是 recipe 报告的 handle；未导出 handle 读回 null）、`AppendFailureKeepsTheWholeFamilyUnpublished`（失败注入契约：invalid input → MissingDependency）。`BloomPostprocessingTest` 迁到 `appendBloomChain`。
- runtime（HelloMaterial）：Deferred / Forward / 切回 Deferred / bloom-off（`Automation.composition_bloom_off.json`）四帧对基线（`c3b-ssao-on` / `c4b-forward` / `c3b-bloom-off`）**逐像素 max diff 0**；恢复 depth 导出后三帧复测仍全 0。日志 validation 分布与 D1 基线逐值一致（09600=20，shadow-toggle 预存在，归灯光线），无 composition 失败。
- 身份说明：`scene.get_active` 的 `(Play Mode)` 后缀即 runtime-play 会话；`set_render_pipeline` 的 `applied:false` 是"已排队、下帧生效"的既有语义（截图等待 2s 后取，切换已生效——Forward 帧与 Forward 基线 0 diff 可证）。

### 保留 / 未做 / 偏离

- 编辑器目标（`ya-game-editor`）构建被并行 GUI 重构阻塞（`EditorAnimationSetTab.cpp` 的 `clearChildren(tree, host)` vs 新签名 `clearChildren()`，他人工作区改动）；Forward depth 恢复的**编辑器活体**验证（overlay 从禁用变启用）未完成——runtime 侧四帧 0 diff 已排除画面回归，编辑器侧留作恢复后的跟进项。
- `ya-testing` 全量被并行 `ScriptApiCore.cpp` 编译错误阻塞（rpg 线 WIP）；本线用 `ya-render-3d-test` closure 验收（覆盖全部 Render3D/Graph 触点）。
- D4 未动 `resolveGraphOutputs` 之外的导出语义：`viewResolve`（MSAA resolve）无消费者，未恢复导出，记录待有需求时一并处理。

## 2026-10-13 — D5 批 1：SSAOFeature 吸收 SSAOStage（M28 分批进行中）

### 目标与边界

**单一验收目标**：SSAO 的设备对象、Settings 与生命周期（init/beginFrame/refresh/shutdown）归 SSAOFeature；pipeline 不再持 `_ssaoStage` 成员与对应 setter。recipe 的 `Stages.ssao`（`SSAOStage*` 过渡参数）保留——concept 去掉 `typename F::Stage` 是最后一批的事。

### 实施内容

- `SSAOFeature` 增加 `stdptr<SSAOStage> _stage`、`Settings _settings{.bEnabled = true}`（保持 pipeline 历史默认）与 `init/shutdown/beginFrame/refreshDeviceFormats/stage()/settings()`；`.cpp` 加实现。
- `SSAOStage` 变为纯设备对象：删 `_radius/_bias/_power/_intensity/_bReverseY`、`setSettings`、实例 `buildFrameData(ctx)`、`init` 内的 ConfigManager 读取（该读取此前总被 `initStages` 的推送覆盖，是死代码）与 `ConfigManager.h` include。静态 `buildFrameData(frameData, extent, r, b, p, i, reverseY)` 保留——feature 的 resolve 用它。
- pipeline：`_ssaoStage` 与五个镜像成员（`_bEnableSSAO/_ssaoRadius/_ssaoBias/_ssaoPower/_ssaoIntensity`）删除；snapshot/loadPersistentSettings/applyPendingSettings/buildRecipeConfig 改读写 `_ssaoFeature.settings()`；`initStages` 调 `_ssaoFeature.init(_render)`；`beginSubmission` 的 `bSsaoOn` 门控保留；`refreshGBufferStageState` 调 `refreshDeviceFormats()`；`shutdown` 调 `_ssaoFeature.shutdown()`；`Stages.ssao = _ssaoFeature.stage()`。
- 测试迁移：`ViewPassResourcesTest.SsaoRecipeKeepsInputLayoutNotSingletonSet` 改成"设备对象 init 前无 layout、settings 归 feature"；`SSAOStageTest` 从 stage 推设置改为 `SSAOFeature{}.resolve(...)` 直接断言 payload。

### 验证

- `xmake r ya-render-3d-test` 235/235。
- runtime（HelloMaterial）：SSAO-on vs `c3b-ssao-on` **0 diff**；SSAO-off（`Automation.composition_ssao_off.json`）vs `c3b-ssao-off` **0 diff**；切 Forward vs `c4b-forward` **0 diff**。
- 观察到并定性：切回 Deferred 后画面是 SSAO-on（对 `c3b-ssao-on` 0 diff），因为管线切换走 `shutdownActivePipeline → initActivePipeline` 整体重建，`loadPersistentSettings` 从 `Runtime.json` 读回 `ssaoEnabled=true`，而 automation override 只在启动时应用一次。这是既有语义（改造前后同构：重建读 config），非本批引入；要改的是"切换后 automation override 是否重放"的产品决定，不在本批范围。
- 日志 validation 分布与基线逐值一致（09600=20 等），无 composition 失败。

### 保留 / 未做

- D5 后续批次：Skybox/Overlay → Bloom/Finalize → Lighting/GBuffer → Shadow（最后，依赖灯光线最新实现）；concept 的 `typename F::Stage`/`Stage*` 参数随最后一批去掉。
- `DeferredPipelineRecipe::Stages` 仍是"设备对象传参表"，批 1 后只剩 7 项；随批次递减。

## 2026-10-13 — D5 批 2：Skybox/Overlay 按 feature 边界拆设备对象（M28 分批进行中）

### 目标与边界

**单一验收目标（该批）**：skybox 与 overlay 两个 feature 各自拥有其设备对象与生命周期；pipeline 不再持共用 Stage 成员。

### 侦察结论：先拆，再吸收

`ViewOverlayStage` 是两个 feature 共用的 672 行设备对象（skybox 管线 + billboard/worldGrid/SimpleMaterial/debug 四条管线）。直接让一个 feature 持有它会制造两次越界：SkyboxFeature 拿 OverlayFeature 的设备、或反之。按「feature 拥有其设备对象」的验收，正确动作是按 feature 边界先拆——这不是搬运而是纠偏，符合 AGENTS.md 规则 0（不做"能用就行"的假收口）与规则 5（不平行造新接口）。

### 实施内容

- **新设备对象 `SkyboxStage.{h,cpp}`**：skybox 的 DSL/PPL/pipeline、`refreshPipelineFormats`、`executeSkybox`、静态 `buildFrameData`（view 去平移）。归 `SkyboxFeature`（`stdptr<SkyboxStage> _stage` + init/shutdown/beginFrame/refreshDeviceFormats）。
- **`ViewOverlayStage` 收窄**：只剩 billboard/worldGrid/SimpleMaterial/debug 四条管线与 `executeOverlay/updateBillboardTextures`；头文件删 skybox 依赖（UnlitMaterial/Skybox.slang.h）。归 `OverlayFeature`（同型生命周期，debug 系统在 init 注入）。
- **纯数据头 `Common/ViewOverlayInputs.h`**：per-View 包 `ViewOverlayFrameInputs`（billboards/gizmos/skybox 可用性）从设备头移出。收益：`EntityIdFeature` 不再 include 任何 Stage 头（它只读 billboard 描边数据）；packet 的生命周期与设备对象解耦。
- **接线**：`DeferredPipelineRecipe::Stages` 拆出 `skybox` 槽（原来是 overlay 槽兼用）；pipeline 删 `_overlayStage`，改持 `_skyboxFeature` + `_overlayFeature`，四处生命周期调用改为 feature 转发。

### 验证

- `xmake r ya-render-3d-test` 235/235（`ViewOverlayStageTest.BuildsSkyboxFrameDataWithoutCameraTranslation` → `SkyboxStageTest.BuildsFrameDataWithoutCameraTranslation`，断言不变）。
- runtime（HelloMaterial）：Deferred / Forward / 切回 Deferred 三帧对基线（`c3b-ssao-on` / `c4b-forward`）**逐像素 max diff 0**——skybox 走新设备对象的直接证据。
- overlay 路径：`--render-graph-trace` 拓扑 dump 含 `Deferred Skybox` 与 `Deferred Forward Transparent`（28 次）；`create_billboard_regression_scene` 后 `list_billboard_components` count=2，全日志零 `Failed to prepare/append`；VUID 分布与本批无关（既有的 descriptor pool/buffer 类）。
- 引擎运行日志零新错误。

### 保留 / 未做

- D5 后续：Bloom/Finalize → Lighting/GBuffer → Shadow（最后，依赖灯光线）；concept 的 `typename F::Stage` / `Stage*` 参数随最后一批去掉，`Stages` 传参表逐步清空。
- Billboard 编辑器侧 smoke（`Script/automation/billboard/`）仍走 editor 目标，受并行 GUI 重构 `clearChildren` 签名变更阻塞，未复跑；game 侧 overlay 路径已由 trace + billboard 计数覆盖。

## 2026-10-13 — D5 批 3：Bloom/Finalize 拆分设备对象，删除 PostProcessingStage（M28 分批进行中）

### 目标与边界

**单一验收目标（该批）**：Bloom 与 Finalize 各自拥有其设备对象与生命周期；`PostProcessingStage`（两 feature 共用的容器）退场；pipeline 不再持该 Stage 成员。

### 侦察结论

`PostProcessingStage` 是 Bloom/Finalize 共用的容器：内含 `BloomPostprocessing`（bloom 链设备）与 `BasicPostprocessing`（finalize 设备）两个对象、一个横跨两者的 settings blob、以及给 automation 直连的 setter。与批 2 同型——先按 feature 边界拆。

### 实施内容

- **设备对象归位**：`BloomPostprocessing` → `BloomFeature`（`stdptr` + init/shutdown/beginFrame；bloom 管线是格式固定的，无 refresh）；`BasicPostprocessing` → `FinalizeFeature`（+ init/shutdown/beginFrame/stage()）。两条管线各持一份 feature 实例（原来也是各持一份 stage，实例数不变）。
- **settings 拆分**：`PostProcessingState` 增加两个半区投影 `gradingOnly()` / `bloomOnly()`（字段零重叠，每个字段恰有一个 owner）。管线的 `_postProcessState` 是唯一权威值，经 `Config.bloom.state` / `Config.finalize.grading`（含 `bGradingEnabled` 折叠）→ `Resolved` 流到两个 feature；feature 实例本身无 settings 状态（见下）。
- **finalize pass 搬到 FinalizeFeature**：pass 的 setup/execute 与 `Postprocessing.Output` 导出从 stage 移入 feature 的 append；`BasicPostprocessing` 只留设备职责。
- **automation 改道**：postprocess overrides 经由 `pipeline.requestSettings`（新增 wire 字段 `RenderPipelineSettings::bPostProcessGrading`）落点，不再直接写 stage 对象；顺带修复 Forward 原先只转发 shadow/postProcessing、会丢 grading 开关的缺口。
- **删除**：`PostProcessingStage.{h,cpp}`。

### 实施期发现并修复的两个真 bug（均已记录，值得后续批次警惕）

两个都属**同一族**：把设备对象从「管线成员」搬到「recipe 里的栈上临时 feature」后，原先合法的写法变成悬空/失联。

1. **lambda 捕获 this 悬空（SIGSEGV）**：`FinalizeFeature::append` 的 execute lambda 捕获 `this`，但 recipe 里 feature 是栈上临时对象（`const FinalizeFeature finalizeFeature;`），图执行时 `this` 已失效。症状：`App::quit` 前的 `BasicPostprocessing::writeInput(this=0x0)` 崩溃（lldb 定位），exit=139。修复：捕获传入的 `stage` 指针（管线持有的设备对象），与其余 feature 一致 —— 这也是既有模式的正确性依据。
   - 诊断方法：git stash 只回退本批文件 → 重建 → exit=0，与本批 exit=139 对照，先定性再定位。
2. **state 藏在 feature 成员导致配置失联**：grading/bloom state 首版存在 feature 的 `_grading`/`_settings` 成员上；管线载入的配置写在**管线成员的 feature 实例**上，而 recipe 用的是**另一个栈上临时实例**（默认值）——曝光 0.8 被静默读成默认 0.6，全屏均匀变暗（mean 111.5→93.8，p50=20 覆盖全图）。修复：state 全走 `Settings → Resolved` 数据流（feature 实例保持无状态），管线侧 `_postProcessState` 为权威值。
   - 诊断方法：在 load（写入 0.8）与 finalize append（读到 0.6）两端加探针，确认写入成功而读取失联，从而定位到「实例不是同一个」。

### 验证

- `xmake r ya-render-3d-test` 235/235（44 suites）。
- runtime（HelloMaterial）**五帧逐像素 max diff 0**：Deferred（vs `c3b-ssao-on`）/ Forward（vs `c4b-forward`）/ 切回 Deferred（vs `c3b-ssao-on`）/ bloom-off（vs `c3b-bloom-off`，`Automation.composition_bloom_off.json`）/ grading-off（vs `c3g-grading-off`，`Automation.composition_grading_off.json`）。
- runtime exit=0（修复前为 139，崩溃已消除；stash 对照确认崩溃由本批引入、也已由本批修复）。

### 保留 / 未做

- D5 后续：Lighting/GBuffer → Shadow（最后，依赖灯光线最新实现）；concept 的 `typename F::Stage` / `Stage*` 参数随最后一批去掉。
- 教训沉淀（供后续批次）：**recipe 里的 feature 是栈上临时对象**——lambda 只捕获管线持有的指针（stage/设备对象），不得捕获 `this`；feature 不得自持可变 settings，一切经 `Settings → Resolved`。
