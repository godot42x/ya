# 组合层实现设计

> 2026-10-12；用于 C1 开工的具体设计草案。以下类型和签名是拟实现接口，不是当前源码。
> 名称允许随现有风格微调；不能改变本文件的协议和生命周期约束而不复审。

## 1. 最小对象集与物理位置

新公共头只在 `Engine/Source/Framework/Render/Render3D/include/Render3D/` 下有一个物理位置；实现放模块对应目录。全部仍属于 `ya-render-3d`，不新增 framework target。

| 拟定入口 | 内容 / owner |
| --- | --- |
| `Composition/PipelineFeature.h` | feature concept、调用状态、窄 preparation/build context；不做全局 registry |
| `Composition/PipelineResources.h` | typed texture/buffer refs、资源声明及使用描述；复用 RHI / Graph 描述 |
| `Composition/PipelineComposition.h` | add / seal、resolved 调用存储、资源要求、prepared invocation |
| `Composition/PreparedPipelineFrame.h` | 本 tick 的 family/view composition 与 lease；move-only、非长期缓存 |
| `Deferred/DeferredPipelineRecipe.h` + `Deferred/DeferredPipelineRecipe.cpp` | Deferred 的唯一组合地图 |
| `Forward/ForwardPipelineRecipe.h` + `Forward/ForwardPipelineRecipe.cpp` | Forward 的唯一组合地图 |
| 现有 `SSAOStage.h/.cpp` 等 feature 入口 | 补齐局部协议 / 类型；不为了统一名称立即改所有 Stage 名 |

允许合并短小的 Composition 头；不得拆出只有转发作用的文件。feature 内部可以保留 pass helpers 与 device resource 类型，但 public contract 必须从一个入口追到，不要求所有代码挤在一个物理文件。

## 2. Feature 协议：统一生命周期，保留异构数据

不建立 `IRenderFeature::run(GlobalContext&)`。使用 C++20 concept + 模板 add；存储实现内部可受控类型擦除，recipe / feature 端始终强类型。

```cpp
struct SSAOStage
{
    struct Inputs;      // albedo / normal / depth 的 typed composition refs
    struct Outputs;     // OptionalAO，显式无 AO 语义
    struct Resolved;    // settings 值、generated UBO、AO resource ref、调用状态
    struct DeviceVariant; // 本次所需 device layout / PSO 引用，安全点准备
    struct Prepared;    // frame slice/set、sampled input set，本次独立
    struct PreparedInputs;  // 显式借用 shared bindings；SSAO 可为空
    struct PreparedOutputs; // 被后续 feature 共享的 prepared 值；SSAO 可为空
    struct GraphInputs; // 降低后 RG handles，限 SSAO 真正需要的字段
    struct GraphOutputs;

    Resolved resolve(FeatureResolveContext&, const Inputs&,
                     const SSAOSettings&, const SSAOViewFacts&) const;
    Outputs outputs(const Resolved&) const;
    Result<DeviceVariant> prepareDevice(const Resolved&, DevicePrepareContext&);
    Result<Prepared> prepareBindings(const Resolved&, const PreparedInputs&,
                                    FeaturePrepareContext&) const;
    PreparedOutputs preparedOutputs(const Prepared&) const;
    Result<GraphOutputs> append(FeatureGraphContext&, const Resolved&,
                                const Prepared&, const GraphInputs&) const;
};
```

`Result<T>` 代表带错误的返回协议，不强制引入新全局 Result 框架；先复用仓库相应 value/error 风格。关键是 **failed 与合法 skipped 不混为 invalid handle**。

- `resolve` 在安全点调用一次：产生 immutable 决策、资源声明、CPU payload；不拿 RG handle，不分配 submission DS。
- `outputs` 仅导出 resolved 中已经确定的端口，不重新判断设置。
- `prepareDevice` 在安全点准备/复用所需 device variant；没有 variant 的 feature 返回明确空值。其引用保存到本次调用，不在 append 时按当前 format 查询。
- `prepareBindings` 使用同一个 Resolved 和已准备的 typed inputs；只通过现有 submission 机制分配，不创建/替换持久 target 或重建 PSO。
- `preparedOutputs` 仅投影本次 Prepared 中确实需要共享的数据，禁止提供任意 feature Prepared 的全局检索。
- `append` 使用 prepared 值与 lowered inputs；直接 `RenderGraph::addPass`，不自行 execute。
- 不同 feature 不强制继承一个数据基类；concept 校验输入、输出和生命周期接口。
- 全部 device lifetime 对象的 init/shutdown/beginFrame 仍有明确 owner；`beginSubmission` 只做一次 warmup，不再偷偷应用本次资源设置。

必要的格式 variant 在 resolve 后、command buffer begin 前准备；只有成功准备的 device variant 才能被 invocation 引用。layout / PSO / sampler 等录制引用按现有 RHI retain 机制保活，不能认为 CPU feature 对象长期存在就等同 GPU 引用安全。

短小 feature 的空 PreparedInputs/Outputs 和无变动 device prepare 可提供 concept-compatible 默认能力，不为统一协议写重复空函数。resolve 的声明错误可累积到 builder 并在 seal 返回错误；不强制每次 add 都返回 Result。Settings / ViewFacts 是该 feature 真正需要的值，不能传整个 pipeline settings 后让所有层自行判断。

## 3. Typed ports 不是第二套 RDG

### 3.1 组成

`TextureRef<Semantic>` / `BufferRef<Semantic>` 是 composition-local 引用，包含：

- 所属 composition 身份（用于拒绝串图引用）；
- 声明的逻辑资源槽；
- 当前生产者 / 写入版本（用于读出地图及原地续写）；
- 安全点可见的 metadata（format、extent、samples 等通过 slot 查，不在端口复制一份）。

至少区分 `LinearColor`、`Depth`、`AO`、`DisplayColor`、`EntityId`；GBuffer 输出可为 typed bundle。跨语义转换必须由明确 feature 输出，不允许公开的任意 cast。

对共享 descriptor/upload bundle 使用 `PreparedRef<T>`：它是明确 producer 已声明的 preparation 输出端口，不是 RG resource。prepare 按 recipe producer-before-consumer 顺序解析成 typed `PreparedInputs`；graph append 使用对应 RG buffer refs 和本次 binding 值。C1 legacy prelude 声明 shared frame/light prepared port，prepare 时绑定实际 bundle；Lighting 引用这个端口，不在安全点把不存在的 DS 装进输入，也不在录制期通过 `get<LegacyBag>()` 查询。

GBuffer typed output bundle 可以携带深度/颜色 resource refs 和一条明确 shared-input ref，但不携带所有 feature 的绑定。单个资源和 PreparedRef 的 lowering 由通用 typed resolver 完成，复杂 bundle 的字段连接由其局部契约显式投影；不建立平行的全功能反射 schema。

关闭时的输出按 feature 类型表达：

- SSAO：`OptionalAO{absent}`，Lighting 的 no-AO shader binding 明确使用中性语义。
- Bloom / optional color transform：返回原 ColorStream，本次不申请 outputs、scratch 或 DS。
- Skybox / sprite 空内容：根据当前行为保留或 passthrough；不能借重构悄悄改变原 pass gating。
- 缺失必需 GBuffer / Display output、准备失败：返回错误，不回落到任意第一张图。

### 3.2 add / seal

```cpp
auto shared = composition.add(viewGpuInputs, sceneInputs, viewGpuSettings, viewGpuFacts);
auto gb = composition.add(gbufferFeature, GeometryInputs{geometryInputs, shared},
                          gbufferSettings, viewFacts);
auto ao = composition.add(ssaoFeature,
                          SSAOInputs{gb.albedo, gb.normal, gb.depth},
                          ssaoSettings, ssaoFacts);
auto color = composition.add(lightFeature,
                             LightingInputs{gb, ao, shadows, environment, shared},
                             lightSettings, lightFacts);
color = composition.add(overlayFeature, OverlayInputs{color, gb.depth},
                        overlaySettings, overlayFacts);
auto ids = composition.add(entityIdFeature, PickingInputs{geometryInputs, gb.depth, shared},
                           pickingSettings, pickingFacts);
auto display = composition.add(finalizeFeature, color, displaySettings, displayFacts);
composition.publish(display, ids); // 通用机制登记公开端口；由输出声明带 role
auto resolved = composition.seal();
```

- `add` 当场调用 resolve，并保存调用数据和连接，返回 symbolic outputs；只允许引用已有 producer / 显式 external input。
- recipe 按声明顺序 append；不按名字动态重排、不进行另一轮拓扑排序。
- `seal` 检查同 composition、必需端口、输出 metadata、持久 role 冲突、重复 import identity、合法 published 输出；常规 feature 不增加 `switch(FeatureKind)`。
- producer-before-consumer 是组合约束；具体 GPU dependency / layout 仍由实际 RDG 读写决定。
- 某些纯顺序约束（family predecessor、shadow side-effect）用明确 OrderToken 降低为 `RGPassBuilder::dependsOn`，不无条件串行所有 feature/pass。

### 3.3 原地写入和 ColorStream

Overlay / Skybox 可在同一 backing texture 上 Load 后续写。它们返回新的 ColorStream producer/version，但不申请另一张 texture。lowering 后仍可是同一 RG handle，RDG 的读写声明负责顺序和 hazard。

同理 Depth 的 Load/read-only/write 行为来自真实 pass declaration，不由 `DepthRef` 名称猜测。map 中要显示 alias / continuation，不能把“同一 handle”误当“没有生产者”。

不实现完整资源 SSA / alias scheduler。C1 测试资源生产-消费，C2 增加同 backing 续写-采样测试；若现有 RDG 无法表达真实 hazard，应停止评审，不在组合器里藏 barrier。

## 4. Resource declaration：参与执行的单一事实

### 4.1 声明资源时就固定来源

FeatureResolveContext 提供窄声明 API，示意：

```cpp
auto ao = resources.viewTexture<AO>(
    EViewAttachment::SSAO,
    ViewAttachmentDesc{ /* 当前 format / usage / samples */ },
    ViewTextureGraphUse{ /* 稳定 label、final layout、publication */ });

auto scratch = resources.transientTexture<LinearColor>(RGTextureDesc{ /* ... */ });
auto frame = resources.uploadBuffer<SSAOFrame>(/* size/alignment/usage */);
```

以上不是另维护一份资源池：

| 来源 | resolve / prepare / lower 行为 |
| --- | --- |
| View persistent | 生成 `ViewTargetRequest` 中的附件；lease 后按同一 slot import |
| Owner external | 引用安全点解析出的具体 owner binding；按已有 import helpers 带入 graph |
| Graph transient | 保存实际 `RGTextureDesc/RGBufferDesc`，lower 时创建 graph 逻辑资源 |
| Submission upload | 预声明本次 slot；prepare 写入 slice、lower host-written buffer；不可把 slice 当物理 allocation |

`EViewAttachment` 保持有限稳定枚举。暂不新增 mip/per-attachment extent 能力；当前真实 feature 不需要，扩展时需连同 owner / graph 用例单独验收。示例的“加 mip”是证明边界的扩展问题，不是本轮功能要求。

### 4.2 allocation 和 graph-use 必须分开

- `ViewAttachmentDesc` 的 format/usage/samples 等仍是 allocation equality 的输入。
- label、final layout、published flags、producer token 不参与 View allocation replacement。
- request 按稳定 role 顺序规范化，禁止 recipe 排列变化造成误 replacement。
- 一个持久 role 只有一个 declaration producer；下游借已有端口。重复相同声明可做显式 intern，冲突描述必须报错，不能 last-wins。
- 同一资源 slot 在 family/view lowering 中 import 一次，后续消费者拿同一 lowered handle；不添加每个 feature 自己 import 同一 AO 的路径。
- identity 保留 P2 的 `RGImportScope(viewId)` + 稳定 label；不得由节点序号 / vector index 生成。共享 external 的 alias 需要明确 intern / key，不猜 pointer 即语义身份。
- 未准备 target 视为错误；产品 feature 不把缺 target 当作“那就 create transient”。

### 4.3 bindings 不复制 shader schema

Descriptor layout 持有在 feature/device 输入提供者；本次准备引用真实 layout 分配 sets。UBO payload 直接使用 Slang 生成类型。

本线只局部化 allocation recipe，例如 SSAO 的 frame set + input set、Bloom 每次 blur 独立 set；不手写通用 manifest 来镜像 shader set/binding。

`rdg-cache-dx` P5 完成前，已有手写 layout 移至正确 owner，但必须标明未解决生成链。P5 替换生成 layout 时不应改变 composition 接线。

## 5. 类型擦除和稳定存储的实现边界

建议初版 `ResolvedPipeline` 持有异构 `InvocationModel<F>` 的 owning records（模板 model + 窄非模板操作表）；每个 record 保存 Resolved、typed connections、feature/device execution 引用。prepared records 另存该次 Prepared，不能在 Resolved 上按“当前 View”写回。

要求：

- vector 增长 / move 不使 feature payload、连接、prepared binding 引用失效；使用稳定 owning records 或索引，不捕获 vector element 地址。
- 不使用 feature 枚举 / type_index 序列化，不公开 `std::any/get<T>(string)`。
- CPU composition 的销毁不释放正在被 GPU 使用的资源：必要资源进入 submission/command-buffer keepalive，CPU snapshot 仅需活到 callbacks 录制完成。
- graph execute callback 不捕获 `FeaturePrepareContext&` / builder / 临时局部变量；需要的数据按值或明确强引用捕获。
- records 的 CPU allocation 开销用现有 benchmark 比较；初版保持简单稳定存储，不能未测先建立第二个跨帧 cache。诊断文本按开关生成。

GraphLoweringContext 只提供 typed slot→RG handle、graph、当前 view import scope、实际 target lease、发布登记。它没有任意 settings / owner / stage 查找器。

## 6. prepare → record 的具体接入

当前 `prepareFrameRecord()` 的 void / device implicit 状态不足以保证本次资源与构图一致。拟改变为：

```cpp
// 仍由应用写完整帧顺序；此处只展示 world 部分。
auto prepared = device.prepareFrameRecord(plan); // Result<PreparedPipelineFrame>
if (!prepared) { return RecordedFrame{}; } // 本次错误，不继续 compose 旧 View

if (!device.beginFrameCommandBuffer(plan, cmdBuf)) { return RecordedFrame{}; }
device.recordViewFamilies(plan, prepared.value());
// UI compose、display compose、end / seal 继续保留原有顺序。
```

`PreparedPipelineFrame`：

- tick-local、move-only，保存 pipeline kind/有效执行引用、frame token、按 Scene family/View 对齐的 ResolvedPipeline、target leases、预解析的 external bindings。
- 由 `RuntimeRenderContext::record()` 局部持有；不挂进作者声明、不常驻 device/pipeline、不扫描以前 flight 来兜底。
- 在 command recording 前验证与 plan 的 hostTick、View 身份和 family 对齐；禁止错帧 / 错 pipeline token。
- `ViewFamilyRecordContext` 传入对应 prepared family/view range；不再重新问 pipeline 当前 settings 来决定资源。
- C1 保留外层 `SceneViewRecording` 及 extraction 结构，只增加 prepared 参数；不趁机重写四层 View 类型。

安全点顺序：应用已有 fence-safe 前提 → 为本次 tick/flight 开始空 publication → apply pipeline switch/format commands → 应用本次 queued settings → resolve Scene 外部 bindings（含 skinning）→ resolve recipes → 准备 device variants → 汇总 / prepare View targets → 保存 lease → begin recording。

将现有 `ViewTargetStore::beginPublication(flight, hostTick)` 移到安全准备入口（同 token 的调用保持现有幂等性），而不是等 command begin 后才清 live count。这样安全点失败也不会让同一 flight 查询到旧 tick 的图。command begin 不重新以别的 token 清表；既有 live GPU 引用仍由 submission/flight keepalive 守卫。

`beginPublication(flight, hostTick)` 在安全点清空该 flight 的旧 tick 发布表，标记本次 tick 开始；但不影响 GPU 资源的 submission keepalive。实际 publication（role→handle 登记）在 graph execute 成功后由通用 collector 完成（见 §8）。准备失败不清表，也不会让查询返回旧 tick 结果（发布表标记新 tick 但无有效条目）。C1 必须测试安全点错误后查询为空，不只测试 execute 错误。

若安全点没有 live submission，仅不做 descriptor / upload 分配；这部分在 begin 后以同一 resolved 实例执行。CPU UBO values 可提前确定。某个 feature 需要 content 判断时，使用已经 extracted 的 snapshot / view data，不遍历 live ECS。

失败边界：安全点失败不得拿旧 lease 继续新配置构图；binding/build/compile/execute 失败则对应 family 不发布本次结果。合法 empty/UI-only plan 返回空准备结果，不是错误。提交同步 / command abort 保持既有机制，不在本线重新设计。

C1 的 frame prepare 首版失败可以整次返回 error；成功后记录阶段按现有 family 返回边界原子发布。不要在一 family 构图尚未成功时逐 View publish。跨 family 的原有已成功结果不被另一 family 失败无条件抹掉。public pipeline selection 仍由安全点推进，prepared token 的执行引用不能在录制中途被 reload。

## 7. 共享输入不能变成重复上传或万能 bag

GBuffer 与 Lighting 当前共享 frame/light set；Forward 多 shading model 有各自生成类型；Shadow/EntityId 等使用相同 Scene skinning。处理方式：

- `DeferredViewGpuInputs` 提供一个明确的 shared frame/light/当前 local-light SSBO binding bundle，作为 recipe 显式输入；准备一次，多消费者借用。
- Forward 对应共享 provider 仅保留真实共享 layout/payload/binding，不纳入 SSAO/post/overlay 等 feature 槽位。
- skinning layout provider 只持 layout，buffer 从安全点 `RenderViewSceneResources` / Scene cache 来。
- environment input 是已解析资源包，作为 Lighting 明确输入，不经 recording-time owner 查询。
- feature 专属 generated UBO / DS 回到 feature Prepared；必要共享 provider 可以是明确 composition 输入贡献，但不能伪装成全局 `Resources.getAnything()`。
- 当前 local-light field mapping、shadow budget / matrices 等逻辑从实际最新代码迁移，不能照历史 PointLight layout 重新实现。

## 8. Publication 与诊断

Published output declaration 绑定公开 role 与本次端口。lowering 记录 role→exported handle/name；通用 collector 只在 family execute 成功后从 execution result 收集，再组装现有 `RenderViewOutput`。

初版保留 `RenderViewOutput` 公开字段和查询 API；role→字段映射集中一处。增加公开 role 可能需要此映射和 debug catalog，但不需要两套 pipeline 各自解析 export 名。allocated-but-skipped 的 attachment 不会自动发布。

Display 仍由必需 Finalize 产生；不改 `displayImage()` 现有兼容行为，也不借兼容回落掩盖本次 Finalize 缺失。Shadow diagnostic 继续独立，不挂 View role。

诊断从 owning invocation/resource/connection records 与 append 前后 pass 范围生成。不要给 RDG 增加 feature 枚举；可在已有 graph trace 下附 composition dump。跨 scope 关联带 View id、调用 label；不把 transient 调试 label 当 owner key。

## 9. 允许调整与必须复审的点

允许：短头合并、Stage 内嵌类型 vs 同目录局部类型、concept 命名、内部 operations table 的具体写法、复用已有 error 类型。

必须复审：动态排序 / 第二张 GPU graph、启用策略改变、资源 owner 改变、typed public API 退化为 runtime blackboard、把 prepared 数据持久缓存、改公开设置结构造成内容迁移、把 UI/present 放入 world recipe、依赖全量 shader rewrite 才能推进。
