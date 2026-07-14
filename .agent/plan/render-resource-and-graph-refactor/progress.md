# GPU 资源模型与 RenderGraph 联合重构进展

本文件承接 `todo.md` 中不再适合作为待办项维护的阶段进展、迁移备注和调查结论。

## 当前状态

- 资源模型层：buffer factory 迁移已基本完成，image/view/sampler 新工厂与 ownership 规则已成型
- 状态跟踪层：`ResourceStateTracker` 已成为统一收口方向，legacy layout 真相已开始退场
- graph core：已有 declaration/compiler/registry/executor 骨架，并已能承接真实 utility/runtime pass
- runtime 主线：Deferred 已进入 graph 主导的渐进迁移阶段，但仍残留 legacy attachment owner、dirty state 与部分 graph 外语义
- 当前风险：startup/runtime 暴露的问题已经从“简单崩溃”转向“submit-time lifetime、imported subresource state、replacement 边界”这类真实主路径约束

## 当前阻塞

- Deferred 主链尚未完全由 graph 接管 pass 顺序、intermediate owner 与 barrier/state
- `IRenderTarget` 仍兼具 attachment owner 和 compatibility facade 的双重职责
- offscreen/environment preprocess 虽已 graph-backed，但仍需继续验证 imported subresource range、executor 生命周期和 final state 契约
- graph registry replacement 已可用，但还需要继续压实与 runtime frame boundary、deferred deletion 和 startup/shutdown 的一致性

## 下一步

1. 以启动链和 `HelloMaterial` smoke 为主，继续清掉 runtime 中暴露的 graph/resource-state/lifetime 问题
2. 完成 Deferred GBuffer/viewport/pipeline 收口，让 graph 成为主链单一事实源
3. 再推进 `IRenderTarget` 收敛、Forward graph 和外围 GPU 工作流迁移

## 最新验证

- 2026-07-14：editor/presentation screenshot automation 链现在也改为优先消费 `RenderImage*`，不再把 presentation source 当成 `Texture*` 在 automation/request/record 边界上传递。`AppAutomationFrameContext`、`AppScreenshotCapture::{request,recordPresentationCapture}`、`AppFrameLoop` 与 `RenderRuntime` 新增的 `getPresentationImage()` 已统一收口到 presentation color attachment image。
- 直接收益：automation/editor screenshot 这条链的 presentation source 和 viewport/postprocess 主路径终于回到同一种 owner 语义，不再额外依赖 compat `Texture::wrap()` 作为截图输入真相；后续若继续削减 `_screenRT` 的 facade 职责，也不用再先补这一段生命周期桥接。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：Deferred/Forward 上没有消费者的 concrete RT accessor 已继续删除。`DeferredRenderPipeline::getGBufferRT()`、`DeferredRenderPipeline::getShadowDepthRT()` 和 `ForwardRenderPipeline::getShadowDepthRT()` 这类只暴露 legacy owner、但当前 runtime/debug 路径已不再使用的逃生口已移除。
- 直接收益：这一步不改行为，但继续压缩了 concrete pipeline 直接把 `IRenderTarget` 暴露给外层的面积，减少后续 agent 或新调用点再次绕开 pipeline frame state / debug outputs 的机会。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：Deferred 的 RT Editor 现在也开始优先消费 pipeline/export 的 attachment format 元数据，而不是直接回读 `_gBufferRT/_viewportRT` 的 attachment desc。`RenderTargetEditorCatalog::Entry` 新增可选 `colorFormats/depthFormat`，Deferred catalog 会用 pipeline-owned spec 填充它们，GUI 仅把 RT 保留给 preview/extent/framebuffer-count 这类兼容展示。
- 直接收益：Deferred 的 editor/debug 路径也开始遵守“pipeline spec 才是真相”的规则，避免把刚收口的 attachment format truth 又从 GUI 层绕回 legacy RT desc；同时 Forward / Presentation / Shadow 仍可继续走默认 RT 回退，不会把这一步扩大成全局 UI 重构。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：Deferred attachment refresh 主路径现在不再走 `IRenderTarget` 的 dirty/mutation 自修复协议。viewport resize、shared depth format 和 RT editor attachment format 修改在 frame boundary 会直接按 `_gBufferRTSpec/_viewportRTSpec` 显式重建 `_gBufferRT/_viewportRT`，而不是先改 RT 内部 desc、再依赖 `refreshIfNeeded()` 追认。
- 直接收益：Deferred 主流程对 legacy `needsRefresh()/refreshIfNeeded()/set*AttachmentFormat()/setExtent()` 的依赖进一步缩小，pipeline-local spec 和 frame-boundary replacement 开始成为真正的控制面；这比继续做 facade 小收口更接近后续“RT 只剩 owner/replacement compatibility 层”的目标。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：Deferred 的 attachment specification 现在开始由 pipeline 自己持有，而不是继续散落在 `initRenderTargets()` 内联描述和 `IRenderTarget` 的运行时 mutation 里。`DeferredRenderPipeline` 新增 `_gBufferRTSpec/_viewportRTSpec`，初始化、viewport resize、shared depth format 和 RT editor color-format 修改都会先更新 pipeline-owned spec，再同步到 legacy RT 并在 frame boundary 刷新 snapshot / stage state。
- 直接收益：当前帧 snapshot formats、`getViewportColorFormat()/getViewportDepthFormat()` 与 RT editor 改动终于回到了同一份事实源；之前“RT desc 已改、pipeline snapshot 仍看旧的 `_gBufferSignedLinearFormat/_viewportColorFormat/_sharedDepthFormat`”这类分叉路径被收掉了，也为后续继续把 `_gBufferRT/_viewportRT` 压缩成 owner/replacement facade 留出了更明确的边界。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：`VulkanImageView` 现在在创建时持久记录自身 `ImageSubresourceRange` 元数据；复用已有 shared subresource view 的 imported graph 资源不再需要在 helper/调用点额外手抄第二份 range。OpenGL command buffer 也同步在 `begin()/reset()` 清理 retained resources，保证跨后端的提交期保活契约一致。
- 直接收益：graph compiler、registry 和 runtime helper 在“已有 image view 直接导入”路径上能稳定看到真实 mip/layer/aspect 范围；同时 retained-resource 生命周期规则不再只在 Vulkan 后端成立。
- 2026-07-14：dynamic rendering attachment 现在开始显式携带 owner token。`RenderingInfo::ImageSpec` 可随 attachment 一起保留 shared `image/imageView` 和额外 retained resources，Vulkan command buffer 在 `beginRendering()` 时统一保活这些 owner；render-target 路径也会把当前 framebuffer 的 `RenderImage` attachments 保活到 submit 完成。
- 直接收益：类似 `MVKAttachmentDescription -> vkQueueSubmit` 这类“record 时合法、submit 时 attachment/view 已失效”的问题，不再只能依赖外层调用者自己记得保活；同时 `validateRenderingImageSpec()` 现在会额外校验 retained owner 与 subresource range 是否和实际 view 一致，崩溃时能更快打到具体 attachment。
- 2026-07-14：尝试移除 Deferred shadow pass 后的全图 depth handoff 时，400 帧 smoke 暴露了新的真实约束：shadow pass 只会写当帧需要的 layer，但 lighting descriptors 仍可能采样更宽的 point-shadow layer 集。未写 layer 若没有被显式 handoff 到 `ShaderReadOnlyOptimal`，validation 会在 `vkQueueSubmit` 报 `VUID-vkCmdDraw-None-09600`，指出 sampled descriptor 命中的 layer 仍是 `Undefined`。
- 当前结论：这条 handoff 还不能直接删除。要彻底移除，至少需要先收紧 shadow descriptor 的实际 layer 覆盖范围，或把 shadow image 的未写子资源初始化/维持到可采样布局，并把这一契约显式纳入 graph/resource-state 模型。
- 2026-07-14：runtime/editor/automation 对 Deferred viewport 主输出的消费链开始脱离 `IRenderTarget -> Texture` 兼容出口。`IRenderPipelineDebugOutputs` 新增 `getViewportOutputImage()`，Deferred 通过 `_currentViewportResources.color` 直接导出 viewport scene color；editor viewport 与 automation screenshot 现在都会优先消费 `RenderImage*`，只有 Forward 或遗留路径缺失时才回退到 `Texture*`。
- 直接收益：`_viewportRT` 不再继续被 runtime 侧默认为“最终 viewport 输出身份”，Deferred viewport color 的 snapshot/export 语义比过去更接近 graph/imported resource 真相；同时 automation/editor 对 compat `Texture::wrap()` 的依赖进一步缩小，为后续继续削减 `_viewportRT` 的 legacy facade 职责留出了更清晰的边界。
- 2026-07-14：Forward viewport snapshot 现在也开始同步导出 `RenderImage*`。`ForwardViewportResources` 除了 compat `Texture*` 之外，会一并缓存 color/depth/resolve attachment 的 `RenderImage*`；`ForwardRenderPipeline::getViewportOutputImage()` 直接返回当前 scene viewport image，旧的每帧赋值 `viewportTexture` 成员已删除。
- 直接收益：automation/editor 获取 active viewport 输出时，不再因为 Forward 缺少 `RenderImage*` 导出而被迫退回 `Texture*`；与此同时 Forward 也少了一份容易与当前 RT snapshot 脱节的 raw texture 状态，继续向“snapshot 才是真相，compat texture 只是 facade”收口。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：Forward viewport snapshot 继续补齐显式 attachment owner。`ForwardViewportResources` 现会缓存 `color/depth/resolve` 的 shared `RenderImage` owner，并统一 `syncRawViews()` 生成 raw image 指针；这让 Forward 在导出 viewport image 的同时，也把当前帧 attachment 的保活边界一起前移到 pipeline snapshot。
- 直接收益：Forward 不再只是在 RT 还活着时“碰巧能拿到 raw image 指针”，而是显式持有当前帧 viewport attachment owner。后续如果继续削弱 `viewportRT` 的 legacy owner/facade 职责，viewport image/debug/automation 这条链不需要再倒回去补生命周期。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：non-editor automation viewport 截图链已不再保留 `Texture*` fallback。`AppAutomationFrameContext`、`AppFrameLoop`、`AppScreenshotCapture::request()` 和 `RenderRuntime` 的公共 viewport 接口中，`viewportTexture/getViewportTexture()/getActiveViewportTexture()` 已移除；runtime/editor/automation 现在统一优先消费 `RenderImage*` viewport/postprocess 输出，editor screenshot 仍单独保留 `presentationTexture`。
- 直接收益：这条热路径不再同时维护 `RenderImage*` 与 `Texture*` 两套 viewport 输出契约，Forward/Deferred 既然都已经能稳定导出 viewport image，就不需要再让 automation 为兼容接口兜底。剩余 `Texture*` 主要收缩到 presentation/editor screenshot 与少量 debug depth/compat facade。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：Deferred attachment snapshot 现在不再在 `beginTick()` 每帧无条件从 `_gBufferRT/_viewportRT` 回读。`_currentGBufferResources/_currentViewportResources` 改为只在初始化和显式 pending refresh / replacement 路径里更新，`beginTick()` 只消费已有 snapshot 并断言 legacy attachment dirty 没有越过 pipeline-local refresh 边界。
- 直接收益：Deferred 的 frame state 更接近“pipeline-local owner/replacement/snapshot”协议，而不是“每帧再向 legacy RT 重新询问当前资源”；后续若继续把 `_gBufferRT/_viewportRT` 收缩成纯 owner/replacement facade，stage/graph 侧已经更少依赖它们作为实时真相来源。
- 2026-07-14：Deferred 对外仍需提供的 compat viewport/depth `Texture*` 现在也开始从 snapshot 派生，而不是再通过 `_viewportRT->getCurrent*Texture()` 回查 framebuffer/RT。pipeline 在 `refreshViewportSnapshot()` 时同步重建 `_viewportTextureCompat/_viewportDepthTextureCompat`，editor fallback、automation fallback 和 debug depth 兼容出口都改为消费这组 snapshot-backed compat texture。
- 直接收益：Deferred 的 compat 输出也开始与 `_currentViewportResources` 保持同一事实源，`_viewportRT` 继续向“只负责 owner/replacement”收缩；即使后续继续削弱 RT facade，高层仍然可以通过同一份 snapshot 维持 `RenderImage*` 主路径和 `Texture*` 兼容路径的一致性。
- 2026-07-14：Deferred pipeline 内部残留的裸 `viewportTexture` 成员已删除，`getViewportTexture()` 直接回退到 `_viewportTextureCompat`；同时 automation 帧上下文只会在 active pipeline 缺失 `viewportImage` 时才再填充 `viewportTexture`。这让 Deferred 的 `Texture*` 输出明确退化为 compat fallback，而不是再和 `RenderImage*` 主路径并行维护两份 viewport 真相。
- 直接收益：Deferred 不再额外保存一份可能与 snapshot 脱节的 raw texture 指针，automation/screenshot 也不会在已经拿到 `RenderImage*` 时继续把 compat `Texture*` 当成同级输入。当前主路径的 viewport 输出语义因此更接近“RenderImage first, Texture fallback only”。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：Deferred viewport depth snapshot 现在明确声明为“借用 GBuffer shared depth”，而不是继续假装由 `_viewportRT` 自己拥有。`buildDeferredViewportResources()` 改为显式接收 `DeferredGBufferResources`，`setSharedDepthFormat()` 不再修改 `_viewportRT` depth format，shared-depth refresh 也不再触发 viewport RT attachment flush。
- 直接收益：Deferred pipeline 内部的 depth owner 边界与当前实现真相终于对齐了。viewport RT 现在更接近“只拥有 color attachment 的 compat facade”，而 pipeline snapshot 会稳定从 GBuffer depth 继承 depth image 和 depth format，减少 shared-depth 变更时再次把 `_viewportRT` 当成真实 depth owner 的回退路径。
- 2026-07-14：`IRenderPipelineExecution::getViewportRT()` 和 `RenderRuntime::getActiveViewportRT()` 已移除。当前 runtime/debug/editor 主链已经分别走 `appendRenderTargetEditorEntries()`、`getViewportOutputImage()`、`getViewportDepthTexture()` 和 compat `getViewportTexture()`，不再需要通过公共 pipeline 执行接口继续暴露 viewport render target。
- 直接收益：pipeline 公共执行接口少了一条纯 legacy 的 `IRenderTarget` 逃生口，后续继续收口 RT owner / replacement 语义时，runtime 层不会再被旧的 viewport RT 转发链牵回去。
- 2026-07-14：Deferred pending refresh 现在开始显式区分 `GBufferAttachments` 和 `ViewportAttachments`。pipeline 在 `setRenderTargetColorFormat()` 时直接记录“下一帧要刷新哪一侧”，`applyPendingResourceRefreshes()` 不再通过 `needsAttachmentRefresh()` 反查 RT 内部 dirty 状态来决定是否刷新。
- 直接收益：Deferred 主流程对 legacy RT dirty 协议的依赖又缩了一层。RT 仍然负责具体重建，但“刷新目标是谁”已经先回收到 pipeline 自己的 frame-boundary refresh mask，后续继续把 replacement 从 RT 内部状态迁到显式 owner/bundle 时，控制流不需要再反向依赖 RT 自报脏。
- 2026-07-14：Deferred 当前帧 `GBuffer / Viewport` snapshot 现在开始显式保留 attachment owner。`DeferredGBufferResources` / `DeferredViewportResources` 除了原有 raw `RenderImage*` 视图，还会缓存 `shared_ptr<RenderImage>` owner；pipeline 从 `IRenderTarget` / `IFrameBuffer` 刷新 snapshot 时同步保存 shared owner，再派生 raw view 给现有 stage/graph 调用面继续消费。
- 直接收益：Deferred graph import、debug snapshot 和 stage frame state 看到的不再只是“从 RT 临时借来的裸指针”，而是一份由 pipeline 在 frame-boundary 持有的 attachment owner 快照。即使后续继续削弱 RT facade 或推进 replacement，当前帧资源的保活边界也已经先向 pipeline-local truth 靠了一步。
- 2026-07-14：Deferred snapshot 的 attachment formats 也开始直接取自 pipeline 自己的格式选择结果，而不再从 `_gBufferRT/_viewportRT` 的 attachment desc 回读。`refreshGBufferSnapshot()` / `refreshViewportSnapshot()` 现在分别用 `_gBufferSignedLinearFormat`、`LINEAR_FORMAT`、`SHADING_MODEL_FORMAT`、`_viewportColorFormat` 与 `_sharedDepthFormat` 组装当前帧 format snapshot。
- 直接收益：当前帧 snapshot 在“owner + format”两个维度上都更接近 pipeline-local truth，而不是继续把 RT desc 当成运行时事实源。后续若要把 snapshot 构建彻底切到 pipeline-owned attachment bundle，格式侧已经不再被 legacy RT 描述绑定。
- 2026-07-14：修复 imported/offscreen 图像的 compatibility seed 仍按“整张 image 单一 layout”推断的问题。`VulkanImage` 现在会持久记录按 aspect/mip/layer 的 compatibility layout，`ResourceStateTracker` 首次 seed 也会按 subresource 读取该状态。
- 直接收益：environment prefilter 这类“逐个 mip/face 离屏写入，随后整张 cubemap 再被导入采样”的路径，不会再因为 compatibility seed 丢失 mixed subresource state 而漏发 barrier。
- 2026-07-14：Deferred 当前帧 GBuffer / viewport snapshot 已从 `Texture*` 切到 `RenderImage*`。这让 deferred 主链 graph import 与 debug snapshot 直接使用 attachment owner，而不是经由 `Texture::wrap()` 兼容层回读当前附件；postprocess 输入暂时仍保留 `viewportRT -> Texture*` 兼容入口，避免把本次变更扩大成后处理接口重构。
- 2026-07-14：Deferred postprocess graph 输入已继续切到 `RenderImage*` attachment owner。`PostProcessingStage` / `BloomPostprocessing` 现在都支持直接导入 `RenderImage`，Deferred 不再经由 `viewportRT -> Texture*` 兼容输入进入 postprocess；同时 Bloom extract/composite 改为在 pass execute 时从 `RGRenderContext` 解析 scene image view，避免 graph 外捕获输入 view 指针。
- 验证结果：
  - `xmake b HelloMaterial` 通过
  - `xmake b ya-testing` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:OffscreenAsyncTest.*:AppAutomationConfigTest.*:ResourceStateTrackerTest.*'` 56/56 通过
  - `HelloMaterial --exit-after-frame=220 --log-level=warn --log-detail-level=error` 退出码 0，日志未再出现 `Validation Error|ERROR|ASSERT|SIGTRAP|EXC_`
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，日志未再出现 `Validation Error|ERROR|ASSERT|SIGTRAP|EXC_`

## 代码对账快照

本节基于当前主路径代码抽样复核，用于校正文档对现状的判断，不替代 `todo.md`。

### 资源模型 / Factory

- `IRenderResourceFactory` 已成为 buffer/image/view/sampler 的真实创建入口，buffer 迁移基本完成
- `RenderImage` 已作为中间 GPU image owner 稳定落地，`IImageView` 也已明确为 non-owning image projection
- 但 `Texture.cpp`、`IRenderTarget.cpp`、`FrameBuffer.cpp`、`Swapchain.cpp` 仍残留 `App::get()` 或 legacy factory 路径，说明“资源创建不依赖全局应用状态”尚未完成

### Resource State / Graph Core

- `ResourceStateTracker`、`RenderGraphExecutor`、`RenderGraphResourceRegistry` 已不再停留在设计稿，且已有独立 core tests 覆盖 generation handle、imported view range、replacement、executor smoke、render helper、state tracker 等契约
- `RenderGraphExecutor` 已能对 compiled texture/buffer state plan 发射最小 transition/barrier，并可驱动真实 runtime pass
- 但 executor 仍按“遍历 pass 时线性扫描全量 state plans”的最小实现工作，barrier backend 也还没有完全收口所有 legacy/manual 路径

### Deferred 主链

- Deferred 已经不是“少量试点 pass”，而是存在一个统一的 `executeDeferredMainGraph()`，将 GBuffer、SSAO、Deferred Light、Skybox、Scene Overlay、Viewport Overlay、Postprocess 串进同一张 graph
- `SSAOStage`、`BloomPostprocessing`、`PostProcessingStage` 都已改为持久化 `RenderGraphExecutor` + persistent graph texture 的真实 runtime 路径
- 但 Deferred 仍依赖 `_gBufferRT/_viewportRT` 提供 imported attachment，shadow handoff 也仍在 graph 外手动 `transitionImageLayoutAuto()`；这说明“graph 接管主链顺序”基本成立，但“graph 接管 intermediate owner / 外围 barrier 真相”尚未完成

### Forward / RenderTarget

- Forward pipeline 仍以 legacy dynamic rendering 路径为主：`executeViewportPass()` 直接 `beginRendering()/endRendering()`，后处理也仍是 graph stage 包裹在 legacy viewport pass 之后
- `ForwardRenderPipeline` 继续使用 `viewportRT` / shadow render target + pending refresh + `waitIdle()` 来处理 resize / shadow rebuild / attachment format 变化
- 这意味着 Forward 离“graph 主路径”还明显早于 Deferred；`IRenderTarget` 也依旧同时承担 attachment owner、dirty protocol 和 compatibility facade 三种职责

### 外围 GPU 工作流

- `PBRGenerateBrdfLUT`、`EquidistantCylindrical2CubeMap`、`CubeMap2PBRIrradianceMap`、`CubeMap2PBRPrefilteredEnv` 都已接入 graph-backed execute
- environment / skybox 预处理通过 `OffscreenJobState` 在 graph 外排队，但 job result 已显式保活 `RenderImage` 和 retained resources，说明 submit-time 生命周期问题已经被部分压实
- 但 offscreen scheduler 仍是 graph 外系统；`finalizeSubmittedOffscreenJobs()` 也还是“Recorded -> GpuCompleted”的保守简化模型，并未引入更精细的 GPU completion/fence 语义

### Runtime / Automation / Screenshot

- automation config、viewport resize、pipeline switch、shadow resolution 等 smoke 入口已有测试覆盖
- `RenderRuntime` presentation pass 已 graph-backed，但 `RenderRuntime::beginFrameCommandBuffer()` 仍然每帧 `waitIdle()`
- screenshot/readback 仍以手写 `transitionImageLayoutAuto()` + `bufferMemoryBarrier()` 为主，尚未迁到统一 graph copy/readback 工作流

### 当前最可信的结论

- 计划对 Deferred 主链、graph core、offscreen utility pass 的判断基本可信，且不少地方代码已比旧计划写得更靠前
- 计划对 Forward、RenderTarget、`App::get()` 残留、per-frame `waitIdle()` 和 screenshot 路径仍偏乐观，后续推进应优先把这些作为“未完成现实”而不是默认已接近收尾

## Phase 0 基线备注

- Codex 当前 macOS smoke 环境下，`HelloMaterial` 已可在补齐 Vulkan runtime env 后完成 `--exit-after-frame=3` 冒烟并正常退出；启动链上此前的 shadow cube layer 分配崩溃、Bloom/Postprocess/SSAO graph 悬空 handle 和 presentation graph layout/present 错误已被修复
- 近期两类真实生命周期问题已被压实并纳入基线：offscreen cubemap preprocess 的 executor/resource owner 已延长到 job 完成；dynamic rendering depth attachment 改为值语义 `std::optional<ImageSpec>`，避免 graph/helper 路径继续引用栈上或短生命周期 attachment 描述
- 针对异步加载与 submit-time 生命周期问题，当前有效 smoke 基线已提升为 `HelloMaterial --exit-after-frame=400`；短启动 smoke 仅用于发现立即崩溃，不再作为此类变更的唯一验收
- `smoke.viewportResize.{width,height,frame}` 与 `smoke.renderPipeline.{target,frame}` 现已接入 automation config，并在 `AppAutomationConfigTest` 中覆盖解析；运行期动作通过现有 editor pending viewport resize 与 `RenderRuntime::setPendingRenderPipeline()` 路径触发
- smoke 运行现已支持 `--log-level`、`--log-detail-level` 以及 `smoke.log.{level,detailLevel}`；后续 Codex/agent 冒烟默认使用 warn/error 降噪，并先过滤日志/文件再扩大读取范围
- `shadow.resolution` 现已补入 automation overrides，并与已有 `shadow.quality / directionalEnabled / pointLightEnabled / filter ...` 一起通过 `ShadowSettingsConfig` 进入 runtime shadow settings；`AppAutomationConfigTest` 已覆盖解析

## Phase 4 迁移备注

- `RenderImage` 仅组合并拥有 `IImage` 与 default `IImageView`，不承载资产语义、采样器或资源状态
- `RenderingInfo::ImageSpec` 已直接引用 image/view，dynamic rendering attachment 协议不再依赖 `Texture`
- BRDF LUT、Deferred SSAO 与 bloom intermediates 已迁移
- postprocess output 已由 `PostProcessingStage` 以 `RenderImage` 形式持有；剩余 `Texture::wrap()` 兼容层只保留在 pipeline viewport 输出侧，供 editor viewport / screenshot fallback 复用
- shadow sampled views 已由 `ShadowMapResources` 显式拥有；shadow pass 内残留的 `Texture::wrap()` attachment adapter 归入 Phase 8
- screenshot offscreen capture 已不再分配 scratch `Texture`；该链路现在直接从 source image copy 到 readback buffer，仅保留 viewport `Texture*` 作为 fallback 输入源，而不再作为中间 owner
- framebuffer/render-target backend 路径现已直接持有 `RenderImage` attachments；`Texture` 仅在 `IFrameBuffer` 兼容 getter 中按需懒创建，backend attachment 创建与 dynamic rendering 录制已不再直接依赖 `Texture::wrap()`
- `ImageViewCreateInfo` / `ImportedImageDesc` 已开始共享公共 identity/ownership 比较语义：registry 不再私有维护一套 image-view / imported-image 比较逻辑；`ImageViewDescKey` 已落到公共层并用于 imported texture replacement 判定，debug label 不再参与 view identity；`RGImportedTextureDesc` 现已可显式携带 shared `imageView` 与 keepalive resources，并由 registry 在复用物理资源时刷新 retained owner
- `RenderGraphImportUtils` 已收敛 Texture / RenderImage 默认 view imported-desc 组装，Deferred/SSAO/Bloom/Postprocess/BRDF LUT/Presentation graph 不再各自手写 `image + imageView + imported usage/finalLayout`；剩余 face/mip/layer 子资源导入继续保留局部显式 helper，直到子资源 view owner/desc 一并模型化
- imported 子资源路径现已进一步分层：`makeImportedSubresourceTextureDesc()` 同时覆盖“按 range 派生 view”和“复用已有 shared subresource view + 显式 subresource range”两类入口；Deferred directional shadow 与 cubemap utility pass 已迁到公共 helper，新增 `RenderGraphCoreTest.ImportedSubresourceHelperKeepsProvidedViewAndCompileRange` 锁住“registry 复用现成 view 且 compiler 看到正确 subresource range”的契约
- `IImageView` 现已保存自身的 `ImageSubresourceRange` 元数据；shared subresource view 导入可直接复用该 range，避免 helper / 调用点重复抄写第二份 range 描述，当前 deferred directional shadow 已切回“shared image + shared view”简单入口并保持 compile range 正确
- `IImageView` 现已改为仅保存 non-owning `IImage*`；`Texture`、`RenderImage`、shadow/editor runtime state 等显式 owner 继续负责 image 生命周期，新增 `RenderGraphCoreTest.ImageViewDoesNotOwnImageLifetime` 锁住该契约
- offscreen cubemap preprocess 链现已改为 `OffscreenJob -> RenderImage` 中间 owner：`EquidistantCylindrical2CubeMap` / `CubeMap2PBRIrradianceMap` / `CubeMap2PBRPrefilterEnv` 直接写 `RenderImage`，`ResourceResolveSystem` 仅在接管最终 skybox/environment 结果时再 `Texture::wrap()` 成可采样资产语义对象
- OpenGL command buffer 现在也会在 `begin()/reset()` 时清理 `retainedResources`，避免“Vulkan 会释放保活引用、OpenGL 一直累积上一帧 retained owner”的后端行为分叉

## Phase 5 迁移备注

- tracker 首次接触 image 时按 aspect/mip/layer 快照兼容 layout，避免局部 transition 污染未触及 subresource
- `IImage` 已降级为 `getCompatibilityLayout()` seed 语义；`VulkanImage` 不再暴露“当前 layout”接口
- `VulkanImage` compatibility layout 仅作为 tracker 首次接触 image/imported image 时的 seed；尚未表达 GPU 执行完成状态
- `BufferResourceState` / `ImageResourceState` 已作为公共状态载体落地，供 legacy barrier path 与后续 graph compiled state plan 共享
- `ResourceStateTracker` 内部已从裸 layout map 提升为 `ImageResourceState` map；现阶段对外仍保留 layout-oriented 兼容 API
- Vulkan command buffer tracked transition 现已写入最小 `ImageResourceState{stages, access, layout}`，不再只回填 layout
- `ImageLayoutTransition` 现已携带 `oldState/newState`，graph compiled state plan 后续可直接复用 transition 载体
- `EPipelineStage` / `EResourceAccess` 已补齐 color/depth attachment 对应位，避免将 attachment layout 误记为 fragment shader write
- imported swapchain initial/final state 已进入 `ImportedImageDesc`；graph compiled state plan 尚未接入
- image allocation 已禁止隐式 isolate transition；buffer/texture upload command 与 graph compiled state plan 仍待显式化
- legacy 显式 `transitionImageLayout(old,new)` 已接入 tracker 旧状态校验；dynamic rendering render-target begin transition 统一对齐 Vulkan attachment 实际布局

## Phase 6 迁移备注

- `RenderGraph` 最小资源声明层已落地：handle/desc/imported/transient/persistent declaration 先独立存在，不急于立刻接 pass/compiler
- graph handle 采用独立 `RGHandle<Tag>{index,generation}` POD，而不是复用 backend native `Handle<void*>`
- imported texture 直接复用 `ImportedImageDesc`，避免再平行定义一套 Vulkan import 协议
- pass 声明层已落地 `RGPassBuilder`，当前支持 texture/buffer read/write 以及 color/depth attachment write
- compiler 首版按 pass 插入顺序推导写后读/写后写依赖，并输出稳定拓扑序；资源未写先读会在 compile 阶段失败
- `RGPassContext` 已提供按 handle 解析 texture/buffer declaration 的执行期只读入口，pass execute 不必回看 graph 存储细节
- compiler 已补齐最小 usage 校验：texture read/sample、color/depth attachment 和基础 buffer usage 不匹配会在 compile 阶段失败
- `debugDump()` 现可输出 pass 列表、拓扑序、依赖边和 compile issues，便于后续 graph/executor 集成期诊断
- `RenderGraphResourceRegistry` 已能把 transient/persistent texture-buffer 与 imported texture-buffer 解析到最小物理资源 owner；当前不做 aliasing、frame recycle 和 view cache
- `RenderGraphResourceRegistry` 现已补上最小 frame-to-frame replacement 语义：持久化 executor 复用同一 handle 时，若 texture/buffer desc 或 imported contract 变化，会在 `sync()` 中替换物理资源；描述未变时则保持复用
- imported texture contract 已进一步补强：graph import 现在可直接复用外部 shared image view，并显式保活额外 owner；registry 在“不需要 replacement”时也会刷新 imported keepalive，避免 owner 更新被旧 entry 吞掉
- SSAO 已成为第一个 runtime-side `ERGResourceLifetime::Persistent` 落地点：输出 AO 图不再由 `DeferredRenderPipeline` 预先分配 `RenderImage`，而是在 stage graph 中声明为 persistent texture，并在执行后回传给 `LightStage` / debug 视图消费
- Bloom extract / blur ping-pong / composite 与 postprocess output 也已沿同一模式改为 graph persistent texture；descriptor 更新已下沉到 pass execute 回调，避免在 graph execute 前依赖外部 intermediate owner
- `RenderGraphResourceRegistry` 的 owned texture/buffer replacement / prune / clear 现在会优先通过 `DeferredDeletionQueue` 退休旧资源；未初始化删除队列的 core test / tool 场景才会立即释放
- `RenderGraphExecutor` 已起最小执行骨架：compile 校验、registry sync、按拓扑序驱动 pass execute callback；尚未接入 Vulkan barrier/state plan 和 rendering/copy helper
- `RenderGraphExecutor` 的 image transition 已改为直接走 command buffer 的 tracked auto transition，避免 executor 与 Vulkan command buffer 各自维护一份 layout 真相
- `RGRenderContext` 已补最小 helper：color rendering begin/end 与 buffer copy，pass callback 开始可以少直接拼 `RenderingInfo` / `copyBuffer` 样板
- compiled graph 已开始产出最小 texture/buffer required state plan（read/storage/color/depth）；texture plan 已在 executor 中先接入 `ResourceStateTracker -> transitionImageLayout()`，buffer plan 已接入最小 `bufferMemoryBarrier()` 发射，后续仍需与统一 barrier backend 收敛
- 已补最小 clear/copy smoke 测试：graph pass callback 可驱动 `beginRendering/endRendering` 与 `copyBuffer`，用于压实 executor/resource resolve/command buffer 调用链
- `PBRGenerateBrdfLUT` 已作为首个真实 graph-backed utility pass 试点：执行路径改为 build graph -> import output RenderImage -> executor 驱动 draw pass，后续可按同模式迁移 irradiance/prefilter/cubemap conversion
- `PBRGenerateBrdfLUT` 不再回退到手写 dynamic rendering 初始化路径：该路径在 MoltenVK 启动阶段曾触发 attachment 构造崩溃，当前保持 graph-backed execute 作为稳定基线
- `EquidistantCylindrical2CubeMap` 已接入第二个 graph-backed utility pass 试点：graph import 现支持复用现有 shared image 并指定 view desc，单 face 输出 attachment 不再依赖 `Texture::wrap()` 临时 adapter
- `CubeMap2PBRIrradianceMap` 已按同模式接入 graph-backed 执行：input cubemap 与 output face view 都经由 imported graph resource + view desc 进入 executor，utility pass 迁移开始形成可复用模板
- runtime 启动链已额外压实 graph execute 的两个现实约束：延迟执行 pass callback 不允许引用捕获局部 graph handle；swapchain-backed presentation target 必须先同步当前 image index，再按实际 acquired image 录制 presentation pass
- `CubeMap2PBRPrefilteredEnv` 已接入 graph-backed 执行：同一模板已覆盖 mip+face 双层 subresource 输出，说明 imported graph texture + view desc + state plan 已足以承接完整环境贴图 utility pipeline
- `PostProcessingStage::execute()` 已成为首个 runtime-side graph-backed stage cut：保留 bloom 预处理与 `BasicPostprocessing::render()` 内部 draw 逻辑，只将外层 output pass/attachment transition/dynamic rendering 壳切到 graph + executor，验证 graph 已能承接主链路中的单 stage 渐进迁移
- `SSAOStage::execute()` 已切到 graph-backed 执行壳：保留现有 GBuffer descriptor 更新、frame UBO 和 fullscreen draw 逻辑，只将 output attachment/rendering/barrier 交给 graph + executor，作为 Deferred 主链路首个 graph 化 stage 样板
- `BloomPostprocessing::render()` 已切到 graph-backed 子链：extract / blur ping-pong / composite 三段都改为 graph pass，现有 descriptor cache、push constants 和 pipeline 逻辑保持不变，说明 graph 已可承接 runtime 内部多 pass 后处理工作流而不必一次性改写 shader/descriptor 层
- Deferred 主链现已进一步把 bloom/postprocess 并入 pipeline 自己的 graph prepare/execute：`BloomPostprocessing` 与 `PostProcessingStage` 新增 append-to-graph 路径，Deferred 不再为后处理单独起 executor；Forward 兼容 wrapper 暂时保留，等待后续统一 graph 接管时再收口
- `RGRenderContext` 已补最小 `beginRasterRendering(color + optional depth)` helper，并有核心测试覆盖 depth attachment 场景；这一步是后续 viewport/shadow 类 pass graph 化的前置接口补齐
- viewport overlay 录制边界已向 pipeline 内回收：`RenderRuntime` 不再在 `renderWorldFrame()` 里直接插入 `Render2D/UI`，而是通过 `RenderPipelineFrameContext::recordViewportOverlays` 回调交给 pipeline 在 viewport rendering 结束前调用；overlay ownership 现已从 runtime 外层推进到 pipeline 内，便于后续继续收 deferred viewport graph 边界
- Forward/Deferred viewport pass 现已在各自 `tick()` 内闭合完成，`RenderRuntime::renderWorldFrame()` 不再负责额外收尾；`IRenderPipeline` 上的 `hasOpenViewportPass()/endViewportPass()` 旧契约已删除，viewport pass 生命周期正式收口到 pipeline 内部
- Deferred viewport-sized intermediate resources 已开始按单一 frame-boundary 批次刷新：viewport resize 现在会一起完成 `GBuffer RT / Viewport RT / SSAO image / postprocess` 替换和 stage 重绑，不再额外挂一条 SSAO resize 队列；这一步先把 ownership 从分散 pending path 收成统一更新点，后续更容易替换成 graph registry
- Deferred pipeline 的 pending refresh 协议也已继续收口：viewport resize、shared depth format、attachment format 与 shadow resource rebuild 现在共享单一 frame-boundary refresh mask 和批处理入口，不再各自维护一条 applyPending* 分支；后续删除 dirty state 时可以直接围绕这一个入口继续裁剪
- Deferred 对 graph 产物的帧内同步也已开始去 stage-local owner：SSAO 输出现在由 pipeline 的 frame state 统一持有并分发给 light pass / debug view，`SSAOStage` 不再额外缓存一份 graph 结果
- postprocess 主输出也已跟进同一路径：`PostProcessingStage` 不再缓存 graph 主输出，Forward/Deferred pipeline 各自持有当前帧 postprocess image，并负责向 viewport compat 输出、debug output 与 automation/screenshot 链路分发
- editor 主 viewport 也已开始脱离 `Texture::wrap()` 兼容出口：`EditorViewportContext` 现在直接消费 `IImageView*`，Forward/Deferred pipeline 不再为 editor 主视口维护 `_viewportTextureCompat`；剩余 `Texture* viewportTexture` 只保留给 automation screenshot fallback
- Forward pipeline 的 viewport resize / shadow rebuild / viewport color-format 编辑现已对齐到显式 pending refresh：`onViewportResized()` 与 RT editor 只记录请求，实际 resource recreate / snapshot refresh / stage format refresh 统一在 frame boundary 批处理，不再依赖 `refreshDirtyResources()` 每帧轮询
- Deferred `refreshDirtyResources()` 已从每帧无条件 `flushDirty()` 收紧为 attachment-spec 变化时的 fallback 修复；resize 主路径不再依赖 runtime tick 内的隐式自修复，后续可以继续朝“显式 replacement、删除 dirty repair path”推进
- Deferred `SSAOStage` / `LightStage` 已不再订阅 `IRenderTarget::onFramebufferRecreated`；GBuffer/SSAO descriptor invalidation 现改回 pipeline 显式资源替换点统一触发，进一步减少 stage 对 legacy render-target 内部事件的依赖
- Deferred `SSAOStage` / `LightStage` 的 GBuffer 读取路径已从 `IRenderTarget -> FrameBuffer -> Texture` 间接反查切到显式 `DeferredGBufferResources` 输入；这一步先把 attachment dependency 从隐式容器借用改成 pipeline 明确传入的资源绑定，后续更容易替换成 graph imported handles
- Deferred pipeline 自身对 viewport/depth 的即时访问也开始显式化：当前 frame 的 GBuffer / Viewport attachment 已收成 `DeferredGBufferResources` 与 `DeferredViewportResources` 快照，depth copy、viewport postprocess 输入不再临时扒开 render target/framebuffer 读取资源
- RenderGraph 已补最小 texture transfer 语义与 `RGRenderContext::copyTexture()` helper；Deferred depth copy 现已切成 graph-backed copy pass 壳，开始把“手写 barrier + copyImage”路径迁回 graph executor 管理
- Deferred viewport raster 外壳已切入 graph：viewport color/depth attachment、light pass、overlay pass 与 viewport overlay callback 现在由单个 graph-backed raster pass 包裹，postprocess 继续作为后续消费方读取 graph pass 输出
- RenderGraph 已补最小 MRT raster helper，`RGRenderContext::beginRasterRendering()` 现在可绑定多个 color attachment；Deferred GBuffer 外壳已迁到 graph-backed raster pass，`GBufferStage` 保持只负责 draw 录制
- Deferred 自身 stage 的格式刷新路径已开始去 `IRenderTarget` 化：`GBufferStage / LightStage / ViewportOverlayStage` 现改用显式 `DeferredAttachmentFormats`，把“为了拿格式而借整个 render target”收紧成更明确的格式描述输入
- debug overlay 路径的格式刷新也已跟进切到显式 attachment format 描述：`DebugRenderSystem / DebugPrimitives / DebugSkinning` 不再为 pipeline format refresh 依赖 `IRenderTarget`，并复用下沉后的通用 `RenderAttachmentFormats`
- Deferred pipeline 当前帧 snapshot 现同时缓存 `GBuffer / Viewport` 的 attachment resources 与 formats；stage refresh 不再在 resize/dirty repair 路径里零散回查 render target 描述，为后续把 legacy owner 替换成显式 attachment set 先收敛一层状态入口
- Deferred stage setup 也已改为复用 pipeline 持有的 current-frame attachment snapshot：`SSAOStage / LightStage` 初始化与 viewport-sized refresh 不再现查 `IRenderTarget -> FrameBuffer`，stage 输入继续向“只吃显式 frame state”方向收口
- Deferred graph execute 路径已开始直接用 current-frame attachment snapshot 推导 render area / overlay extent；`executeGBufferPass()` 与 `executeViewportPass()` 不再为这些元数据读取 `_gBufferRT/_viewportRT`
- Deferred 启动阶段已补 runtime attachment format fallback：pipeline 会先按后端 `isImageFormatSupported()` 解析 HDR color / viewport color / shared depth / shadow depth 的可创建格式，再初始化 render target 与 stage format refresh，避免在 MoltenVK/portability 环境下因为硬编码 `R16G16B16A16_SFLOAT` 或 `D32_SFLOAT` 直接崩在 `VulkanImage::allocate()`
- shadow 公共资源也开始显式化执行期元数据：`ShadowMapResources` 现在缓存 depth image 与 layer count，Deferred shadow handoff 不再现查 `renderTarget -> framebuffer -> texture`
- common shadow technique/stage 契约也已跟进显式资源输入：`BasicShadowMapTechnique / ShadowStage` 现改吃 `depth image + format + extent` 做 shadow view/pipeline refresh，Forward/Deferred 两条路径都不再通过 `refreshFromRenderTarget()` 反查当前 framebuffer
- shadow pass / debug / graph import 路径都已脱离 `Texture` adapter：directional / point shadow depth attachment、editor preview 与 deferred light graph import 现在统一直接使用 `depth image + image view`
- shadow extent / resolution 读取也开始从 legacy render-target 元数据收口回 `ShadowMapResources`：Forward/Deferred 对 shadow 分辨率、extent 和 technique refresh 的查询已优先复用显式缓存状态
- 兼容期 attachment 读取也开始从 framebuffer owner 下沉到 render-target facade：Deferred/Forward/shadow 公共路径现改用 `IRenderTarget::getCurrent*Texture()` 访问当前 attachment，减少高层对 `getCurFrameBuffer()` 的直接依赖，为后续收敛成 `RenderAttachmentSet` 先铺一层兼容接口
- Forward dirty refresh 也已开始对齐 Deferred 的收口方向：`ForwardRenderPipeline::refreshDirtyResources()` 不再每帧无条件 `flushDirty()` viewport/shadow render target，而是按真实 dirty 状态触发刷新
- shadow 资源 facade 继续补齐显式元数据与最小状态查询：`ShadowMapResources` 现缓存 `depthFormat` 并提供 `isDirty()/hasAttachmentDirty()`，Forward/Deferred 对 shadow technique refresh 和 dirty 判定已减少直接摸内部 render-target 状态
- Deferred attachment dirty repair 已开始收口成显式 helper 边界：`flushGBufferResources/flushViewportResources`、snapshot refresh、stage-state refresh 现分离成独立步骤，viewport resize 与 attachment dirty fallback 共享同一套刷新入口，后续删除 dirty repair path 时不必再拆散现有控制流
- Forward dirty refresh 也已继续向同样的显式 helper 边界收口：viewport/shadow 的 flush 与 stage-state refresh 现在由独立 helper 承接，后续若要把 legacy dirty repair 替换成显式 replacement/refresh，不必再在 `refreshDirtyResources()` 内联拆控制流
- `IRenderTarget` 兼容期 dirty 协议也已开始下沉成最小 facade：新增 `isDirty()/hasAttachmentDirty()/flushIfDirty()`，Forward/Deferred/Shadow facade 已优先走这些 helper，而不是直接读取 `bDirty` 或裸调 `flushDirty()`
- Forward 的 pipeline format refresh 也已开始去 `IRenderTarget` 化：`ForwardViewportStage / LitPasses / UnlitPass / AuxPasses` 现改吃显式 `RenderAttachmentFormats`，把“为了拿格式而借整个 viewport render target”收紧成 pipeline 维护的最小格式 snapshot
- Forward pipeline 自身对 viewport 执行期 attachment 读取也开始显式化：当前 viewport 的 `color/depth/resolve/extents` 已收成 `ForwardViewportResources` snapshot，viewport pass/finalize/postprocess 输入不再临时从 `viewportRT` 现查 attachment 与 extent
- shadow technique 已不再保留 `IRenderTarget` 输入契约：`BasicShadowMapTechnique` 只通过显式 `depth image + format + extent` 刷新派生视图与 pipeline，Forward viewport extent 查询也优先复用 pipeline snapshot，而不是回查 legacy owner
- `ShadowStage` 也已去掉仅作转发的 shadow render-target 持有；pipeline 只在资源替换点显式推送 `ShadowMapResources` 快照，shadow 链路继续从 legacy owner 句柄转向显式 frame state
- 运行时调试/编辑器视图路径也已开始脱离 `FrameBuffer` 细节：presentation、viewport、gbuffer、shadow 调试预览优先走 `IRenderTarget::getCurrent*Texture()` facade，而不是直接扒开当前 framebuffer
- Deferred 调试预览输出已进一步从 legacy owner 转到显式 snapshot：`RenderRuntime` 读取 `DeferredGBufferResources / DeferredViewportResources` 做 editor preview，不再通过 `gBufferRT / viewportRT` 反查 attachment
- shadow debug preview 也已开始与 legacy render target 分层：`IRenderPipelineDebugOutputs` 新增显式 `shadowDepthTexture` 输出，editor preview 不再靠 `shadowDepthRT -> depth texture` 间接拆图
- Forward debug preview 也已向同样方向对齐：shadow cubemap 预览复用显式 `shadowDepthTexture`，viewport depth 预览改读 `ForwardViewportResources` snapshot，而不是在 editor 里回查 `viewportRT`
- `IRenderPipelineDebugOutputs` 已继续补齐显式 preview 输出：新增 `viewportDepthTexture`，让 `RenderRuntimeEditorViewport` 不再依赖具体 `ForwardRenderPipeline` 取 viewport depth
- preview/debug 与 owner 语义的接口边界继续收口：`getShadowDepthRT()` 已从 `IRenderPipelineDebugOutputs` 和 `RenderRuntime/App` 的 preview 转发链移除，只在 concrete pipeline 上保留给 RT editor/catalog 使用
- postprocess output 已进一步收口为 graph persistent texture：`PostProcessingStage` 不再预分配主输出 `RenderImage`，而是在 execute 时由 `RenderGraphResourceRegistry` 按 desc/extent 接管 replacement；stage 内部也不再持有 compat `Texture`
- pipeline/runtime 层已开始暴露显式 `postprocessOutputImage`：`IRenderPipelineDebugOutputs` 与 `RenderRuntime` 现同时提供 `RenderImage*` 输出，把 `Texture*` 兼容链进一步收窄到 pipeline viewport / screenshot fallback 一侧
- automation screenshot 已开始优先消费 `RenderImage*` 的 postprocess 输出：`AppAutomationFrameContext` / `AppScreenshotCapture` 现在先用 `postprocessImage`，仅在缺失时回退 viewport `Texture*`
- runtime 对 deferred concrete owner 的一部分直连也已开始回收：RT editor deferred entries 与 shared depth format 修改现通过 `DeferredRenderPipeline` helper 完成，`RenderRuntime` 不再直接摸 `_gBufferRT/_viewportRT`
- Deferred light fullscreen pass 已迁入 graph：`executeViewportPass()` 现显式 import `GBuffer/AO/shadow/environment` 读取资源并声明 viewport HDR output；overlay 仍保留为后续独立 graph pass 壳，尚未迁移 stage 内部 draw/descriptor 逻辑
- Deferred 当前帧 `GBuffer / Viewport` attachment snapshot 现已直接缓存 `RenderImage*`，graph import 与 editor debug snapshot 不再先经由 `FrameBuffer -> Texture` 兼容层绕回当前附件；viewport/postprocess 的最终 compat `Texture*` 输出仍保留给 screenshot / legacy UI 路径
- Deferred overlay 的 scene 查询也已继续向 setup snapshot 收口：Direction gizmo 现在在 `updateStageFrameInputs()` 里生成 frame input，`ViewportOverlayStage::executeOverlay()` 不再直接查询 scene registry
- forward 侧的 RT editor owner 暴露也已对齐成 pipeline helper：`RenderRuntime` 不再直接用 `getViewportRT()/getShadowDepthRT()` 组 forward catalog entries
- deferred debug snapshot 也已从 runtime 自身的数据拼装回收到 pipeline：`DeferredPipelineDebugViews` 已移动到 deferred 模块，并由 `DeferredRenderPipeline::buildDebugViews()` 统一导出
- Forward viewport extent 的应用点也已开始收口：`applyViewportExtent()` 统一承接 extent 变更与 snapshot 刷新，execute/onViewportResized 不再各自分散维护 `viewportRT` 与 `_viewportResources` 的同步
- legacy dirty repair 的语义也继续往高层收口：`IRenderTarget` 与 `ShadowMapResources` 开始提供 `needsRefresh()/needsAttachmentRefresh()/refreshIfNeeded()` facade，Forward/Deferred dirty refresh 已不再直接暴露 `isDirty()/hasAttachmentDirty()/flushIfDirty()` 调用
- RT editor catalog 的 concrete pipeline 分支也开始回收到公共接口：`RenderTargetEditorCatalog` 已下沉到 common 头，`appendRenderTargetEditorEntries()` 现通过 `IRenderPipeline` 暴露，`RenderRuntime` 不再自行分支调用 forward/deferred 的 catalog helper
- runtime 对 viewport attachment format 的 concrete pipeline 依赖也继续缩减：`IRenderPipelineExecution` 新增 `getViewportColorFormat()/getViewportDepthFormat()`，`Render2D::init()` 已通过 active pipeline 接口读取格式，不再直接引用 forward/deferred 的 static 常量
- editor RT depth-format 调整的 deferred 特判也继续回收到 active pipeline 接口：`setSharedDepthFormat()` 现通过 `IRenderPipelineSettingsUI` 暴露，`RenderRuntime` 与 RT editor GUI 不再保留 `setDeferredSharedDepthFormat()` 这类 concrete helper
- deferred shared-depth 编辑已开始脱离 attachment dirty fallback：`setSharedDepthFormat()` 现在只负责标记 pending refresh，`beginTick()` 会在安全点统一 flush GBuffer/Viewport depth attachment 并刷新 stage state，先把 editor 最常见的 depth 改动迁到显式路径
- deferred color-format 编辑入口也已开始走同样的显式路径：`IRenderPipelineSettingsUI::setRenderTargetColorFormat()` 现由 deferred 实现，RT editor 对 deferred GBuffer/Viewport 的颜色格式修改会先标记 pending，再在 `beginTick()` 安全点统一刷新 attachment / snapshot / stage state，而不是仅依赖 `refreshDirtyResources()` fallback
- postprocess resize 的 stage-local dirty state 也已开始删除：`PostProcessingStage` 不再保留 `_pendingResizeExtent/_bResizePending` 或在 `beginFrame()/execute()` 内隐式申请 resize，Forward/Deferred 现通过显式 `resizeResources(extent)` 在各自安全点触发 replacement

## Phase 7 执行优先级与调查结论

### 近期执行优先级

1. 先处理 Deferred viewport / SSAO / postprocess 的 legacy intermediate owner，推进 graph registry 接管 replacement
2. 再把 Deferred attachment/resource replacement 的残余 dirty state 从 stage/postprocess 侧彻底删除
3. Forward graph、RenderTarget 大拆分、editor extension API 继续后置，除非前两项已经不再阻塞 Deferred 主链完成标准

### 调查结论 / 停止线

- 当前 Deferred `GBuffer / Viewport` 的 attachment spec 变更入口已经基本收敛到 pipeline 内部显式路径：viewport resize、shared depth format、editor color format 都已有 pending refresh 承接；`refreshDirtyResources()` attachment fallback 已删除，并以断言约束残余隐式 mutation
- SSAO 与 postprocess output 现在都已切到 graph persistent/resource-registry replacement 路径；后续更值得推进的是 bloom extract / blur ping-pong / composite 等残余 postprocess intermediates，而不是回到 owner 搬移式小重构
- 额外调查已进一步落地：`RenderGraphResourceRegistry` 现在已有最小 frame-to-frame replacement / lifetime 语义，且 SSAO 已作为首个 runtime-side `ERGResourceLifetime::Persistent` 落地点成立；后续应沿同一模式继续评估 postprocess/bloom，而不是回到 pipeline/stage 间的 owner 搬移
- runtime 中高频 graph 执行点也已开始为后续 registry 生命周期铺路：`RenderGraphResourceRegistry::sync()` 现会 prune 当前 graph 已不再使用的旧 handle，`SSAOStage / PostProcessingStage / BloomPostprocessing` 已改为持久化 `RenderGraphExecutor`，不再每次执行都丢弃 registry 状态
- 进一步调查：当前 runtime 在 `RenderRuntime::beginFrameCommandBuffer()` 里仍会每帧 `waitIdle()`，因此现阶段 graph registry replacement 在运行时语义上仍然保守；同时 owned resource replacement 已开始接入 `DeferredDeletionQueue`，后续若继续推进“去 waitIdle 化”，应把 imported wrapper / view cache 等剩余路径也纳入同一延迟退休协议
- Deferred pipeline 自己的 graph 壳也已跟进同一策略：GBuffer / Viewport / DepthCopy 不再每帧临时构造 `RenderGraphExecutor`，而是复用 pipeline 持有的 executor，为后续把 imported graph 资源继续收向持久 registry 打基础
- 因此继续追加 facade-only 收口的收益已经明显下降；Phase 7 后续优先做“删除 owner / graph 接管 replacement / 删除残余 dirty state”，不继续堆只改变接口表述的小提交
- 现有 checklist 保持“删除 legacy path 后才勾选”的口径；已有 graph shell 或兼容层不单独视为完成
