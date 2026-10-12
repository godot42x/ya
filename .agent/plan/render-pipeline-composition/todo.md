# 当前待办

> 2026-10-12 深夜；C1-foundation + 集成切片（真实 GBuffer→SSAO→Lighting 闭环）+ C3A（tail 全 feature 化与集中层删除）已带 runtime 证据落地。
> 命名对应：plan.md 的 C1 验收主体 = "C1-foundation + 集成切片"；plan.md 的 C2（Bloom/Finalize 样本）剩余项与 C3B（shared inputs 收口）为下一步。

## 已完成（带证据）

- [x] 安全点单次 `resolveFrameComposition`；`recordViewFamilies(plan, prepared)` 显式消费，删除录制期第二次 `prepareFrameRecord`。
- [x] GBuffer/SSAO/Lighting features 拥有真实 resolve/prepareBindings/append 与真实 RDG pass。
- [x] 同一份 resolved composition 生成 ViewTargetRequest（旧手工清单 → `collectViewTargets()`）。
- [x] 删除 `DeferredFrameGraphOrchestrator`、`appendGBuffer/appendSSAO/appendLight`、`bUseSSAO` 透传、Deferred `appendTargetRequests`、`preparedComposition` 占位、`SSAOStage::appendGraphPass`。
- [x] **C3A**：ForwardOpaque/Skybox/Sprite2D/Bloom/Overlay/EntityId/Finalize 全部迁入 feature；`DeferredFrameGraphPasses`/`DeferredFrameGraphResources` bag/`allocateDeferredViewPassResources`/`ViewResources` 槽位删除；runtime：SSAO on/off 逐像素一致、bloom-off 10 pass 无拷贝 pass/transient（详见 progress.md C3A 节）。
- [x] Runtime 证据（C1i + C3A 复验）：SSAO on/off 图对照 + `Deferred.SSAO` import 14245→0 + Forward↔Deferred 切换逐像素一致 + C3A on/off 像素 0 diff。

## 下一步（按优先级）

- [ ] **C3B / M17**：SSAO/Skybox payload+DSL+DS 归 feature 局部准备；`Binding` 只留真实共享输入；shared upload 计数验证。
- [ ] **裁薄壳决定**：C3A 实测行数净 +612（协议壳 > 集中层删除）；对 ForwardOpaque/Skybox 等薄 pass 评估 recipe 声明或共享 helper，先决定再动工。
- [ ] **plan.md C2 剩余**：`_lastBlurPassCount` 调用状态退出、blur pass 次数边界测试（M09/M14）、grading-off 截图（M12）。
- [ ] **M08 双 View runtime**：两 View 不同 SSAO payload/DS 互不覆盖；共享 frame/skinning 复用。
- [ ] **M10 failure injection**：prepare/build/compile/execute 失败不发布部分新 family。
- [ ] **M13 完整**：统一输出收集，不在 pipeline 侧按 feature 名称解析。
- [ ] **C4**：Forward recipe、Shadow typed token、`appendTargetRequests` 全退出。
- [ ] **C5**：组合诊断、扩展用例、多 View、performance/churn 门禁与 skill 吸收。

## 不得漏掉

- [ ] M22 双管线多 View 实跑；历史 scope 单测不是 runtime 验证。
- [x] AO 关闭无专属 requirement/import/pass/新 publication；C3A 收口：ViewResources 槽位删除，SSAO off 时 feature 不分配 DS（runtime off 13 pass + 像素一致）。
- [x] Bloom off：无 Bloom pass/transient/import；Finalize 仍执行产生 DisplayColor（runtime bloom-off 10 pass）。
- [ ] Bloom on 时 Overlay 续写 Composite backing 的诊断内容（M11）需要在 C4 前补 before/after topology 断言。
- [ ] 旧灯光 GPU 字段映射、生成类型、预算和现有 shadow 策略用例随迁移保留。
- [x] prepare 失败中止 recording（代码路径）；[ ] 完整 failure injection 仍待补。
- [ ] 不并行推进 `rdg-cache-dx` 旧 P3/P4 或旧 orchestrator 改名批次。
- [ ] `rdg-cache-dx` P5 未完成前，不宣称 shader layout 单源化已完成。

## 当前阻塞 / 未验证

无架构方向阻塞。待实测：backend 多 View fixture（M08）、画面重复性基线（≥2 次抓帧）、性能 before/after（M23）、grading-off 截图（M12）。工作区 `Engine/Plugins/log.cc` 为灯光线改动，未纳入本线提交。若实际实现违反 `plan.md` §8，应停止并复审，不臆造下一任务。
