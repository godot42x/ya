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

## 下一步（按优先级）

- [x] **C4b（M18：Shadow 经 composition 接入）**：`ShadowFeature`（Composition/Features）成为两 recipe 的第一个条目——resolve 启用决策、prepareBindings 生产 `ShadowPreparedView` token、append 产出 `ShadowGraphOutputs`；Lighting 从 recipe 自己的 shadow 输出取 depth。两管线的 `_shadowStage->appendGraphPasses` 直调与 `prepareShadowPass` 退出，改为 `applyShadowFrameSettings()`（只应用 settings）。strategy 仍持 images/runtime/budget，算法与绑定不动。证据：trace 16-pass/13-pass 拓扑与 C3B 基线**逐条目一致**（shadow→GBuffer→…）；shadow on/off 差 31782 px 证明生效；Deferred/Forward 对基线逐像素 0 diff。
- [ ] **C4c（C4 收尾）**：确认无剩余混合生命周期资源槽位（`ViewResources` 复核）；Forward 透明槽保持"无功能契约"记录；M19 lifecycle 用例。
- [ ] **待决定的测试 hook（C3 遗留缺口）**：entity-id/picking runtime hook（读取 `DeferredView.EntityId` 的 pick/readback；editor 已有 isolate readback 路径）与 direction gizmo 场景 hook（回归场景加一个 `DirectionComponent` 实体）。
- [ ] **M12 余项**：`SurfaceImageTest` 侧编码回归未跑。
- [ ] **plan.md C2 剩余**：`_lastBlurPassCount` 调用状态退出、blur pass 次数边界测试（M09/M14）。
- [ ] **M08 双 View runtime**：两 View 不同 SSAO payload/DS 互不覆盖；共享 frame/skinning 复用。
- [ ] **M10 failure injection**：prepare/build/compile/execute 失败不发布部分新 family。
- [ ] **M13 完整**：统一输出收集，不在 pipeline 侧按 feature 名称解析。
- [ ] **C5**：组合诊断、扩展用例、多 View、performance/churn 门禁与 skill 吸收（"如何新增一个 pass"进 skill）。

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
