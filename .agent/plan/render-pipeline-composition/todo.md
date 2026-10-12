# 当前待办

> 2026-10-12 深夜；C1-foundation + 集成切片（真实 GBuffer→SSAO→Lighting 闭环）+ C3A（tail 全 feature 化与集中层删除）+ C3A-trim + C3B（Deferred 侧共享输入收口）+ C3G（剩余门禁）已带 runtime 证据落地。
> 命名对应：plan.md 的 C1 验收主体 = "C1-foundation + 集成切片"；plan.md 的 C2（Bloom/Finalize 样本）剩余项、C3 遗留测试 hook 与 C4 为下一步。

## 已完成（带证据）

- [x] 安全点单次 `resolveFrameComposition`；`recordViewFamilies(plan, prepared)` 显式消费，删除录制期第二次 `prepareFrameRecord`。
- [x] GBuffer/SSAO/Lighting features 拥有真实 resolve/prepareBindings/append 与真实 RDG pass。
- [x] 同一份 resolved composition 生成 ViewTargetRequest（旧手工清单 → `collectViewTargets()`）。
- [x] 删除 `DeferredFrameGraphOrchestrator`、`appendGBuffer/appendSSAO/appendLight`、`bUseSSAO` 透传、Deferred `appendTargetRequests`、`preparedComposition` 占位、`SSAOStage::appendGraphPass`。
- [x] **C3A**：ForwardOpaque/Skybox/Sprite2D/Bloom/Overlay/EntityId/Finalize 全部迁入 feature；`DeferredFrameGraphPasses`/`DeferredFrameGraphResources` bag/`allocateDeferredViewPassResources`/`ViewResources` 槽位删除；runtime：SSAO on/off 逐像素一致、bloom-off 10 pass 无拷贝 pass/transient（详见 progress.md C3A 节）。
- [x] **C3A-trim（接手/开闭收敛）**：ForwardOpaque 槽位改由 recipe 声明；`ColorContinuation.h` 单一契约（4 个使用者）；recipe 头"Adding a pass"接入说明；target 发布守卫测试；子系统 7672→7568（−104）。
- [x] **C3B（M17 Deferred 侧）**：SSAO/Skybox payload+DSL+DS 归 feature 局部准备；共享 `Binding` 只留 frame/light/skinning（6 字段）；trace 每共享 buffer 每 View import 一次、SSAO off 无 SSAO UBO import；子系统 7568→7536（−32）。
- [x] Runtime 证据（C1i + C3A + C3A-trim + C3B）：SSAO on/off 图对照 + `Deferred.SSAO` import 14245→0 + Forward↔Deferred 切换逐像素一致 + 三场景像素 0 diff（C3B 再次逐像素一致）。
- [x] **C3G（剩余门禁）**：sprite+opaque 遮挡（`run_sprite2d_isolated.py` 全绿：遮挡 0→137270 px、世界尺寸/正交尺度断言；trace 证明跑在 Deferred 组合的 `Deferred Sprites` pass）；overlay/billboard（editor 回归绿；修复 companion 重构造成的过期键名测试脚本债）；grading off（10 pass、Finalize 仍发布 `Deferred.View.Display`、与 graded 基线仅色彩差异 max 19/255）；M11 bloom on/off 续写 backing 断言成文。
- [x] **协议诚实化（审计后续，非 plan.md checkpoint）**：`Feature` concept 改为真实签名并由 9 个 feature `static_assert` 强制；`FeatureGraphContext` 删 `graph`/`render` 死字段；`RenderStageContext` 删 `Scene* derivedScene`（Forward 侧唯一读点改显式传参）。证据：SSAO-on 与 c3b 基线逐像素一致、Forward 与切换前基线逐像素一致、切换回 Deferred 逐像素一致。

- [x] **C4a（Forward recipe + 共享 feature + 两处收口）**：
  - `ForwardPipelineRecipe`（resolve/prepareBindings/appendGraph）取代 `ForwardFrameGraphOrchestrator`/`ForwardFrameGraphPasses`/`ForwardFrameGraphResources`；Forward 的 View target 由 `collectViewTargets()` 单一来源产生。
  - **共享 feature**：`BloomFeature`/`EntityIdFeature`/`FinalizeFeature` 移到 `Composition/Features/`，pass 名与 import 标签由 Settings 从 recipe 传入（两管线各自词汇，字符串逐字不变）——M16 要的跨管线同协议证据。
  - 新增 `ForwardOpaqueFeature`（clear + 全部 shading model + direction overlay + debug）与 `ForwardSkyboxFeature`（skybox frame UBO 自持）；`ForwardViewStage::execute*` 去掉只重复 pass context 的 binding 参数。
  - **M17 Forward 半边**：skybox frame UBO/DS 自 `ForwardFrameResourceSet` 移出（唯一消费者 = skybox feature）；`ForwardViewAuxPasses` 自持 skybox frame DSL（同 C3B Deferred 模式）；其 `prepare` 删除。
  - **`EFrameComposition::NotApplicable` / `appendTargetRequests` 全退出**：两管线都 resolve composition，`RenderDeviceState` 无预组合分配路径。
  - 证据：render3d 223/223、full 1814/1814；Deferred / Forward / 切回 Deferred 三帧与改动前基线**逐像素一致**（max diff 0）。

- [x] **C5a（收尾批次：清理 + 诊断）**：
  - **`_lastBlurPassCount` 退场（plan-C2 余项 / M09/M14）**：blur 次数改为 `appendGraphPasses` 的局部值（它由本次 state 算出、几行后由 composite 读）；成员态会让一个 View 的 composite 依赖"最后调用的是谁"。新增 `BloomPostprocessingTest` 边界用例：同帧两次不同次数的 bloom 各自选对自己的 ping-pong 侧——**验证过它确实抓 bug**（临时把成员态改回来，测试立刻红）。
  - **M13 共享输出收集**：新增 `Composition/ViewOutputCollection.h`——Bloom/Postprocess 的 export 名（两条管线相同的 feature 词汇）从"两个 pipeline 各解析一遍"收敛为单一读取点；管线各自的 pass 词汇（`Deferred.*` / `ForwardView.*`）保留在各自 PassNames。
  - **死脚手架删除**：`CompositionTextureRef`/`CompositionBufferRef`/`CompositionPreparedRef` + `composition_tag`（4 次审计均为零生产使用；真实 typed ports 是各 feature 的 Inputs/Outputs，由 concept 强制）与 `Core/Optional.h`（`OptionalHandle` 零生产使用）。`Result` 保留（67 处生产使用）。
  - **M20 组合诊断**：两个 `ResolvedView::describe()` 输出"每 feature 开/关 + 声明的 targets"（owning 字符串，不悬空）；接入既有 `--render-graph-trace` 开关，off 时不构建文本。runtime 实测两个管线的 dump 均出现（`[RGTrace] composition:`）。
  - 证据：render3d 224/224、full 1808/1808；**新实例**（未改任何设置）Deferred / Forward / 切回 Deferred 三帧对基线逐像素 max diff 0。
  - **事故记录**：一组并行 rpg-tactics runtime 与测试实例争抢 GPU 造成 `DEVICE_LOST`（28 个历史日志中仅该次出现；独占复跑 0 次，且它与既存 VUID-03320 相邻但后者在之前多次运行中都不致命）。另：并行会话的提交 `06e083ad` 扫入了我暂存的 `Optional.h` 删除，一度让 HEAD 的 include 悬空，已由本批补全删除后修复。

- [x] **C5b（M21 扩展验收）**：`FeatureProtocolTest.TestOnlyFeatureRunsTheFullChainWithoutCorePlumbing`——测试专用 `TestTransformFeature`（producer → 两段多 pass transform，私有 scratch 由 `graph.createTexture` 创建 → result）走完 resolve → prepareBindings → append → compile；`static_assert(Feature<...>)` 证明同一协议；零 pipeline/core/bag 改动。负向证据：首版把 result 覆盖 scratch 自身时 `graph.compile` 立刻报 ReadBeforeWrite（接线错误被 RDG 校验抓住）。证据：render3d 225/225、full 1809/1809。

- [x] **C5c（M04 修复）**：`ViewTargetRequest::operator==` 原为默认顺序敏感比较 → recipe 声明重排会重建整组 View target（新 generation + 重分配图像）。改为按 role 集合比较（allocation 本就按 role 索引）；新增 `ViewTargetStoreTest.ReorderingDeclarationsKeepsTheAllocation` 并负向验证过。render3d 226/226、full 1810/1810、三帧逐像素 0 diff。

- [x] **C5d（M05/M06 证据补齐）**：`DuplicateRoleIsRejectedInsteadOfLastWins`（role 冲突拒绝，非 last-wins）、`InsertingAnImportLeavesExistingBindingsUnreplaced`（import key = label+scope，新增 import 不替换既有绑定）。render3d 228/228、full 1811/1811。

- [x] **C5e（M20 收口）**：组合诊断补 pass 贡献关联——两管线输出 `composition contributed ... passes [a..b)`，runtime 实测与 graph trace 的 pass 数（16 / shadow-off 14）逐一对上。

## 下一步（按优先级）

- [x] **C4b（M18：Shadow 经 composition 接入）**：`ShadowFeature`（Composition/Features）成为两 recipe 的第一个条目——resolve 启用决策、prepareBindings 生产 `ShadowPreparedView` token、append 产出 `ShadowGraphOutputs`；Lighting 从 recipe 自己的 shadow 输出取 depth。两管线的 `_shadowStage->appendGraphPasses` 直调与 `prepareShadowPass` 退出，改为 `applyShadowFrameSettings()`（只应用 settings）。strategy 仍持 images/runtime/budget，算法与绑定不动。证据：trace 16-pass/13-pass 拓扑与 C3B 基线**逐条目一致**（shadow→GBuffer→…）；shadow on/off 差 31782 px 证明生效；Deferred/Forward 对基线逐像素 0 diff。
- [x] **C4c（C4 收尾）**：
  - **`ForwardFrameResourceSet::ViewResources` bag 删除**：entityId/post 槽位在 C4a 后已是死写，debug 收进 `ForwardOpaqueFeature`（它在画 debug pass，UBO 本就该它建）；`ViewResources` 结构、`getViewResources`/`mutableViewResources`、`allocateForwardViewPassResources` 全部退出——Forward 的 View slot 与 Deferred 一样就是 `Binding`。
  - **死绑定结构清除**：`SSAOPassBindings` / `DeferredLightingPassBindings`（bag 删除后零引用）、`PostprocessPassBindings`（bloom+toneMap 镜像，feature 各自持有后无消费者）。
  - **M19 recipe 级用例**（`ForwardPipelineRecipeTest` +3）：空 plan resolve 为"空组合"而非失败；disabled config 是 `InvalidInput` 错误而非空组合；缺 View binding 的 prepare 是 `MissingContext` 而非静默绑定空集。store/submission 层的 reuse/generation/lease/fence 由既有 `ViewTargetStoreTest`/`RenderSubmissionTest` 覆盖。
  - Forward 透明槽保持"无功能契约"记录（recipe 内联声明，同 Deferred opaque 槽待遇）——注释已在 `ForwardPipelineRecipe.cpp`。
  - 证据：render3d 226/226、full 1817/1817；Deferred / Forward / 切回 Deferred 三帧与基线**逐像素 0 diff**。
- [ ] **待决定的测试 hook（C3 遗留缺口）**：entity-id/picking runtime hook（读取 `DeferredView.EntityId` 的 pick/readback；editor 已有 isolate readback 路径）与 direction gizmo 场景 hook（回归场景加一个 `DirectionComponent` 实体）。
- [ ] **M12 余项**：`SurfaceImageTest` 侧编码回归未跑。
- [ ] **M08 双 View runtime**：两 View 不同 SSAO payload/DS 互不覆盖；共享 frame/skinning 复用。
- [ ] **M10 failure injection**：prepare/build/compile/execute 失败不发布部分新 family。
- [ ] **M13 完整**：统一输出收集，不在 pipeline 侧按 feature 名称解析。
- [ ] **C5 余项**：M22（双 View runtime——需先有第二 View 声明入口，超出本线范围，待决定）、M23（performance/churn 三跑前后对照，依赖 perf-baseline 机制）、skill 吸收（"如何新增一个 pass"进 render-arch）；M20 的 RDG pass 区间关联。

## 不得漏掉

- [ ] M22 双管线多 View 实跑；历史 scope 单测不是 runtime 验证。
- [x] AO 关闭无专属 requirement/import/pass/新 publication；C3A 收口：ViewResources 槽位删除，SSAO off 时 feature 不分配 DS（runtime off 13 pass + 像素一致）。
- [x] Bloom off：无 Bloom pass/transient/import；Finalize 仍执行产生 DisplayColor（runtime bloom-off 10 pass）。
- [x] Bloom on 时 Overlay 续写 Composite backing 的诊断内容（M11）：C3G 已把 before/after topology 断言成文（bloom on = `Deferred.BloomComposite`；bloom off = `DeferredView.Color`，单附件 Load/Store 无拷贝）。
- [x] 旧灯光 GPU 字段映射、生成类型、预算和现有 shadow 策略用例随迁移保留（C3B：`DeferredLightBufferTest` 全绿，shared provider 只含 frame/light/skinning）。
- [x] prepare 失败中止 recording（代码路径）；[ ] 完整 failure injection 仍待补。
- [ ] 不并行推进 `rdg-cache-dx` 旧 P3/P4 或旧 orchestrator 改名批次。
- [ ] `rdg-cache-dx` P5 未完成前，不宣称 shader layout 单源化已完成。

## 当前阻塞 / 未验证

无架构方向阻塞。待实测：backend 多 View fixture（M08）、画面重复性基线（≥2 次抓帧）、性能 before/after（M23）、grading-off 截图（M12）。工作区 `Engine/Plugins/log.cc` 为灯光线改动，未纳入本线提交。若实际实现违反 `plan.md` §8，应停止并复审，不臆造下一任务。
