# Progress

## 当前状态

- 阶段：R0 基线审计已完成，R0 的代码门禁已补齐；尚未进入 R1。
- 已确认：RenderFrameInputs.h 已有四组输入；RenderFrameData 与 RenderFrameExtractor 仍混合场景级和 View 级数据；RenderRuntime 仍按单 View 记录。
- 已确认前置：多 OS window 的 surface/present 改造属于 gui-multi-os-window-editor，不在本计划重复实现。
- 本轮新增一条 RenderRuntimeSnapshotTest 时序回归；未修改运行时实现。

## R0 真实调用链

~~~text
GameRuntimeFrameOrchestrator::tickRender
  -> resolve frameState / viewport rect / camera matrices
  -> RenderFrameExtractor::extract(scene, camera input, frameData)
       -> extractCamera
       -> extractLights (directional/point/shadow fit)
       -> extractDrawItems (mesh/material/skinning)
       -> sortDrawItems
  -> GameUIHost::buildSnapshot()                 [runtime/simulation only]
  -> acquirePresentFrame(primary surface)
  -> RenderRuntime::renderFrame(FrameInput)
       -> apply pending pipeline changes
       -> prepare Render2D overlay/UI pipelines
       -> prepareFrame / begin command buffer
       -> renderWorldFrame
            -> active Forward/Deferred pipeline tick
                 -> pipeline-specific graph build/record
       -> recordCameraViewCompose
            -> Runtime UI snapshot compose to camera display RT
            -> editor/module view compose callback
       -> PresentationGraphService::recordDisplayCompose
            -> display compose to surface swapchain image
       -> end command buffer
  -> submitPresentFrame(surface, command buffer)
~~~

R0 结论：world snapshot 与 UI snapshot 都在 renderFrame 前生成；RenderRuntime 只接收 FrameInput 和 immutable snapshot 指针；ViewCompose 先于 DisplayCompose；acquire/present 仍由 host coordinator 负责；当前实现仍是单 Camera、单 command buffer/submit。

## Checkpoint

| Checkpoint | 状态 | 保留项 | 未完成 |
| --- | --- | --- | --- |
| R0 单 View 基线 | 已完成 | 单 View、现有 pass、单 submit、Forward/Deferred topology | 真实 GPU golden 仍依赖可运行窗口环境 |
| R1 World/View 分离 | 未开始 | RenderFrameData 短期 adapter | 场景抽取与 View 准备拆分 |
| R2 ViewFamily | 未开始 | Forward/Deferred topology、当前 submit 约束 | 多 View record、双 Surface 验收 |
| R3 GUI2D/GameUI | 未开始 | WidgetTree live source、UIFrameSnapshot 输入 | UI-only 与 GameUI[ViewId] |
| R4 性能收口 | 未开始 | 优化由 profile 触发 | cache、submit、第三 pipeline 决策 |

## 下一轮接力点

R0 已完成。下一轮先设计 WorldFrameSnapshot / RenderViewInput 的字段和所有权，再修改 RenderFrameData 或 RenderRuntime::FrameInput。
