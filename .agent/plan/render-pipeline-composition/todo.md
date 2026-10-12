# 当前待办

> 2026-10-12 晚；C1-foundation + 集成切片（真实 GBuffer→SSAO→Lighting 闭环）已带 runtime 证据落地。
> 命名对应：plan.md 的 C1 验收主体 = 本线 "C1-foundation + 集成切片"；plan.md 的 C2（Bloom/Finalize 样本）与 C3（Deferred 全地图）未开始。

## 已完成（本轮，带证据）

- [x] 安全点单次 `resolveFrameComposition`；`recordViewFamilies(plan, prepared)` 显式消费，删除录制期第二次 `prepareFrameRecord`。
- [x] GBuffer/SSAO/Lighting features 拥有真实 resolve/prepareBindings/append 与真实 RDG pass；recipe/feature 内无硬编码 format/extent/null stage。
- [x] 同一份 resolved composition 生成 ViewTargetRequest（GBuffer×4 + depth + 可选 SSAO + SceneColor + tail extras）。
- [x] 删除 `DeferredFrameGraphOrchestrator`、`appendGBuffer/appendSSAO/appendLight`、`bUseSSAO` 透传、Deferred `appendTargetRequests`、`preparedComposition` 占位、`SSAOStage::appendGraphPass`。
- [x] Runtime 证据：SSAO on/off 图对照 + `Deferred.SSAO` import 14245→0 + 截图差异 13.51% + Forward↔Deferred 切换逐像素一致（详见 progress.md）。

## 下一步（按优先级）

- [ ] **C3 tail 迁移**：ForwardOpaque / Skybox / Sprite2D / Bloom / ForwardTransparent / EntityId / Postprocess → features；`DeferredFrameGraphResources` bag、`DeferredFrameResourceSet::ViewResources` 槽位、`allocateDeferredViewPassResources` 退出。
- [ ] **shared inputs 明确 owner**：frame/light/skinning/environment 数据提供者与布局收口（M17）。
- [ ] **plan.md C2 样本**：Bloom 多 pass 契约 + 必需 Finalize（M09/M11/M12/M14）；`_lastBlurPassCount` 类调用状态退出。
- [ ] **M08 双 View runtime**：两 View 不同 SSAO payload/DS 互不覆盖；共享 frame/skinning 复用。
- [ ] **M10 failure injection**：prepare/build/compile/execute 失败不发布部分新 family。
- [ ] **M13 完整**：统一输出收集，不在 pipeline 侧按 feature 名称解析；declared-but-skipped 不发布。
- [ ] **C4**：Forward recipe、Shadow typed token、`appendTargetRequests` 全退出。
- [ ] **C5**：组合诊断、扩展用例、多 View、performance/churn 门禁与 skill 吸收。

## 不得漏掉

- [ ] M22 双管线多 View 实跑；历史 scope 单测不是 runtime 验证。
- [x] AO 关闭无专属 requirement/import/pass/新 publication（本轮 runtime 验证；View binding 表的 DS 预分配保留，属 C3 收口）。
- [ ] Finalize 仍执行（grading/Bloom off）；同 backing 续写、空 draw attachment 语义、Shared GPU inputs 不重复上传（C2/C3）。
- [ ] 旧灯光 GPU 字段映射、生成类型、预算和现有 shadow 策略用例随迁移保留。
- [x] prepare 失败中止 recording（代码路径）；[ ] 完整 failure injection 仍待补。
- [ ] 不并行推进 `rdg-cache-dx` 旧 P3/P4 或旧 orchestrator 改名批次。
- [ ] `rdg-cache-dx` P5 未完成前，不宣称 shader layout 单源化已完成。

## 当前阻塞 / 未验证

无架构方向阻塞。待实测：backend 多 View fixture（M08）、画面重复性基线（≥2 次抓帧）、性能 before/after（M23）。工作区 `Engine/Plugins/log.cc` 为灯光线改动，未纳入本线提交。若实际实现违反 `plan.md` §8，应停止并复审，不臆造下一任务。
