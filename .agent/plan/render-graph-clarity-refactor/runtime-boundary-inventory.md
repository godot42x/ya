# RenderGraph 运行边界与调用链 Inventory

时间：2026-08-02

## 1. 一帧的 command buffer 生命周期

```text
RenderRuntime::renderFrame
  -> prepareFrame
     -> beginFrameCommandBuffer
        -> IRender::begin(&imageIndex)
        -> swapchain image-index 对应 ICommandBuffer::reset/begin
  -> active pipeline tick
     -> world graph / pipeline-local graph 录制
  -> presentation graph 录制
  -> graph 外 presentation capture callback
  -> submitFrame
     -> ICommandBuffer::end
     -> IRender::end(imageIndex, commandBuffer)
     -> backend submit/present
```

代码锚点：

- `Engine/Source/Runtime/Rendering/RenderRuntime.cpp`
- `Engine/Source/Runtime/Rendering/RenderRuntimeFrame.cpp`

约束：

- RenderGraph 只负责 command recording 期间的 pass/resource 编排。
- swapchain acquire、queue submit、present 不建模为普通 graph pass。
- graph callback 不得调用 `waitIdle()`、创建资源或替换正在录制的 GPU resource。

## 2. Deferred executor 调用链

主入口：

```text
RenderRuntime::beginViewportPassAndTickPipeline
  -> DeferredRenderPipeline::tick
     -> DeferredRenderPipeline::executeDeferredMainGraph
        -> build one RenderGraph
        -> _graphExecutor->prepare(graph, compiled)
        -> stage resource prepare / resolved resource 回灌
        -> _graphExecutor->executeCompiled(graph, compiled, cmdBuf)
```

当前主图 pass 顺序：

```text
Shadow append
  -> Deferred GBuffer
  -> SSAO append
  -> Deferred Light
  -> Deferred Skybox
  -> Bloom graph passes
  -> Scene Overlay
  -> Viewport Overlay
  -> Postprocess finalize
```

当前过渡点：

- `prepare()` 与 `executeCompiled()` 之间仍有 Stage prepare 和 resolved image 回灌。
- attachment rendering desc 仍在 execute lambda 内。
- SSAO、Postprocess、Shadow 等模块仍可能拥有局部 graph/executor。

代码锚点：

- `Engine/Source/Runtime/Rendering/Deferred/DeferredRenderPipeline.cpp`
- `Engine/Source/Runtime/Rendering/Deferred/SSAOStage.cpp`
- `Engine/Source/Runtime/Rendering/Common/PostProcessingStage.cpp`
- `Engine/Source/Runtime/Rendering/Common/Shadow/`

## 3. Forward executor 调用链

当前 Forward viewport 图：

```text
ForwardRenderPipeline::tick
  -> executeViewportPassGraph
     -> Shadow appendGraphPasses
     -> Forward Viewport graph pass
        -> ForwardViewportStage::execute
        -> viewport overlays
     -> _graphExecutor->execute(graph, cmdBuf)
  -> finalizeViewportPass
     -> PostProcessingStage::execute (独立/图外路径)
```

结论：

- Forward shadow group 和 viewport 外壳 graph-backed。
- PBR、Phong、Unlit、Skybox、Debug 等主 surface 仍由 `ForwardViewportStage` 固定顺序执行。
- 本子计划不能把 Forward 主 surface 迁移算作 executor contract 阶段的完成条件。

代码锚点：

- `Engine/Source/Runtime/Rendering/Forward/ForwardRenderPipeline.cpp`
- `Engine/Source/Runtime/Rendering/Forward/ForwardViewportStage.cpp`
- `Engine/Source/Runtime/Rendering/Forward/ForwardViewportLitPasses.cpp`
- `Engine/Source/Runtime/Rendering/Forward/ForwardViewportAuxPasses.cpp`

## 4. Presentation graph 边界

Presentation 使用每个 swapchain image 一个 `RenderGraphExecutor`：

```text
RenderRuntime::renderPresentationPass
  -> select current presentation image/executor
  -> import swapchain image with PresentSrcKHR final layout
  -> Presentation graph pass
     -> beginColorRendering
     -> presentation postprocess
     -> presentation extensions
     -> endRendering
  -> presentationExecutor->execute(graph, cmdBuf)
  -> graph 外 recordPresentationCapture callback
```

结论：

- Presentation graph 与 world graph 保持独立。
- capture callback 当前仍在 graph 外录制，后续由主计划调查是否 graph-declared。
- 不应为了“统一一张 graph”把 acquire/present/ImGui 边界塞进 world graph。

代码锚点：

- `Engine/Source/Runtime/Rendering/RenderRuntime.Resources.cpp`
- `Engine/Source/Runtime/Rendering/RenderRuntimeFrame.cpp`
- `Engine/Source/Runtime/Application/Utility/AppScreenshotCapture.cpp`

## 5. Offscreen utility 边界

`OffscreenTaskService` 使用独立 command buffer、独立 submit 和 fence：

```text
OffscreenTaskService::tick
  -> reset/begin offscreen command buffer
  -> AppTaskManager::updateOffscreenTasks
  -> end
  -> submitToQueue(..., fence)
  -> 下一次 tick 等 fence
  -> finalizeCompletedJobs
```

结论：

- 这是独立的 utility/job completion boundary，不是 world RenderGraph 的局部 executor。
- 本计划不把它合并进 `RenderRuntime` world executor。
- 若未来要 graph 化 offscreen job，必须单独设计 queue/fence ownership，不得直接套用 world-frame contract。

代码锚点：

- `Engine/Source/Runtime/Rendering/Services/OffscreenTaskService.cpp`

## 6. Owner 映射

| 责任 | 当前 owner | 本子计划 | 主计划 |
|---|---|---|---|
| graph declaration/compile/execute contract | RenderGraph / Executor | 负责 | 消费 |
| persistent stable key | 当前缺失 | 只提供适配 seam | FG-101/102 |
| transient lifetime/physical slots | 当前缺失 | 只提供 compiled metadata seam | FG-106~FG-110 |
| per-flight upload arena | Stage/pipeline 分散 | 不负责 | FG-111 |
| Deferred FrameResourceSet | Stage 分散 | 不负责 | FG-201~ |
| Forward 主 surface migration | Stage 固定顺序 | 不负责 | 主计划后续阶段 |
| presentation acquire/submit/present | RenderRuntime/backend | 保持边界 | 主计划 P4/P5 调查 |
| offscreen submit/fence | OffscreenTaskService | 保持 utility 边界 | 独立调查 |

## 7. 尚未完成的运行时证据

本 inventory 是代码路径审计，不等同于 FG-002 的运行时基线。
仍需单独执行固定机位、graph dump、draw count、validation 和 pipeline-switch smoke，
因此不提前将主计划 `FG-001/FG-002` 标记为完成。
