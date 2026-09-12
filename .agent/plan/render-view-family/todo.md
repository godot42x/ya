# TODO

## R0

- [x] 画出 GameRuntimeFrameOrchestrator、RenderFrameExtractor、RenderRuntime、Forward/Deferred、ViewCompose、DisplayCompose、Present 的真实调用图。
- [x] 核对 RenderGraph build/execute 的 snapshot 时序和 live-state 访问。
- [ ] 登记并修复影响 R0 的 GUI widget test include/target 配置。
- [ ] 添加单 Camera golden：矩阵、离屏 extent、output format、surface imageIndex。
- [ ] 添加 resize、surface recreate、zero-extent、关闭单窗场景。
- [x] 记录 Forward 与 Deferred 当前 pass 顺序，不改策略。

## R1

- [ ] 设计 WorldFrameSnapshot 字段和所有权，确认 resource generation/lifetime。
- [ ] 设计 RenderViewInput、RenderViewFamily、RenderViewOutput 最小字段。
- [ ] 将 RenderFrameExtractor 的 world extraction 与 view preparation 分开。
- [ ] 迁移 shadow/entity-id/debug/Forward/Deferred 消费者。
- [ ] 为旧 RenderFrameData 建立短期 adapter，并记录删除条件。

## R2

- [ ] 扩展 RenderRuntime::FrameInput 为 family 级 additive API。
- [ ] 为每个 View 建立独立 output/extent/format 句柄。
- [ ] 录制两个 View，共享一个 WorldFrameSnapshot。
- [ ] 验证一个 View 到多个 Surface、多个 View 到一个 Surface。
- [ ] 验证 surface acquire/present/recreate 不进入 View pipeline。
- [ ] 只有在 trace 证明必要时再提出 submit 拆分。

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
