# Progress

## 当前状态

- 阶段：R0 基线审计已完成；R1 契约小步已完成，尚未进入多 View 迁移。
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
| R1 World/View 分离 | 进行中（契约小步完成） | RenderFrameData 仍兼容单 View；旧消费者未迁移 | 场景抽取、View sorting、shadow preparation 拆分 |
| R2 ViewFamily | 未开始 | Forward/Deferred topology、当前 submit 约束 | 多 View record、双 Surface 验收 |
| R3 GUI2D/GameUI | 未开始 | WidgetTree live source、UIFrameSnapshot 输入 | UI-only 与 GameUI[ViewId] |
| R4 性能收口 | 未开始 | 优化由 profile 触发 | cache、submit、第三 pipeline 决策 |

## 下一轮接力点

R0 已完成。下一轮先设计 WorldFrameSnapshot / RenderViewInput 的字段和所有权，再修改 RenderFrameData 或 RenderRuntime::FrameInput。

## R1 审计结论

RenderFrameData 当前不是一个可以整体搬家的 world snapshot：

- RenderDrawItem 的 world transform、mesh/material 引用和 entity id 可成为共享 candidate；sortKey 与 bucket 顺序依赖 Camera/pipeline，应下沉到 View preparation。
- skinning palette 是可共享的一帧结果，但 palette index 必须在多个 View 间稳定。
- 原始灯光参数可共享；directional cascade/shadow view-projection 当前依赖 Camera，属于 View preparation。
- RenderFrameData 的 view/projection/viewProjection/cameraPos/viewportExtent/viewOwner 应迁移到 View 输入；目前仍由 CameraFrameInput 同时携带，不能删除旧字段直到所有 stage 迁移。
- 现有消费者覆盖 Forward、Deferred、shadow、entity-id、debug overlay 和 AppRenderState per-flight storage，直接拆 struct 会同时改变资源生命周期和 pass 输入。

R1 尚未完成代码迁移。下一步应先落一个真实的未排序 world candidate + view sorting 内部切片，并用两个不同 Camera 对同一 world candidate 的结果测试验证，而不是先添加空的 snapshot facade。

R1 第一小步已完成：WorldFrameSnapshot 显式承载当前可识别的 world lights、draw buckets 和 skinning palettes；RenderFrameData 作为兼容容器继承它并继续保留 camera、viewport、frame metadata。该步没有迁移消费者，也没有改变 shadow matrix、排序或 pipeline 行为；下一步仍必须落地真实的 world candidate 与 View preparation 切片。
