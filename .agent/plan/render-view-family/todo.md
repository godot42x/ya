# TODO

## R0

- [x] 画出 GameRuntimeFrameOrchestrator、RenderFrameExtractor、RenderRuntime、Forward/Deferred、ViewCompose、DisplayCompose、Present 的真实调用图。
- [x] 核对 RenderGraph build/execute 的 snapshot 时序和 live-state 访问。
- [ ] 登记并修复影响 R0 的 GUI widget test include/target 配置。
- [ ] 添加单 Camera golden：矩阵、离屏 extent、output format、surface imageIndex。
- [ ] 添加 resize、surface recreate、zero-extent、关闭单窗场景。
- [x] 记录 Forward 与 Deferred 当前 pass 顺序，不改策略。

## R1

- [x] 设计 SceneFrameSnapshot 字段分类和所有权方向，确认 resource generation/lifetime 风险。
- [x] 设计 RenderViewInput 的现有来源与兼容迁移边界；RenderViewFamily/Output 留待真实多 View 切片。
- [x] 引入 SceneFrameSnapshot 的实际存储边界，RenderFrameData 仅作为现有 per-view pipeline packet。
- [x] 将 shadow/cascade 字段从 SceneFrameSnapshot 移到独立 per-view preparation。
- [x] 将 RenderFrameExtractor 拆为 Scene extraction 与 View preparation 两个显式阶段。
- [x] 通过显式 TerrainProcessor 输入移除 extractor 对全局 App 的依赖。
- [x] 将 directional shadow/cascade preparation 从共享 Scene snapshot 的可变字段迁移到 per-view preparation；Scene snapshot 只保存 view-independent light source data。
- [x] 定义 SceneId、SceneRenderRequest、SceneViewportTask、SceneRenderScheduler 的最小契约。
- [x] 实现 frame-local submit/seal/clear 调度和按 SceneId 去重 snapshot builder。
- [x] 将 snapshot table 提升为 SceneRenderPlan 所有者，以 snapshotIndex 供多个 viewport task 复用。
- [x] 用 sceneRevision 防止同一 Scene 内容变化后错误复用旧 snapshot。
- [x] 在 snapshotFor() 校验 snapshotIndex 对应的 SceneId/revision，拒绝错误 task 索引。
- [x] 将 SceneRenderScheduler 的 snapshot builder 接到真实 Scene extractor，并让 plan snapshot 直接进入当前单 View preparation。
- [ ] 扩展 SceneRenderScheduler request collection 到 GameEditor/preview 的多个 View。
- [ ] 在 UI GPU compose 之前聚合 SceneRenderRequest，并禁止 UI paint/compose 期间临时抽取 Scene/ECS；不强制 UI widget tick 的相对顺序。
- [x] 迁移 shadow/entity-id/debug/Forward/Deferred 消费者到显式 Scene snapshot / per-view 字段。
- [x] 迁移 RenderFrameData 消费者到显式 `sceneSnapshot` + per-view 数据，并删除 SceneFrameSnapshot 继承关系。

## R2

- [x] 扩展 RenderRuntime::FrameInput 为 SceneRenderPlanInput，并在录制前校验 plan/task/snapshot 归属。
- [x] 为 FrameUploadArena 增加 frame token 防护，拒绝同一 submission 内重复 rewind flight backing。
- [x] 将 FrameUploadArena 同一 token 的 begin 改为幂等追加语义，避免多 View 准备时重置 cursor；descriptor binding 隔离仍留在后续切片。
- [x] 原子迁移 RenderFrameData：Scene snapshot 改为 shared_ptr<const SceneFrameSnapshot>，并使用 View-owned draw bucket 副本承载 camera-dependent sort；禁止旧 sortDrawItems() 修改共享 snapshot。
- [x] 将 View-owned draw bucket 副本替换为 index/order ranges，消除 RenderDrawItem 的重复拷贝；DrawCandidateView 提供 indexed read-only range。
- [x] 引入 RenderSubmissionContext / RenderViewRecordingContext；ForwardFrameResourceSet 提供 beginSubmission/beginView，同一 submission 的每个 View 拥有独立 frame descriptor set 与 upload slice。
- [x] 抽取 ViewDescriptorSetAllocator 与 beginFrameResourceSubmission/writeUploadSlice；Forward 与 Deferred 共用 pool 增长和 submission 开头，不合并 payload struct。
- [x] 将 Deferred 的 per-flight descriptor binding 改为 View-owned beginSubmission/beginView；SSAO/skybox 写入同一 View slot。
- [ ] 将 DrawPacket grouping/packet ranges 的生命周期纳入 submission/View context，确认录制期不缓存失效的候选 span。
- [x] 移除 Forward/Deferred pipeline 中跨 View 共享的 `_lastTickCtx` / `_lastFrameInput`，graph build 改用调用栈内 View-local post context。
- [ ] 明确并实现同 Scene 复用表：snapshot/candidates/skinning/light sources 共享；visibility/sort/shadow/targets 按 View 生成。
- [ ] 让同一逻辑帧的多个 surface/window 共用一个 SceneRenderScheduler/SceneRenderPlan，避免按窗口重复抽取同一 Scene。
- [x] 将 RenderRuntime 持久状态保活到 submit/fence。
- [x] 将 Shadow 的 per-flight descriptor binding 改为 View-owned beginSubmission/beginView。
- [ ] 将 PointShadow indirect instance/cull 缓冲从 flight 轴改为 View-owned（若双 View 录制需要）。
- [x] 增加同一 submission 多 View 的 slot/slice identity 验证（RenderViewBindingTable + Forward/Deferred/Shadow writeViewPayloads）。真实 GPU descriptor write 与 output publish 隔离仍待录制切片。
- [x] 为每个 View 建立独立 output/extent/format 句柄。
- [x] 将 Forward/Deferred viewport persistent key / RT 改为 View-keyed，避免多 View 共用一份 GBuffer/color。
- [ ] 录制同一 Scene 的两个 View，共享一个 SceneFrameSnapshot。
- [ ] 录制两个 Scene 的两个 View，验证 snapshot 和资源生命周期隔离。
- [ ] 验证一个 View 到多个 Surface、多个 View 到一个 Surface。
- [ ] 验证 surface acquire/present/recreate 不进入 View pipeline。
- [ ] 只有在 trace 证明必要时再提出 submit 拆分。

### R2 reuse gates

- [ ] GameRuntime 不再提交固定 sceneRevision = 0；改为真实 Scene content generation。
- [ ] 禁止 prepareView() 对共享 Scene snapshot 做按值复制或原地排序。
- [ ] 为相同 Scene 的双 View 增加 snapshot pointer/index identity 验证。
- [ ] 为不同 camera 的双 View 增加 draw-order / shadow-preparation 非共享验证。

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
