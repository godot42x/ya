# 现有代码迁移地图与删除清单

> 2026-10-12 起点。下文省略的路径前缀为 `Engine/Source/Framework/Render/Render3D/`。
> 公共头的物理路径均在 `include/Render3D/...`；新头不在模块根留同名副本。

## 1. 两套 recipe 必须保留的实际行为

### Deferred

```text
Shadow prepared/output（现有策略，不建子 executor）
  → GBuffer
  → SSAO（可选）
  → Lighting（消费 GBuffer / OptionalAO / Shadow / Environment）
  → ForwardOpaque attachment 操作（当前无 draw）
  → Skybox
  → Sprite2D（当前空 worldSprites 不加 pass）
  → Bloom 等线性颜色贡献（可选）
  → Transparent/Overlay（在返回的颜色上 Load 后续写）
  → EntityId（独立输出，复用 Scene depth）
  → Finalize（必需 DisplayColor）
```

公开输出：SceneColor、SceneDepth、GBuffer0..3、有效 AO、有效颜色效果诊断、EntityId、DisplayColor。当前 Deferred 的 Bloom.Composite backing 还会被 Overlay 续写；本线必须保留这项诊断图的实际内容，不偷偷发布 overlay 之前的副本。

### Forward

```text
Shadow prepared/output
  → Opaque（现有 PBR/Phong/Unlit/Simple/Direction/Debug）
  → Skybox
  → Transparent attachment 操作（当前无 draw）
  → EntityId
  → Bloom 等线性颜色贡献（可选）
  → Finalize（必需 DisplayColor）
```

公开输出：SceneColor、SceneDepth、有效颜色效果诊断、EntityId、DisplayColor。当前 Forward 不通过 Deferred 的 Sprite2D 路径；不得因为统一协议就新增功能或强行共享 recipe。

**不要求逐字保持完全独立 pass 的 topo 序号，但必须保持资源生产-消费、Load/Store/depth、效果插入和副作用顺序。** 对空 draw pass 的删除必须另有证明与 progress 偏离记录，不能把缺画面功能当 cleanup。

## 2. 拟定 feature 边界

| Feature / 输入提供者 | typed 输入 / 输出 | 局部资源和准备 | 实施 |
| --- | --- | --- | --- |
| SSAO（补齐 `SSAOStage`） | GBuffer albedo/normal/depth → OptionalAO | generated frame UBO、frame/input DSL 与 DS、AO role、noise external | C1 |
| Deferred Lighting（补齐 `LightStage`） | GBuffer、OptionalAO、ShadowOutput、Environment、shared frame/light → LinearColor | SceneColor role、GBuffer/shadow CIS、graph pass；shared set 不复制 | C1 / C3 shared provider |
| Bloom（补齐 `BloomPostprocessing`） | ColorStream → ColorStream + typed diagnostics | persistent extract/blur/composite、graph scratch、归一 blur 次数、每 pass DS | C2 |
| Finalize（现 `PostProcessingStage` / `BasicPostprocessing`） | LinearColor + display policy → DisplayColor | Display role、input DS、immutable grading/encoding 参数 | C2 |
| GBuffer（补齐 `GBufferStage`） | geometry snapshot、shared view/skinning → typed GBuffer + Depth | GBuffer roles、SceneDepth role、formats/materials、pass params | C3 |
| Deferred shared GPU inputs | frozen View/Scene facts → shared frame/light/当前 light SSBO/skinning binding | frame/light DSL 和 generated payload/upload，准备一次；非杂物 bag | C3 |
| Skybox | LinearColor、Depth、skybox packet → continued ColorStream | skybox frame UBO/DS、明确 mesh/texture refs | C3 Deferred / C4 Forward |
| Sprite2D | ColorStream、Depth、world sprite packet → continued ColorStream | instance upload、texture batches、frame UBO/DS；现有空内容 gate | C3 |
| Overlay | ColorStream、Depth、prepared overlay packet → continued ColorStream | billboard frame/textures、gizmo inputs、独立 DS | C3 |
| EntityId（补齐 `EntityIdPass`） | geometry/sprite/billboard picking inputs、Depth、skinning → EntityId | EntityId role、frame upload/DS；保持两个策略当前输入差异 | C3 / C4 |
| Forward world contributions | scene draw packet、frame/skinning/shadow → SceneColor/Depth 与续写 | 原 shading model/material pools、frame DSL/provider；透明 attachment 语义 | C4 |
| Shadow（补齐 `ShadowStage` 接入） | frozen shadow policy、View/Scene packet → ShadowPrepared/GraphOutputs/OrderToken | 原策略 image owner、invocation face/cascade/cull bindings、indirect buffers | C3 接口 / C4 生命周期收口 |

Feature granularity 按真实功能契约，不强制“一个 pass 一个类”。Forward Opaque 内多 shading model 合法；Shadow 多 pass 合法；Skybox 共享 shader 不要求强行合并两个 strategy 的执行实现。

## 3. 现有文件 / 符号 → 去向

| 起点 | 最终归属 / 动作 | 完成阶段 |
| --- | --- | --- |
| `Deferred/DeferredRenderPipeline.cpp::appendTargetRequests` | recipe resolve 的资源汇总；函数整体退出接口 | C1 接通 / C3 删除旧 body |
| `Forward/ForwardRenderPipeline.cpp::appendTargetRequests` | 同上，保留 Forward 独立 recipe | C1 prepare 接口桥接 / C4 删除旧 body |
| `DeferredRenderPipeline::appendDeferredViewToGraph` | family renderer 驱动已解析 composition，不再组装 all-stage BuildInputs | C3 |
| `ForwardRenderPipeline::appendViewportPassGraph` | family renderer 驱动 Forward composition | C4 |
| `allocateDeferredViewPassResources` | 分到每个 feature 的 prepareBindings | C1/C2 缩减，C3 删除 |
| `allocateForwardViewPassResources` | 同上 | C2 缩减，C4 删除 |
| `DeferredFrameGraphOrchestrator.{h,cpp}` | `DeferredPipelineRecipe` 替代 map；旧 export collection 进入公共机制 | C3 删除旧入口 |
| `ForwardFrameGraphOrchestrator.{h,cpp}` | `ForwardPipelineRecipe` 替代 | C4 删除旧入口 |
| `DeferredFrameGraphPasses.{h,cpp}` / `DeferredFrameGraphPassContext` | 每段 graph contribution / params 回到对应 feature；不留总 context | C1 分离 AO/Light，C3 删除旧总包 |
| `ForwardFrameGraphPasses.{h,cpp}` | world/skybox/entity-id/颜色 feature 自己贡献 graph | C4 |
| `*FrameGraphResources.h` | typed outputs 连接 + composition-local lowering，不存每 feature 内部 handle | C3/C4 |
| `DeferredFrameResourceSet::SSAOFrameData/ssaoFrame/ssaoFrameDescriptorSet/_ssaoFrameDSL` | SSAO Resolved/Prepared/device layout | C1 |
| `DeferredFrameResourceSet::SkyboxFrameData/skyboxFrame/_skyboxFrameDSL` | Skybox 局部准备与布局 | C3 |
| `DeferredFrameResourceSet::Binding` 中 shared frame/light/SSBO | 明确 `DeferredViewGpuInputs` provider；继承最新灯光 field mapping 与预算 | C3 |
| `ForwardFrameResourceSet::Binding/FramePayloads` | Forward 真实共享 view GPU input provider + Skybox 自有准备 | C4 |
| `*FrameResourceSet::ViewResources/_viewBindings` | composition invocation 的 Prepared；GPU allocation 继续 submission owner | C3/C4 |
| `Common/ViewPassResources.h/.cpp` 中 feature 专属 types/allocateBloomPassBindings | types / preparation 回对应 feature；generic writeUniform / allocate 能力有真实消费者才保留 | C1–C4 |
| `PostProcessingStage` 的 mutable state/DSL getters | 保留公开设置 compatibility surface；graph 路径消费 frozen Resolved，不暴露内部 layout 给 pipeline | C2 |
| `BloomPostprocessing` 的缺 target→transient fallback、last-call blur 状态 | 产品调用变为声明来源固定、per-invocation count；私有 standalone 测试应显式选择来源 | C2 |
| `ShadowFrameResources` 混合 layout / View table / runtime state | device layouts/static 与 invocation prepared 分开；strategy 持 runtime state | C4 |
| `PerFlightFrameResourceSetBase` | 若仅剩 skinning layout，改为真实 layout provider；不得恢复 SSBO owner | C4 |
| `Common/IRenderPipeline.h` execution 接口 | resolve/prepared token 进入明确接口，取消独立 appendTargetRequests；settings/debug/target config surface 保留 | C1 接口 / C4 收口 |
| `RenderDeviceState.Frame.cpp::prepareFrameRecord/recordViewFamilies` | 返回并消费本次准备值；在安全点开始本 tick publication，失败不查询到旧图；publishFamilyResult 仍统一发布成功输出 | C1 |
| `Common/RenderFrameInputs.h::ViewFamilyRecordContext` | 添加本次 prepared family/view 数据引用；不把行为塞进 RenderFramePlan | C1 |
| `Applications/GameRuntime/Render/RuntimeRenderContext.cpp::record` | 仅局部持有 prepared token 并传入 world record，整帧顺序不搬回 Framework | C1 |
| `collectViewOutput` 两套 Bloom/Finalize/role 收集 | 公共成功输出 collector；descriptor metadata 来本次 actual outputs | C2–C4 |
| `Debug/ViewportDebugCatalogBuilder.cpp` 与 RenderViewOutput 消费 | 公共输出字段兼容；不自动暴露 allocated-but-skipped 图 | C2–C4 验证 |

这里的 provider 名为工作名。不能把原 bag 改名 provider 就算迁移：必须只剩真实共享输入，禁止再承载 feature slots。

## 4. C1 的桥接方式：能运行、也能退出

第一个真实 recipe 的目标形态：

```text
LegacyDeferredPrelude
  ├─ 仅剩未迁移的 shadow/GBuffer/shared-input 准备与 graph 贡献
  └─ 返回 typed GBuffer / Depth / shared bindings / external inputs
        → SSAOFeature
        → LightingFeature
        → LegacyDeferredTail（skybox/sprite/颜色/overlay/entity-id/finalize）
```

- 一个 legacy bridge 实现、两个有明确位置的调用段；不是整个旧 orchestrator 黑盒。
- 桥接 resource 声明一次，其 attachment 需求和已迁移 feature 一起 seal；不能保留旧 `appendTargetRequests` 再单独走一遍。
- 原 `importFrameBuffers/createAttachmentTextures` 先缩减为 legacy slots；SSAO/Light 必须真正拿自己的资源/Prepared，不经 legacy 私有 bag 查。
- shared frame/light 由 prelude typed input 提供，prepare 一次；C3 再迁其 provider，不在 C1 复制 UBO / SSBO。
- prelude 的 shared binding 经 `PreparedRef<T>` 声明/投影，录制期准备后解析给 Lighting；不是 resolve 时假装 DS 已存在，也不是万能 Prepared bag 查找。
- Forward 在 C1 也必须适配 prepared token，允许一个明确 LegacyForward bridge；C2 去颜色细节，C4 全退出。
- adapter 记录名称、使用者、剩余 fields、删除 checkpoint；不得添加第 3 套临时 target manager 或 legacy enable bool。

### 桥接实现约束

`LegacyDeferredPrelude` 不是 wrapper 或 adapter，而是符合 feature 协议的过渡实现：

- 实现 `resolve/prepareBindings/append/outputs` 接口，作为 composition 的一个 feature 节点。
- `resolve()` 汇总 GBuffer/Shadow 未迁移部分的资源声明（复用现有 `ViewTargetStore` role），返回 `Resolved` 持有未迁移部分的配置和资源要求。
- `prepareBindings()` 使用现有机制准备 shared frame/light（复用 `DeferredFrameResourceSet::beginView` 局部逻辑，不持有整个 `FrameResourceSet` 引用），返回 `Prepared` 持有本次 shared bindings。
- `append()` 调用现有 GBuffer/Shadow graph helper（传 typed params，不传 `DeferredFrameGraphPassContext`），记录对应 RDG pass。
- `outputs()` 返回 `PreludeOutputs{ TextureRef<GBuffer>[4], TextureRef<Depth>, PreparedRef<SharedFrameLight> }`，供 SSAO/Lighting 消费。

禁止：把整个 `DeferredFrameGraphPassContext` 包进 `LegacyPreludeFeature`，通过万能 context 查任意资源。C1 验收时如果 prelude 持有完整旧 context / orchestrator / PassContext 引用，视为未达成"局部化契约"目标。

## 5. 测试迁移规则

现有可复用用例入口：

- `Engine/Test/Source/Render3D/ViewTargetStoreTest.cpp`：allocation reuse / replacement / unregister。
- `RenderGraphCoreTest.cpp`：import scope/key、transient、barrier 与执行资源 retain。
- `ViewPassResourcesTest.cpp`、`RenderViewBindingTableTest.cpp`、`RenderSubmissionTest.cpp`：View 隔离、upload、descriptor / keepalive。
- `DeferredRenderPipelineTest.cpp`、`ForwardFrameGraphOrchestratorTest.cpp`：现有 graph 输入契约；后者当前只测默认值，不能拿它代替 recipe 行为测试。
- `ShadowPreparedViewTest.cpp`、`ViewFamilyRendererTest.cpp`、`RenderViewOutputTableTest.cpp`：shadow token / family / publication。
- 灯光线新增 `DeferredLightBufferTest.cpp`：实际 local-light 字段映射、预算与 shader 生成类型；移动代码后保留语义用例。

新用例建议落 `Engine/Test/Source/Render3D/PipelineCompositionTest.cpp`、`PipelineFeatureContractTest.cpp`、`PipelineRecipeTest.cpp`，自动进入现有 target。名称可以细分，但必须可直接执行。

旧类型删除时用等价新行为用例替代，并在 progress 列出旧测试→新测试。不能为了编译通过而删除资源隔离 / 预算 / publication 断言，也不能用读源码字段不存在代替运行时验证。

## 6. 真正完成后的 grep / diff 边界

以下现象必须在范围内消失：

- `bUseSSAO` 出现在 pipeline→BuildInputs→PassContext→append 的转发链。
- pipeline 代码点名 BloomExtract/Blur/Composite 的 request/import/DSL/export 细节。
- `allocate*ViewPassResources` 随功能数增长的集中分配表。
- public all-stage context、graph resources bag、per-flight all-feature binding table。
- stage/helper 同时有两条 graph 组合入口，或产品路径依赖 legacy bridge。

合法保留：public settings 字段、`EViewAttachment` role、feature-local params/Prepared、generic submission allocator、真正共享 GPU 输入、私有 pass helpers、RDG graph-resource registry。搜到同名不意味着必须删除，按职责核对。
