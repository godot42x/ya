# 进度

## 2026-09-29 P1 registry 决策诊断与 import owner 表

- 改动：registry `sync` 产出 `RGRegistrySyncStats` 与结构化事件（bind/replace/retained refresh/prune/owned buffer/transient miss，附变化原因位与"registry 是否自有 GPU 对象"）；`--render-graph-trace=true` 运行时开关（关闭时只计数不存事件）；executor 带名字（Deferred/Forward/Bloom/BrdfLUT/Presentation[i]/ScreenshotCopy），开启时首帧与有替换/prune 的 sync 打印 `debugDump`（上限 4 次）；`debugDump` 新增 imports 段（wrapper/image/view 来源、buffer range、retained 数）。替换判定谓词原样保留，graph 语义不变。
- 验证：ya-render-3d-test 187 通过（新增 5 个 trace/dump 测试）；HelloMaterial 200 帧 flush 只在 frame 2/4/5/10/12，warmup 后零 flush，与 P0 基线一致；另跑 240 帧 automation（frame 60 改 800x600，frame 120 切 Forward）。
- 发现（身份是位置）：graph 每帧重建，handle = 声明序号，registry 按序号认条目。sync#2 的 11 个替换全部是位置错位：`PointShadowCull.*` 7 个 buffer 首次出现插在 handle 3，后续 `PointShadow.FaceUBO.*` 等被挤位，DDQ 因此多 11 个析构（仅 wrapper 引用，GPU 对象仍由 owner 持有）。
- 发现（真实替换判定正确）：`PointShadowIndirectRenderer` 扩容（sync#3/#4，6 个，owner 自己也 DDQ 退役旧 buffer）、环境贴图异步加载（3 个，`import-identity|image|view`）、resize 后 `ViewTargetStore` 重建（sync#61，12 个附件单次替换，DDQ 一次 12 析构后归零）都被 desc+指针比较准确识别，无漏判。
- 发现（registry 自建 import view 未被使用）：运行时所有 texture import 都带 owner 的 image 与 view，`createImportedTexture` 的建 view 分支只在测试中走到。
- 发现（transient 纹理池不淘汰）：`_transientTexturePool` 只在 `clear()` 清空，resize 后 `Bloom.BlurScratch` 按新尺寸再分配一份，旧尺寸条目常驻。
- 未验证：多 View 运行时场景。编辑器相机预览需选中 CameraComponent 实体，automation/JS 未暴露选择 API；代码上 Deferred 同 family 的所有 View 追加进同一张图、各 View label 相同，因此 View 增减会让其后所有 import 错位，label 也不能作身份。
- import owner 表（HelloMaterial Deferred/Forward，240 帧）：

| import 族 | owner | 身份来源 | 替换（非稳态） | 稳态替换 | registry 自建 GPU 对象 |
| --- | --- | --- | --- | --- | --- |
| `DeferredGBuffer.*` `DeferredView.*` `Deferred.SSAO` `Deferred.Bloom*` `ForwardView.*` | `ViewTargetStore` | `targets->find(role)` 的 wrapper/image/view | resize 时 12（单次） | 0 | 否 |
| `DeferredLight.Environment.*` | environment lighting 资源 | 环境贴图 wrapper/image/view | 异步加载完成时 3 | 0 | 否 |
| `PBRGenerateBrdfLUT.Output` `Environment.BrdfLut` | BrdfLUT 生成器 | 生成器输出纹理 | 0 | 0 | 否 |
| `*Shadow.Depth.*` `BasicShadowMap.Depth` | `BasicShadowMapTechnique` | shadow image + array view，retained=4 | 0 | 0 | 否 |
| `PointShadowCull.*` | `PointShadowIndirectRenderer` | 容量增长时新 buffer | 首现错位 7 + 扩容 6 | 0 | 否 |
| `PointShadow.FaceUBO.*` | `ShadowFrameResources`（per-flight UBO 段） | buffer + range | 错位 4 | 0 | 否 |
| `Deferred.{Frame,Light,SSAOFrame,SkyboxFrame}UBO` | `DeferredFrameResourceSet::beginView` | per-flight buffer + range | 0 | 0 | 否 |
| `*.SkinningSSBO` | `SceneSkinningCache`（P0） | scene + flight 槽 buffer | 0 | 0 | 否 |
| `SSAO.Noise` | `SSAOStage` | 噪声纹理 | 0 | 0 | 否 |
| `Presentation.Output` | swapchain / presentation service | swapchain image | 0 | 0 | 否 |
| `Bloom.BlurScratch`（transient） | registry transient 池 | desc 匹配 | resize 时新分配 1 | 0 | 是（不淘汰） |

- **P2 决策：部分执行**（用户确认）。依据：稳态已为 0，真实 owner 重建的判定准确，指针比较与 owner generation 在已观察到的所有 owner 上等价，不值得给 owner 加 generation；问题只在"条目按位置匹配"——拓扑变化（首现 pass、View 增减）会误替换并产生 DDQ churn。执行范围：`RGImported*Desc` 增加 owner 提供的稳定 key（如 viewId+attachment、light+face），registry 按 key 匹配条目，替换仍由 backing 指针/desc 判定；删除 registry 自建 import view 路径（owner 必须提供 view）；同批清理 import 转发包装与 host-written lambda；transient 纹理池淘汰未使用的旧尺寸条目并入 P2。

## 2026-09-29 计划 review（对照代码）

- 发现：上一轮校准删掉了 import 替换决策的收敛（旧 P2/P3），但 `RenderGraphResourceRegistry::sync` 仍按 desc + 指针判替换、按指针比 retained 列表，P0 根因机制未变；`createImportedTexture` 会为 import 自建 image view。
- 发现：`debugDump` 只在编译失败和测试中调用，看不到 registry 决策，import 仅有 finalizes，transient slot/alias 只覆盖 buffer；原 P1"在 debugDump 基础上"挂错了层。
- 发现：import 入口已有 5 个一行转发包装（Environment 两份相同）与 3 个 host-written lambda，`makeImportedBufferDesc` 38 处 import 中仅 2 处使用。
- 发现：原 P4 与 render-view-family（plan.md `*GpuResourceLibrary` 表、todo.md P2/P5）重叠；原 P3 的 View 附件审计已由 render-arch skill 第 18 条定论。
- 发现：Slang 生成链只出 struct offset/size，不出 set/binding；运行时 SPIRV-Cross 反射未被任何 pipeline 启用；Render 下约 63 处手写 descriptor layout。
- 用户决策：
  1. import 替换判定是否改为 owner 身份：先做 P1 诊断，看数据再决定（P2 为条件阶段）。
  2. 容器拆分：本计划接手 render-view-family 的 `*FrameResourceSet`/`ShadowFrameResources`/`PerFlightFrameResourceSetBase` 拆分与改名；`FrameRecording` 等 submission 改名仍归 render-view-family。
  3. feature 切片选 Bloom。
  4. descriptor layout：扩展生成器，Bloom 与一条完整管线改用生成 layout，删除手写。
  5. import 包装/lambda 清理与 P2 身份收敛绑定，同批处理。
- 计划调整：阶段重排为 P1 诊断+owner 表 → P2 身份收敛（条件）→ P3 Bloom → P4 容器拆分 → P5 生成 layout；原"按生命周期 owner 审计"并入 P1。
- 本轮只改计划工件（含 render-view-family 转交标注、plan/AGENTS.md 活跃线表、根 AGENTS.md 路径笔误），未改代码、未跑测试。

## 2026-09-29 计划校准

- 用户目标：改善渲染架构开发体验，重点解决 RDG 与 pipeline 自有资源缓存并存带来的 owner/失效复杂度，同时减少为了改一个功能而跨多个文件理解和调试的负担。
- 用户补充：关注开闭原则、数据与逻辑分离；“DSL”只是表达一眼看清 pipeline contract 的能力，不要求真的造 DSL。
- 现状校准：RenderGraph 已有 debugDump() 和编译期若干资源/依赖诊断；因此计划改为补充 dump 与 owner/allocation 的关联，不再把基础 dump 或泛化悬空依赖 warning 当作待建功能。
- 资源边界校准：ViewTargetStore、SceneSkinningCache、FrameUploadArena、graph transient 具有不同生命周期。import、wrapper、retained lease 不等于第二份物理 GPU allocation；后续只基于 allocation 与退役证据决定收敛。
- 计划调整：加入普通 feature 垂直切片、按生命周期的资源 owner 审计、单条真实混合容器拆分、从 Slang/reflection/graph/typed declarations 生成或校验的 pipeline contract 视图。
- 本轮只更新计划工件，没有改渲染代码，也没有重跑历史 P0 测试或 200 帧运行。
- 未决：P2 在 Bloom 与 SSAO 中选择哪个作为 feature 切片；执行前按真实改动范围选择并在进度中说明。（已于同日 review 定为 Bloom，阶段号改为 P3。）

## 2026-09-29 P0 落地记录（历史）

- 诊断：HelloMaterial 稳态每帧 1–3 个 DDQ 析构，调用栈定为 RenderGraphResourceRegistry::sync → retireRetainedResources；label 为 Deferred / DirectionalShadow / PointShadow 三路 SkinningSSBO，同 desc、每帧新指针。根因记录为 RenderSubmissionPool::acquire 每帧清空 _families，family 帧内有效，prepareSceneFamilySkinning 每帧重建 64KB buffer。
- 改动：新增 device 级 SceneSkinningCache（scene 键、flight 环、字节比对上传、DDQ 退役）；prepareFrameRecord 预录制解析进 RenderViewSceneResources::skinningBuffer；三处 beginView 改从该通道取；family 删帧内 gpu 包与 snapshot 绑定；prepareSceneFamilySkinning 只做描述符集绑定。
- 历史验证：ya-render-3d-test 182 通过（含 9 个新增 cache 回归测试）；HelloMaterial 200 帧 warmup 后零 flush；sol2 的 2 个 panic 经当时无改动对照确认为存量问题。
- commit：533003be（代码与测试一次提交）。以上为既有记录，不代表本轮重新验证。
