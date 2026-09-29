# RenderGraph 资源生命周期与管线开发体验

> 建立日期：2026-09-29
> 更新日期：2026-09-29
> 状态：P0 skinning 生命周期收敛、P1 registry 决策诊断已落地；P2 定为部分执行（按 owner key 匹配 import 条目）；P2–P5 待做。

## 目标

降低修改渲染功能时需要同时理解的文件数、生命周期时间轴和隐式契约，让开发者能够回答：

- 一个 pipeline / feature 由哪些 pass 组成，输入输出是什么？
- 它需要哪些 descriptor、buffer、texture、vertex input；调用方必须提供什么？
- 某个 GPU allocation 由谁创建、按什么身份复用、何时失效和退役？
- 增加一个普通 feature 时，哪些文件是扩展点，哪些核心层不应被迫修改？

期望是可读、可核对的 contract，不要求外部 DSL 或重写管线声明方式。shader 侧接口以 Slang 生成物为唯一事实源；不维护与代码平行的手写 schema。

## 现状（2026-09-29 按代码核对）

- **registry 自己做 import 替换决策。** `RenderGraphResourceRegistry::sync` 用 `diffTexture` / `diffImportedBuffer`（P1 前名为 `needs*Replacement`）比较 desc 与指针（texture 比底层 image/view 指针，buffer 比 `IBuffer` 指针），retained 列表也按指针比较后 `retireRetainedResources`。P0 的每帧 DDQ churn 就出在这条路径；P0 通过让 owner 保持指针稳定修掉了症状，判定机制未变。
- **import 路径上 registry 会自建 GPU 对象。** `createImportedTexture` 在调用方未提供 view 时 `_factory.createImageView`，未提供 image 时 `_factory.importImage`；这些对象由 registry 条目持有并退役。
- **`debugDump()` 看不到 registry 决策。** 只在编译失败（`RenderGraphExecutor.cpp`）和测试中调用，无运行时触发；输出的是 compiled graph，import 仅有 finalizes，transient slot/alias 只覆盖 buffer（texture 走 `acquireTransientTexture` 池，不在 dump 中）。
- **import 入口分散。** 38 处 `importBuffer/importTexture` 调用；`makeImportedBufferDesc` 仅 PointShadow 2 处使用。已有 5 个一行转发 `makeImportedTextureDesc` 的匿名包装（SSAO、Bloom、PostProcess、Environment ×2，后两者完全相同），3 个各自写的 host-written buffer lambda（Deferred、DirectionalShadow、PointShadow）；同一个 skinning buffer 走了 3 条不同 import 路径。
- **View target 所有权已定。** `render-arch` skill 第 18 条：View texture 唯一 owner 是 `ViewTargetStore`（generation 标记 replacement，`ViewTargetLease` 保活）。本计划不重审 View 附件本身，只审 registry 一侧对它们的 import 处理。
- **资源容器混合生命周期。** `DeferredFrameResourceSet` / `ForwardFrameResourceSet` 同时持有 device 级 DSL、按 flight×View 的 `RenderViewBindingTable<ViewResources>` 和 shadow 运行态；`DeferredFrameResourceSet.h` 类注释在 P0 后已过时（仍称持有 skinning storage）。
- **descriptor layout 是手写的第二份事实。** `slang_gen_header.py` 读 Slang reflection JSON，但只生成 struct offset/size，不生成 set/binding；SPIRV-Cross 运行时反射（`ShaderReflectionConfig`）存在但没有 pipeline 启用；Render 下约 63 处手写 `DescriptorSetLayout*` 字面量。

## 生命周期与职责边界

| 责任 | 唯一事实 / owner | 允许的职责 | 不应承担 |
| --- | --- | --- | --- |
| Graph 临时资源 | RenderGraphResourceRegistry 的 transient 池 | graph 内生命周期、别名复用、执行期资源状态 | scene/view 跨帧资源的替换决策 |
| 外部 GPU allocation | 对应 device / scene / view / submission owner | 创建、复用身份、失效和安全退役 | pass 顺序与 barrier 推导 |
| Graph import | graph 对外部资源的 binding/lease | 将资源带入 graph，并保证录制到 submit 所需的存活期 | 第二份物理 allocation、跨帧复用策略 |
| Pipeline / feature | feature 模块及组合入口 | 静态配置、必需输入、pass 组合和输出 | 自建与通用 owner 并行的 GPU 资源池 |
| Shader interface | Slang 与生成链 | descriptor layout 和 shader buffer layout 的事实源 | 手写重复的 shader-facing schema |
| Vertex input | 现有 vertex declaration / pipeline 配置 | 明确格式、stride、attribute 与调用方输入要求 | 由诊断文档复制维护一份会漂移的定义 |

表中"Graph import 不应承担跨帧复用策略"是目标，不是现状；现状见上一节第一条，P1 数据后按 P2（部分执行）收敛。

## 阶段

### P0：Scene skinning 生命周期收敛（已完成）

SceneSkinningCache 按 scene 与 flight 环复用 skinning SSBO，避免帧内 SceneFamily 重建引起每帧换指针与退役。历史验证：ya-render-3d-test 182 通过，HelloMaterial 200 帧 warmup 后零 flush（commit 533003be）。后续执行需按当前环境重跑，不当作新验证。

### P1：registry 决策诊断与 import owner 表

在 registry `sync` 的替换 / retained 刷新 / prune 退役点输出结构化事件：handle、label、lifetime、判定原因（desc 哪个字段不同、wrapper 指针变化、底层 image/view/buffer 指针变化、retained 列表变化）、是否导致物理对象退役（区分 registry 自建 view 与外部 owner 的 allocation）。提供运行时开关（CLI 参数或 automation 配置，沿用现有 `AppOptions` / automation 入口），不常驻打印。`debugDump` 补 imported 资源段，只展示 owner 能稳定提供的信息，不在 registry 内猜身份。

用这套诊断跑 HelloMaterial（及至少一个多 View 场景，如编辑器 viewport），产出 import owner 表：每个 import 族的 owner、稳定身份来源（generation / 内容版本 / 无）、每帧替换与退役次数、registry 是否为其自建 GPU 对象。

验收：
- 已知 churn 能从事件指到生产方和原因；能区分"wrapper 更新"和"物理 allocation 替换"。
- graph 运行语义不变；ya-render-3d-test 全过；HelloMaterial flush 基线不变。
- progress.md 记录 owner 表与 **P2 决策**（执行 / 不执行 / 部分执行）及依据。

### P2：import 条目按 owner key 匹配（P1 决策：部分执行）

P1 数据：稳态替换已为 0，真实 owner 重建（扩容、环境加载、resize）都被 desc+指针比较准确识别；问题在 registry 按 handle（即声明序号）匹配条目，拓扑变化（首现 pass、View 增减）会把后续 import 全部误判为替换并产生 DDQ churn；同一张图内多 View 的 label 相同，不能作身份。详见 progress.md P1 记录。

范围：
- `RGImported*Desc` 增加 owner 提供的稳定 key（如 `ViewTargetStore` 的 viewId + attachment、shadow 的 light + face、SceneSkinningCache 的 scene + flight 槽），registry 按 key 匹配 import 条目；替换仍由 backing（image/view/buffer 指针与 desc）判定。不给 owner 加 generation 计数。
- 删除 registry 为 import 自建 image view / import image 的路径，owner 必须提供 view（P1 运行时从未走到该分支）。
- import desc 入口同批收敛：删除 5 个一行转发包装与 3 个 host-written lambda，调用点统一走 `RenderGraphImportUtils` 中的构造函数（签名携带 key），不新增同义 helper。
- transient 纹理池淘汰：`_transientTexturePool` 淘汰本帧未使用的旧 desc 条目（经 DDQ 退役），resize 后不再常驻旧尺寸 `Bloom.BlurScratch`。

验收：拓扑变化不产生误替换（P1 trace 计数，含单测覆盖 pass 插入与多 View 同 label）；HelloMaterial 稳态替换 0；resize 只触发 owner 单次替换且旧 transient 条目被淘汰；ya-render-3d-test 全过；flush 基线不回退。

### P3：Bloom 垂直切片

以 Bloom 验证普通 feature 的局部扩展路径。Bloom 经 `PostProcessingStage` 被 Forward 与 Deferred 共用，并依赖 `ViewTargetStore` 的 `BloomExtract/Blur/Composite` 附件。把它的输入要求、资源声明、pass 组合、descriptor/buffer 要求与调用方参数整理为一个可读入口，并检查改动 Bloom 时是否被迫触碰 executor、registry、无关 feature 或多个缓存。

若过程反复跨越无关文件，先调整实际职责边界再完成 feature；不以搬文件或抽象层数量作为成果。

验收：一份具体变更清单（触及文件与职责）；开发者能从 Bloom 入口追全调用与输入；Forward 与 Deferred 下 Bloom 渲染结果不变（截图或 automation 对照）；相关测试通过。

### P4：Forward / Deferred / Shadow 资源容器按生命周期拆分（接手自 render-view-family）

2026-09-29 起由本计划接手 `render-view-family` 中的以下条目：`DeferredFrameResourceSet` → `DeferredGpuResourceLibrary`、`ForwardFrameResourceSet` → `ForwardGpuResourceLibrary`、`ShadowFrameResources` → `ShadowViewResources`、`PerFlightFrameResourceSetBase` → `SkinningLayoutProvider`，以及这些容器的职责拆分。`RenderSubmission` → `FrameRecording`、`flightIndex` → `flightSlot`、`FrameUploadArena` → `UploadArena` 及 orchestrator/passes 文件改名仍归 `render-view-family`。

拆分方向：`*GpuResourceLibrary` 只持 device 级 layout/pool/static 资源；按 flight×View 的 `ViewResources` 绑定表与 upload slice 移到 submission 侧持有者；shadow 运行态归 shadow strategy。先走通 Deferred 一条调用链（frame 准备 → graph import → submit/retire），再按同一模式处理 Forward 与 Shadow。修正过时注释。

验收：View、Scene 与 flight 身份不再靠同一结构中的散落字段推断；时间轴可顺读；ya-render-3d-test（含 `DeferredFrameResourceSetTest` 随新名）全过；稳定态 allocation/flush 基线不回退；`render-view-family` 对应条目标注已转交。

### P5：Slang 生成 descriptor layout，替换手写

扩展 `slang_gen_header.py`，从 reflection JSON 生成每个 entry 的 descriptor set/binding 表（set、binding、descriptor 类型、数量、stage）。Bloom 与一条完整管线（Deferred 或 Forward，执行时按改动面选定）改为直接使用生成的 layout，删除对应手写 `DescriptorSetLayout*` 字面量。生成物与 graph pass 声明合起来即 pipeline contract 的可读来源。

验收：所选路径上不再有手写 descriptor layout；shader 改 binding 后 C++ 侧随生成物变化，不一致在编译或测试阶段暴露；渲染结果不变；`xmake ya-shader` 生成链与 ya-render-3d-test 通过。reflection 不支持的字段显式标为未描述并规划最小来源，不从名称猜测。其余手写 layout 的迁移列为后续项，不在本阶段批量改。

## 全局验收标准

- 开发者不需同时追读多个缓存与执行层才能回答资源的 owner、身份和退役时机。
- 普通 feature 的输入/输出与扩展点在一个局部入口可见，核心 graph 执行层不因 feature 变化而被迫修改。
- shader-facing 接口可追溯到 Slang 生成物，没有需人工同步的第二份接口定义（P5 覆盖路径内）。
- 不改变 pass 声明的读写语义、graph 推导的执行顺序或 barrier 算法，除非某个 checkpoint 明确提出并单独评审。
- 资源缓存改动比较 allocation、replacement、retirement 和 DeferredDeletionQueue flush 基线，并记录运行环境及 warmup。

## 明确不做

- 不引入外部 DSL、代码生成语言或大规模管线重写。
- 不把 registry、ViewTargetStore、SceneSkinningCache 和 submission arena 合并成全局资源管理器。
- 不把 import wrapper、retained 引用或 graph handle 直接等同于物理 GPU allocation。
- 不添加对任意无依赖 pass 的泛化 warning；保留现有有语义的 graph 编译检查。
- 不新增与 `RenderGraphImportUtils` 已有构造函数同义的薄 helper。
- 不接手 `FrameRecording` 重命名与 submission 模型改造（归 render-view-family）。
- 不以文件数量减少、目录搬动或提交数量作为开发体验改善的替代指标。

## 验证基线

执行前确认 target/project 仍有效（2026-09-29 已核对存在）：

    xmake b ya-render-3d-test && xmake r ya-render-3d-test
    python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=200 --log-level=trace

对比 HelloMaterial 稳定态 DeferredDeletionQueue flush 时，排除首次创建、resize 和退出时 flushAll；P1 落地后同时记录 import 替换/退役计数。旧记录中的"warmup 帧约 frame 2/4/5/10/12"仅作历史参考，不作为固定断言。
