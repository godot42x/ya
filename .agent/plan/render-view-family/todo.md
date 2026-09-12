# TODO

## R0

- [x] 画出 GameRuntimeFrameOrchestrator、RenderFrameExtractor、RenderRuntime、Forward/Deferred、ViewCompose、DisplayCompose、Present 的真实调用图。
- [x] 核对 RenderGraph build/execute 的 snapshot 时序和 live-state 访问。
- [ ] 登记并修复影响 R0 的 GUI widget test include/target 配置。
- [ ] 添加单 Camera golden：矩阵、离屏 extent、output format、surface imageIndex。
- [ ] 添加 resize、surface recreate、zero-extent、关闭单窗场景。
- [x] 记录 Forward 与 Deferred 当前 pass 顺序，不改策略。

## R1

- [x] 设计 WorldFrameSnapshot 字段分类和所有权方向，确认 resource generation/lifetime 风险。
- [x] 设计 RenderViewInput 的现有来源与兼容迁移边界；RenderViewFamily/Output 留待真实多 View 切片。
- [x] 引入 WorldFrameSnapshot 的实际存储边界，并保留 RenderFrameData 兼容容器。
- [ ] 将 WorldFrameSnapshot 语义迁移为 SceneFrameSnapshot/SceneRenderPlan，保留兼容期适配。
- [x] 定义 SceneId、SceneRenderRequest、SceneViewportTask、SceneRenderScheduler 的最小契约。
- [x] 实现 frame-local submit/seal/clear 调度和按 SceneId 去重 snapshot builder。
- [x] 将 snapshot table 提升为 SceneRenderPlan 所有者，以 snapshotIndex 供多个 viewport task 复用。
- [x] 用 sceneRevision 防止同一 Scene 内容变化后错误复用旧 snapshot。
- [x] 在 snapshotFor() 校验 snapshotIndex 对应的 SceneId/revision，拒绝错误 task 索引。
- [ ] 将 RenderFrameExtractor 的 Scene extraction 与 View preparation 分开。
- [ ] 在 UI GPU compose 之前聚合 SceneRenderRequest，并禁止 UI paint/compose 期间临时抽取 Scene/ECS；不强制 UI widget tick 的相对顺序。
- [ ] 迁移 shadow/entity-id/debug/Forward/Deferred 消费者。
- [ ] 为旧 RenderFrameData 建立短期 adapter，并记录删除条件。

## R2

- [ ] 扩展 RenderRuntime::FrameInput 为 SceneRenderPlan/SceneViewportTask additive API。
- [ ] 为每个 View 建立独立 output/extent/format 句柄。
- [ ] 录制同一 Scene 的两个 View，共享一个 SceneFrameSnapshot。
- [ ] 录制两个 Scene 的两个 View，验证 snapshot 和资源生命周期隔离。
- [ ] 验证一个 View 到多个 Surface、多个 View 到一个 Surface。
- [ ] 验证 surface acquire/present/recreate 不进入 View pipeline。
- [ ] 只有在 trace 证明必要时再提出 submit 拆分。

## 设计评估门禁

- [x] 对照 UE Scene/FSceneRenderer/ViewFamily、Unity Camera/ScriptableRenderContext、Godot Viewport/SubViewport、ImGui draw data。
- [x] 明确 Scene owner 不直接依赖 RHI；Scheduler 属于 Render3D orchestration，不属于 GUI Framework。
- [x] 明确 UI 之前只指 UI GPU compose 之前。

## R3

- [ ] UI Editor 无 world render 路径。
- [ ] GameUIHost[ViewId] 的 tree/input/focus/snapshot 生命周期。
- [ ] 同一 View 多 Surface 复用 snapshot。
- [ ] 不同 View 不共享 live WidgetTree。
- [ ] 验证 UIFrameSnapshot 在 graph build 前生成，录制期不读 live tree。

## R4

- [ ] 汇总 CPU/GPU/frame trace 与资源峰值。
- [ ] 决定是否引入 culling/sort/cache。
- [ ] 决定是否拆 world/view 与 display/present submit。
- [ ] 评估第三种 pipeline 的接入契约，不新增强制 BaseRenderPipeline。
