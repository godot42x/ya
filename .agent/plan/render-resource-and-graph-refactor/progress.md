# GPU 资源模型与 RenderGraph 联合重构进展

本文件承接 `todo.md` 中不再适合作为待办项维护的阶段进展、迁移备注和调查结论。

## 当前状态

- 资源模型层：buffer factory 迁移已基本完成，image/view/sampler 新工厂与 ownership 规则已成型
- 状态跟踪层：`ResourceStateTracker` 已成为统一收口方向，legacy layout 真相已开始退场
- graph core：已有 declaration/compiler/registry/executor 骨架，并已能承接真实 utility/runtime pass
- runtime 主线：Deferred attachment/intermediate owner、shadow image handoff、共享 buffer、point-shadow cull 与 directional/point raster recording 已统一进入主图/state plan
- 当前风险：startup/runtime 暴露的问题已经从“简单崩溃”转向“submit-time lifetime、imported subresource state、replacement 边界”这类真实主路径约束

## 当前阻塞

- imported image 的 final state 之前只在部分 raster 路径里被 attachment `finalLayout` 偶然覆盖；非 raster 末次使用后仍缺少统一 executor 收口
- offscreen/environment preprocess 虽已 graph-backed，但仍需继续验证 imported subresource range 与 submit-time 生命周期契约
- graph registry replacement 已可用，但还需要继续压实与 runtime frame boundary、deferred deletion 和 startup/shutdown 的一致性

## 下一步

1. 以启动链和 `HelloMaterial` smoke 为主，继续清掉 runtime 中暴露的 graph/resource-state/lifetime 问题
2. 补齐 imported resource 的 final-state / replacement / shutdown contract，并用 core test 锁住
3. 再推进 Forward graph 和外围 GPU 工作流迁移

## 最新验证

- 2026-07-16：automation screenshot 的 frame-context/render-image 传递现在也改成了 owner-aware，不再把当前帧 postprocess / viewport 输出先降成裸 `RenderImage*` 再交给 `AppScreenshotCapture::request()`。此前 `AppFrameLoop -> AppAutomation::onFrameCompleted() -> handleScreenshotAutomation()` 这段链路虽然最终会在 request 内把 source image 抓成 `shared_ptr<IImage>` 放进 offscreen job，但在 runtime 到 automation 的边界上，owner 真相已经先掉成 raw 了。现在 `AppAutomationFrameContext`、`RenderRuntime` shared getter 与 screenshot request 签名统一改成直接传 `shared_ptr<RenderImage>`；这样 request 选择 source 的窗口内不再依赖“当前帧 render-image wrapper 还恰好活着”，owner 真相会一路带到真正开始排队 screenshot job 的地方。
- 直接收益：automation screenshot 这条链终于和 editor viewport、debug catalog、presentation screenshot 一样，开始在跨层边界保留当前帧 render-image owner，而不是只在更深一层 job lambda 里补救性地抓 `IImage`。后续如果继续排查 automation screenshot 与 pipeline switch / postprocess replacement / frame-boundary rebuild 的交界，这里不再是一个 runtime 外围仍然用 raw render-image 的例外。
- 当前停止线：这一步只收了 automation frame-context 到 screenshot request 的 owner 传递，不代表所有 automation artifact 路径都已经 shared-owner 化；像 RenderDoc capture 相关状态本来就不是 render-image owner 模型，不应为了写法统一硬混进来。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*:AppScreenshotCaptureTest.*'` 74/74 通过
  - 新增 `AppScreenshotCaptureTest.ViewportRequestRetainsSourceImageAfterCallerDropsRenderImageOwner`
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0
- 2026-07-16：`RenderPipelineDebugOutputCatalog` 里的 render-image 输出现在也开始直接携带 shared owner，而不再把 viewport/postprocess/bloom 这些当前帧图像统一降成裸 `RenderImage*`。此前 runtime 在组装 debug catalog 时只保留 raw pointer，editor viewport/debug 再立刻从这些 raw 输出回取 `getImageShared()`；这让 owner 真相明明已经存在于 Forward/Deferred pipeline 与 postprocess/bloom stage 当前帧 snapshot 里，却又在 runtime/export 边界掉了一次。现在 catalog 会直接保存 viewport/postprocess/bloom extract/blur/composite 的 shared owner，Bloom/PostProcessing/Forward/Deferred 只补最小 shared getter，editor debug slot 直接消费这些 owner，而不是再从 raw image 反查。
- 直接收益：runtime debug/export 这一层终于和前面已经收过的 editor 主 viewport、Deferred SSAO debug slot、pipeline postprocess output 一样，开始把 owner 真相带到真正的消费边界。后续如果继续排查 postprocess graph replacement、viewport rebuild 或 debug UI 刷新时的生命周期问题，不需要再把 debug catalog 当成一个“中间层又把 shared owner 降成 raw”的例外。
- 当前停止线：这一步只收了 debug catalog 里的 render-image 输出，`viewportDepthTexture`、shadow view 这些非 `RenderImage` 导出仍然保持原语义；如果后续继续收口，要先确认它们的 owner 真相是否同样清晰存在，避免把不同资源模型硬揉成一类。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*:AppScreenshotCaptureTest.*'` 73/73 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0
- 2026-07-16：Deferred debug views 导出的 SSAO 槽位现在也不再把当前帧输出降成一根裸 `RenderImage*`。此前 `DeferredRenderPipeline::buildDebugViews()` 把 `_currentSSAOOutput` 退化成 raw pointer，`RenderRuntime::appendDeferredDebugSlots()` 再立刻从这根裸指针回取 `getImageShared()` / `getImageView()` 组装 editor debug slot；也就是说 owner 真相明明还在 pipeline 当前帧 snapshot 里，却在 debug/export 边界又掉了一次。现在 `DeferredPipelineDebugViews` 会直接带出 SSAO 的 shared owner，editor 侧则显式保存对应的 `ownedView` 和 `image`，不再隐含依赖这份 raw `RenderImage*` 在 slot 消费期间“刚好还活着”。
- 直接收益：Deferred debug/export 链和前面已经收过的主 viewport、postprocess prepared output、screenshot presentation source 一样，开始把 owner 真相保留到真正消费 debug slot 的边界。后续继续排查 editor debug 面与 graph replacement / viewport rebuild 的交界时，SSAO 不再是一个额外的裸指针例外。
- 当前停止线：这一步只收了 Deferred debug views 里的 SSAO slot，不代表所有 debug slot 都已经逐条切成 owner-aware；剩余输出仍然要按价值继续审，但不混进本批。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*:AppScreenshotCaptureTest.*'` 73/73 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0
- 2026-07-16：editor 主 viewport 输出现在也改成了 owner-aware snapshot，而不再只把当前帧最终图像降成一份裸 `IImageView*` 塞进 `EditorViewportContext`。此前 `RenderRuntime::updateEditorViewportContext()` 会根据 postprocess / viewport output 只挑一个 raw view 传给 editor；一旦 pipeline 当前帧输出在 graph replacement、postprocess replacement 或 frame-boundary rebuild 后发生 owner 变更，editor 侧这条主显示链仍然依赖“底层 `RenderImage` 恰好还没被释放”的隐含前提。现在 `EditorViewportContext` 会显式保存选中的 `RenderImage` shared owner，raw `viewportImageView` 只作为从 owner 派生出的便捷视图；Forward / Deferred 也各自补了最小 shared getter，把“最终 editor viewport 应该显示哪张图”的 owner 真相保持在 runtime/pipeline 边界，而不是再让 editor 缓存一根裸 view 指针充当事实源。
- 直接收益：editor 主 viewport 终于和前面已经收过的 presentation screenshot、Deferred/Forward postprocess output、viewport attachment snapshot 站到同一条生命周期语义上。后续如果继续排查 graph registry replacement、pipeline switch 或 viewport rebuild 时的 editor 观察面，这里不再是一个“主链都拿 owner，只有 editor 主图还靠裸 view”的例外。
- 当前停止线：这一步只收了 editor 主 viewport 的 owner retention，不代表所有 editor/debug 输出都已完全 shared-owner 化；像 Deferred debug views 里仍有少数 raw export，可继续按价值再逐条审，但不混进本批。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*:AppScreenshotCaptureTest.*'` 73/73 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0
- 2026-07-16：Forward pipeline 当前 postprocess 输出现在也改成了 owner-aware snapshot，而不再只缓存一份裸 `RenderImage*`。此前 `ForwardRenderPipeline::finalizeViewportPass()` 在 `_postProcessStage.execute()` 成功后，只把返回的 raw `RenderImage*` 填进 `_currentPostprocessOutput`；这和 Deferred 已经持有 shared output owner、`PostProcessingStage` 也已经提供 `getPreparedOutputImageShared()` 的现状不一致，等于把 graph-backed postprocess 的 owner 真相又在 Forward 边界降回了裸指针。现在 Forward 直接缓存 `PostProcessingStage` 的 prepared output shared owner，对外 raw getter 维持不变，但生命周期真相已前移到 pipeline 当前帧 snapshot。
- 直接收益：Forward 与 Deferred 在“graph 外缓存最终后处理输出”这件事上终于采用同一种 ownership 语义。后续如果继续排查 pipeline switch、shutdown 或 postprocess graph executor replacement，Forward 不再是一个额外依赖“裸指针此刻刚好还有效”的例外面。
- 当前停止线：这一步只收了 Forward 当前 postprocess 输出 owner，不代表 Forward 所有 debug/export 面都已逐条 owner-aware 审完；后续仍可继续看 viewport/depth/shadow 之外是否还有类似的 raw snapshot 残留。
- 验证结果：
  - `xmake b ya-testing` 通过
- 2026-07-16：editor/presentation screenshot automation 现在会把当前 presentation `RenderImage` 的 shared owner 一起带进 screenshot state，而不是只在 request/record 边界上传裸 `RenderImage*`。此前 non-editor/offscreen 截图已经会在 job lambda 中捕获 `shared_ptr<IImage>`，但 editor 路径仍然是 `AppFrameLoop -> AppAutomation -> AppScreenshotCapture::recordPresentationCapture()` 逐层传裸指针；如果 screenshot request 和真正录制之间发生 swapchain rebuild / presentation wrapper replacement，这条链没有自己的 owner snapshot，只能隐含依赖 runtime 当前帧对象“还没被换掉”。现在 `AppAutomationFrameContext` 改为携带 shared presentation image，`AppScreenshotCaptureState` 在 pending presentation capture 窗口内保存这份 owner，录制函数直接从 state 取图，不再要求调用侧重新传 raw image。新增 `AppScreenshotCaptureTest.EditorRequestRetainsPresentationOwnerUntilReset`，锁住“editor screenshot request 之后，即使外部局部 shared_ptr 已释放，state 仍会保活 presentation source，直到 capture reset/完成”为止。
- 直接收益：automation/editor screenshot 这条链终于和最近几批 graph/offscreen keepalive 修复站到同一边——需要跨帧/跨录制窗口保活的 imported/presentation source，不再只靠外层调用时机碰巧正确。后续如果继续排查 presentation rebuild、editor screenshot 与 runtime replacement 的交界，这里不再是一个裸指针例外。
- 当前停止线：这一步补的是 editor screenshot 在“request 到 record”窗口内的 source owner 保活，不代表 presentation screenshot 的 copy/transition 全链已经完全 graph 化；后续仍可继续看是否需要把 presentation capture 本身进一步吸收到更统一的 imported-resource 生命周期合同里。
- 验证结果：
  - `xmake b ya-testing` 通过
  - `ya-testing --gtest_filter='AppScreenshotCaptureTest.*'` 1/1 通过
- 2026-07-16：`RenderGraphResourceRegistry` 现在也会把 imported buffer 的 retained owner 通过 `DeferredDeletionQueue` 退休，而不是只在 texture 侧有 GPU-safe release contract。此前 imported buffer 在 registry `prune/switch-to-owned/replace/clear` 这些出口上都会直接销毁 `RGImportedBufferDesc::retainedResources`；这和 imported texture 已经走 DDQ 的行为不对称，也让 shutdown / replacement / graph 清理边界继续依赖“buffer owner 立刻释放也没事”的隐含前提。现在 registry 新增统一的 retained-owner 退役 helper，并补上 `RenderGraphCoreTest.ResourceRegistryPruneDefersImportedBufferKeepAliveReleaseThroughDeletionQueue` 与 `RenderGraphCoreTest.ResourceRegistryClearDefersImportedBufferKeepAliveReleaseThroughDeletionQueue`；原有 imported buffer refresh/prune tests 也改成对全局 DDQ 单例状态稳健，不再把“队列尚未初始化”当作隐藏假设。
- 直接收益：imported buffer 与 imported texture 在 registry replacement/prune/clear 这层终于对齐到同一条 deferred-deletion contract。后续继续查 startup/shutdown、一帧外 replacement 或 runtime 常驻 executor 清理问题时，不需要再把 buffer 侧当成一个语义例外。
- 当前停止线：这一步补的是 imported buffer retained owner 的 registry 退役语义，不代表所有 imported buffer callsite 都已经逐条审完；后续仍可继续看是否还存在 graph 外缓存 raw buffer 但 owner 真相没有同步前移的路径。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 72/72 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0
- 2026-07-16：presentation swapchain image 重建时，现在会主动 `clear()` 掉 `_presentationGraphExecutor`，不再把旧 imported wrapper 留到“下一次 presentation execute 时再顺手 replacement”。此前 `rebuildPresentationImages()` 只清 `_presentationImages` 并重建新的 swapchain `RenderImage`，但持久 presentation executor 的 registry 仍会保留上一次 graph import 的 wrapper/keepalive；这在正常下一帧会被替换掉，可是 replacement 边界并不显式。现在重建前先清 executor，并新增 `RenderGraphCoreTest.ExecutorClearDefersImportedTextureKeepAliveReleaseThroughDeletionQueue`，锁住“executor.clear() 释放 imported texture 时同样走 DDQ，而不是立即掉 owner”。
- 直接收益：presentation 这条 runtime 常驻 graph 的 imported wrapper 生命周期，现在和 swapchain recreate 边界对齐得更明确。后续如果继续查 viewport resize、present target capture 或 shutdown 时的 imported-resource replacement，就不用再把“旧 presentation wrapper 还挂在 executor registry 里”等到下一帧自动替换视作隐含前提。
- 当前停止线：这一步收的是 runtime 常驻 presentation graph executor 的 replacement 边界，不代表所有长期存活 executor 都已经逐个审完；后续仍可继续看 SSAO、postprocess 之外还有没有类似“registry 会最终自愈，但 frame-boundary 没有显式 clear”的点。
- 2026-07-16：Deferred 当前帧的 SSAO / postprocess / bloom debug 输出现在也开始缓存 shared owner snapshot，而不再只留 registry 解析出来的裸 `RenderImage*`。此前 `_currentSSAOOutput`、`_currentPostprocessOutput` 以及 `BloomPostprocessing::_extract/_blur/_compositeImage` 都只是 frame-local raw view；这和已经 owner-aware 的 `DeferredGBufferResources` / `DeferredViewportResources` 不一致，也会让 graph registry replacement / clear 之后的调试输出继续依赖“registry 恰好还没换掉物理资源”的隐含前提。现在 `resolvePreparedResources()` 与 Deferred 主图同步改用 `resolveTextureShared()`，内部继续对外暴露原有 raw getter，但 owner 真相已回收到 stage/pipeline 的当前帧 snapshot。
- 直接收益：postprocess/bloom/ssao 调试输出与主 attachment snapshot 终于采用同一种 ownership 语义。后续继续排查 resize、shutdown 或 graph registry replacement 时，debug/editor 观察面不会再是“主资源有 owner、后处理输出只有裸指针”这一块例外。
- 当前停止线：这一步收的是 Deferred 主线和 postprocess/bloom 内部 prepared output owner，不代表 Forward pipeline 的当前帧输出也已经完全对齐；Forward 仍按计划后置，不把它混进本批。
- 2026-07-16：`RGRenderContext::resolveTexture()` 现在也会像 buffer/attachment helper 一样，把解析到的 `RenderImage` 的 image、image view 以及 imported keepalive 一起 retain 到 command buffer。此前只有 `beginRasterRendering()` 和 `copyTexture()` 这类显式 helper 会调用 `retainResolvedRenderImage()`；如果某个 graph pass 只是 `pass.read(texture)`，然后在 execute callback 里通过 `resolveTexture()` 取出 image view 去更新 descriptor（`PostProcessingStage` 就是当前真实调用点），submit/encode 阶段仍可能晚于这份局部引用的生命周期边界。新增 `RenderGraphCoreTest.ResolveTextureRetainsImportedTextureKeepAliveResources` 后，这条 sampled-read 路径也被锁进 submit-time contract。
- 直接收益：RenderGraph 对 imported texture 的 submit-time 保活不再只覆盖 attachment/copy 路径，纯 sampled-read 的 runtime pass 也能拿到同一条合同。后续继续排查 descriptor/cache 与 graph imported texture 的交界问题时，可以把“resolveTexture 拿到 view 但没 retain”排除掉。
- 当前停止线：这一步补的是 graph execute callback 内部的 texture resolve 保活，不等于所有 graph 外 descriptor cache 都自动安全；像 `LightStage::prepare()` 这类 frame-boundary descriptor 更新，仍然要靠 stage/pipeline 自己持有当前帧 attachment owner，不能误把 graph command-buffer keepalive 当成更上层缓存生命周期的通用解。
- 2026-07-16：补上了 RenderGraph registry 在析构路径上的 deferred-deletion contract。此前 `RenderGraphResourceRegistry` 只有显式 `clear()` 时才会把 texture / owned-buffer 资源通过 `DeferredDeletionQueue` 退休；如果 registry 直接析构（例如 `DeferredRenderPipeline::shutdown()` 里 `_graphExecutor.reset()` 导致成员 registry 跟着析构），这些 graph-owned / imported wrapper 资源会绕过 DDQ 立即释放，和运行期 replacement/clear 路径不一致。现在 registry 析构会统一走 `clear()`，并新增 `RenderGraphCoreTest.ResourceRegistryDestructorDefersImportedKeepAliveReleaseThroughDeletionQueue` 锁住“析构后 retained owner 仍存活，直到 `flushAll()` 才真正释放”的行为。
- 直接收益：shutdown / pipeline switch / future executor lifetime 调整时，registry 不再要求所有调用方都记得先手动 `clear()` 才能拿到 GPU-safe release contract。这样我们在继续审 startup/shutdown consistency 时，可以把“显式 clear 和隐式析构是否一致”从待怀疑项里移掉。
- 当前停止线：这一步补的是 registry 析构与 DDQ 的一致性，不代表所有 graph/runtime 退出路径都已完全证实；后续仍要继续看 pipeline shutdown、resource provider 与更高层 service 在 wait-idle 之后是否还存在类似“手动路径安全、析构路径漏合同”的不对称。
- 2026-07-16：buffer 侧的 registry replacement/prune keepalive contract 现在也补上了对称 core tests。新增 `RenderGraphCoreTest.ResourceRegistryRefreshesImportedBufferKeepAliveWithoutReplacingBuffer` 与 `RenderGraphCoreTest.ResourceRegistryPrunesImportedBufferKeepAliveWhenBufferIsRemoved`，分别锁住两件事：其一，同一 imported buffer handle 在 desc/identity 稳定时只刷新 retained owner、不重建 raw buffer；其二，当 imported buffer 从 graph 中移除时，registry prune 会释放这份 keepalive，而不是把 owner 无界滞留在缓存里。
- 直接收益：这一步没有扩大 runtime 改动面，但把 “imported buffer keepalive 不只在 executor 有效，registry frame-to-frame sync/prune 也能正确更新和释放 owner” 这条 contract 补成了和 texture 侧更对称的证据链。接下来如果再碰 startup/shutdown 或 replacement 相关问题，就能更明确地区分“实现缺口”和“只是之前没被测试锁住”。
- 当前停止线：目前补齐的是 imported buffer 在 registry 中的 keepalive 刷新/移除证据；更广义的 startup/shutdown consistency 仍需继续从 runtime 主链调查，尤其是 graph 退出、pipeline shutdown 与 deferred deletion queue 的交界处。
- 2026-07-16：`PointShadowCullPass` 里最后一个“owner 已存在但 graph import 只拿到裸指针”的 buffer 也已接上显式 keepalive。此前 cull pass 持有 `instanceBuffer` 时只保存 `IBuffer*`，虽然真实 owner 一直在 `PointShadowIndirectRenderer::_perFlight[].instanceBuffer`，但这份 owner 信息在 `bindInstanceBuffer()` 时被丢掉，graph import 只能走 raw path；现在 cull pass 会直接保存这份 `shared_ptr<IBuffer>`，并在 append graph 时把实例 buffer 也作为 retained imported buffer 导入。
- 直接收益：point-shadow compute cull 的四类 imported buffer（instance/frustum/draw-command/visible-instance）现在都走同一条 submit-time keepalive contract，不再只剩实例数据这一个特例依赖“renderer owner 应该还在”的隐式前提。
- 当前停止线：这一步补的是 owner 已清晰存在、但在 pass 边界被降成裸指针的路径。后续如果再遇到 raw `IBuffer*` import，需要先确认 owner 真相是不是同样明确，避免为了统一写法而把真正的所有权问题藏起来。
- 2026-07-16：Deferred 主图里的 frame/light/skinning imported buffer 现在也不再只以裸 `IBuffer*` 进入 graph。`GBufferStage` 新增 shared owner getter，`DeferredRenderPipeline::executeDeferredMainGraph()` 改为把这些 per-flight `shared_ptr<IBuffer>` 连同 `retainedResources` 一起传入 `RGImportedBufferDesc`，因此 Deferred GBuffer / light 主链与前面 shadow 路径的 imported-buffer keepalive contract 已对齐。
- 直接收益：Deferred 默认主链上最核心的 host-written UBO / SSBO 不再只靠 stage 成员“通常会活得足够久”这个隐含前提；graph executor / command buffer 现在可以把这些 owner 显式 retain 到 submit-time 生命周期边界。
- 当前停止线：这一步只补齐了 Deferred 主图里 owner 本就明确存在的 imported buffer。像 point-shadow cull 的 `instanceBuffer` 这类仍只有裸指针输入、暂时没有显式 shared owner 的路径，后续仍需结合上游 owner 结构再决定怎么收口，避免为了统一接口而虚构所有权。
- 2026-07-16：RenderGraph imported buffer 现在也具备了和 imported texture 对齐的 keepalive contract。`RGImportedBufferDesc` 新增 `retainedResources`，executor 在发射 imported buffer barrier 时会把这些 keepalive retain 到 command buffer，`RGRenderContext::resolveBuffer()` 也会在执行期补同样的 retain；另外 point-shadow / directional-shadow 这些 graph 路径里原本以 `shared_ptr<IBuffer>::get()` 导入的 UBO / SSBO / indirect buffers，现在会把 shared owner 一并传进 graph import，而不是只留下裸 `IBuffer*`。配套新增 `RenderGraphCoreTest.ExecutorRetainsImportedBufferKeepAliveResources`，锁住“imported buffer 的 submit-time owner 会随 graph execution 进入 command buffer keepalive”的行为。
- 直接收益：frame/light/skinning、point-shadow indirect/cull 等 imported buffer 不再完全依赖“外层 owner 恰好一直活着”这个隐含前提。graph 对 imported resource 的 submit-time 生命周期契约因此不再只覆盖 texture/image view，buffer 侧也有了同一条 keepalive 通路。
- 当前停止线：这一步补的是 imported buffer keepalive 与几条高价值 runtime import callsite；更广义的 registry replacement / startup-shutdown 一致性仍需继续压实，尤其是那些还只能传裸 `IBuffer*` 而没有显式 owner 的路径。
- 验证结果：
  - `xmake b ya-testing` 通过
  - `ya-testing --gtest_filter='RenderGraphCoreTest.ExecutorRetainsImportedBufferKeepAliveResources:RenderGraphCoreTest.ExecutorSeedsImportedBufferBarrierFromDeclaredInitialState'` 通过
- 2026-07-16：offscreen job 现在会把录制期 command buffer retain 的 keepalive 资源显式转交到 `OffscreenJobResult::retainedResources`，而不是只保活到当前录制结束。此前 graph/imported view 若在 offscreen utility pass 中通过 command buffer retain 才满足 submit-time 生命周期，job 完成后结果对象并不会继续持有这些 owner；这次在 `queueOffscreenJob()` 的成功路径中补上了 retained-resource 转交，并让 `OffscreenTaskService::shutdown()` 在存在 pending fence / submitted jobs 时先等待并 finalize，再销毁 fence 与清空提交列表。配套新增 `OffscreenAsyncTest.QueueOffscreenJobPublishesRecordedKeepAliveResources`，并把原来依赖假 `ICommandBuffer*` 的 queue tests 一并升级为真实 test command buffer。
- 直接收益：offscreen output 不再只是“有一张结果图”，而是把录制期真正依赖的 keepalive 一起带出，shutdown 时也不会跳过最后一批 recorded jobs 的 finalize。后续继续推进 environment preprocess、screenshot copy 或 graph-backed utility output 时，submit-time 生命周期证据会更完整。
- 当前停止线：这一步补的是 offscreen 结果 keepalive 与 shutdown finalize；更广义的 graph registry replacement / startup-shutdown 一致性，以及 imported buffer 的 final-state contract 仍需继续推进。
- 验证结果：
  - `xmake b ya-testing` 通过
  - `ya-testing --gtest_filter='OffscreenAsyncTest.QueueOffscreenJobPublishesRecordedKeepAliveResources:OffscreenAsyncTest.QueueOffscreenJobRecordsAndPublishesSuccessfulTask:OffscreenAsyncTest.QueueOffscreenJobMarksExecutionFailure:OffscreenAsyncTest.CancelledQueuedJobRemainsCancelledWhenQueuedWorkerRuns:OffscreenAsyncTest.FinalizeSubmittedJobsPromotesOnlyRecordedJobs'` 通过
- 2026-07-16：`RenderGraphExecutor` 现在会在 pass 执行完成后统一收口 imported texture 的声明 `finalLayout`。此前 swapchain / presentation 这类 imported image 若最后一次使用落在 transfer 等非 raster 路径，只会被迁到 pass 所需 layout，而不会在 graph 结束后自动回到 import contract；这次新增 `finalizeImportedTextureStates()` 后，executor 会遍历 imported texture，并按声明的 subresource range 追加最终 layout transition。配套新增 `RenderGraphCoreTest.ExecutorRestoresImportedTextureFinalLayoutAfterTransferPass`，锁住“transfer 写入 imported swapchain image 后仍会回到 `PresentSrcKHR`”的行为。
- 直接收益：imported image 的 final-state contract 不再依赖“最后一个 pass 恰好是 raster 且底层 begin/end rendering 会处理 finalLayout”这种偶然路径。startup/runtime 后续若继续把 presentation、copy、offscreen output 等路径迁到 graph，就不会再把 imported final layout 丢在 executor 外部手写 barrier 或隐式 side effect 上。
- 当前停止线：这一步只补齐了 imported texture 的 graph 结束 final layout 收口；imported buffer 仍只有 initial state，registry replacement / shutdown 与 submit-time lifecycle 也还需要继续压实。
- 验证结果：
  - `xmake b ya-testing` 通过
  - `ya-testing --gtest_filter='RenderGraphCoreTest.ExecutorRestoresImportedTextureFinalLayoutAfterTransferPass'` 通过
- 2026-07-16：`IRenderTarget` / `VulkanRenderTarget` / backend `IRender::createRenderTarget()` 已从代码面整体删除。前一批把 `RenderTargetCreateInfo` 脱离 legacy owner 接口并清掉 runtime 假依赖后，重新全局核查确认源码中已经不存在任何 runtime/editor/test 调用面；因此这次直接移除了 `Render/Core/IRenderTarget.h`、Vulkan backend 的 `VulkanRenderTarget.{h,cpp}`，以及 `Render.h`、`VulkanRender.h/.cpp`、`OpenGLRender.h` 上对应的 dead factory API。
- 直接收益：Phase 8 终于不再只是“缩小 `IRenderTarget` 面”，而是把这套已经失去真实调用面的 legacy owner/create API 真删掉了。当前 runtime 对 attachment/image owner 的主路径已经完全建立在显式 `RenderImage` / graph import / attachment snapshot 上，后续剩余工作会更集中到资源生命周期与 graph/runtime 契约本身，而不是继续围绕一个死掉的 render-target abstraction 收尾。
- 当前停止线：虽然 `IRenderTarget` 这层 dead API 已删除，但这并不等于所有 framebuffer compatibility 语义都自然消失；后续仍需继续关注 startup/smoke 暴露的 imported subresource state、final state 与 runtime owner 生命周期问题，避免把“删掉旧抽象”误当成资源模型迁移已经完成。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0，日志未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-16：`RenderTargetCreateInfo` 已从 `IRenderTarget.h` 中拆出为独立头，runtime/editor 侧那些只是为了保存 attachment spec 而被动 include `IRenderTarget.h` 的位置已改为直接依赖 `RenderTargetCreateInfo`；同时仓库内确认没有任何实例化/接线调用面的 `Render/Pipelines/ShadowMapping.{h,cpp}` scaffold 已删除，连带清掉了一批无用的 `IRenderTarget` forward declaration 与 include。
- 直接收益：这一步没有假装“已经删除 RT backend owner”，但把真正的前置清场做实了。现在业务侧描述 viewport / gbuffer attachment 规格已经不再被 legacy owner 接口绑住，`IRenderTarget` 的剩余调用面更真实地收缩到 backend `createRenderTarget()` / `VulkanRenderTarget` compatibility 本体，而不是继续被一堆假依赖和孤立 scaffold 淹没。
- 当前停止线：`IRenderTarget` / `VulkanRenderTarget` 类型本身、backend `createRenderTarget()` 入口以及 `recreateImagesAndFrameBuffer()` 里的 image/view/framebuffer physical owner 逻辑仍然存在；所以下一步应该继续处理 dead API/backend owner 本体，而不是再回头整理 runtime 规格定义。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0，日志未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-16：`RenderingInfo.renderTarget` / Vulkan command-buffer render-target recording path 与 RT Editor 的 `entry.rt` fallback 已删除。调查确认当前仓库中已经没有任何生产者在构造 `RenderingInfo.renderTarget`，也没有任何 RT editor catalog 条目再回填 concrete `IRenderTarget*`；因此 Vulkan command buffer 里那条 `renderTarget -> beginFrame()/buildRenderTargetAttachmentSet()/render-pass-or-dynamic-rendering` 兼容分支，以及 GUI 里依赖 `entry.rt->getCurrent*Texture()` 的 fallback 读取都只是被动残留。
- 直接收益：Phase 8 又少掉了一层“虽然代码还在、但上层已经没人再走”的 compatibility 数据面。现在 runtime/editor 的渲染 attachment 输入统一收口为显式 `RenderAttachmentSet` / attachment snapshot，Vulkan command buffer 不再为了一个无人生产的 `renderTarget` 模式保留第二套 begin/end recording 分支；RT Editor 也不再把 concrete `IRenderTarget` 当兜底真相源。
- 当前停止线：这一步还没有删除 `IRenderTarget` / `VulkanRenderTarget` 类型本身，也没有触及 backend `createRenderTarget()` 和 `recreateImagesAndFrameBuffer()` 里的 physical owner/create 实现；因此剩余工作更明确地收缩为“删掉 dead API/scaffold 或继续拆 backend owner 本体”，而不是继续维护 render-target recording 数据面。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0，日志未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-16：调查 `RenderTargetPool` 后确认它在当前仓库中已经没有任何调用方或初始化入口，只剩孤立的 `RenderTargetPool.{h,cpp}` 自身实现被动参与编译。基于这一事实，本计划把“明确 `RenderTargetPool` 与 graph resource registry 的停止线”收口为：`RenderTargetPool` 不再迁移到新资源模型，直接作为 dead legacy owner 路径删除，而不是继续给一段无人使用的 `IRenderTarget` pool 适配显式 `RenderImage` owner。
- 直接收益：这次不是再做一轮 facade 整理，而是明确消掉了一条原本会误导 agent 继续投入的伪迁移目标。Phase 8 里剩余真正需要处理的 owner/create 职责因此更聚焦到 Vulkan framebuffer compatibility / `VulkanRenderTarget::recreateImagesAndFrameBuffer()` 本身，而不再被一个无调用面的 pool 分散优先级。
- 当前停止线：删除 dead `RenderTargetPool` 之后，`IRenderTarget` 的 image/view/framebuffer 创建职责仍完整保留在 Vulkan compatibility 实现内；所以 Phase 8 离“删除 `IRenderTarget` 的 image/view 创建职责”还差 backend framebuffer 路径本体，而不是差一个更高层的 pool 包装。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0，日志未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-16：Forward viewport 已不再通过 `viewportRT` 这个 legacy `IRenderTarget` bundle 创建和持有 color/depth attachment。`ForwardRenderPipeline` 现在直接按 `_viewportRTSpec` 通过 resource factory 创建显式 `RenderImage` owner，并只为少量兼容消费者保留 `Texture::wrap()` 得到的 depth/color compat 视图；RT Editor 的 Forward Viewport 条目也改成直接消费 attachment snapshot，而不是再把一个 concrete RT 暴露给 GUI 兜底读取。
- 直接收益：Phase 8 又少掉了一个真实的 `IRenderTarget` physical owner 用例，而且这次不是 presentation 外圈，而是 Forward 主 viewport 本身。Forward viewport 的 render pass 录制、当前帧 owner 保活、postprocess 输入和 editor 预览现在都统一回到了显式 `RenderImage` snapshot 语义，`IRenderTarget` 在 runtime 主路径里的 owner 角色进一步缩到 `RenderTargetPool` 和 Vulkan framebuffer compatibility 支线。
- 当前停止线：这一步仍然没有触及 `VulkanRenderTarget::recreateImagesAndFrameBuffer()` 负责 image/view/framebuffer physical owner 的 backend 兼容实现，也还没有定义 `RenderTargetPool` 与 graph registry 的最终停止线；因此 `todo.md` 里“删除 `IRenderTarget` 的 image/view 创建职责”暂时还不能勾掉，只是 Forward viewport 已经先脱离这条链。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0，日志未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-16：presentation 路径已不再通过 `_screenRT` 这个 legacy `IRenderTarget` owner 包装 swapchain image。`RenderRuntime` 现在直接为 swapchain 每个 backbuffer 建立显式 `RenderImage` owner，presentation graph import、editor RT catalog、GUI inspection 和 presentation screenshot 都统一改为消费当前帧 `RenderImage`；`_screenRT` / `_screenRenderPass` 与对应的 `getPresentationTexture()` compat 出口已删除。
- 直接收益：Phase 8 少掉了一个真实的 `IRenderTarget` physical owner 用例，而不是只收 facade 表层。presentation 这条链现在和 deferred viewport/export 一样回到了显式 image owner + current-frame snapshot 语义，后续继续删除 `IRenderTarget` 的 image/view 创建职责时，范围可以更专注地落在 Forward viewport 和 RenderTargetPool，而不必再顾虑 swapchain/presentation 这条最外圈路径。
- 当前停止线：这一步还没有触及 `VulkanRenderTarget::recreateImagesAndFrameBuffer()` 负责创建 attachment image/view/framebuffer 的核心 owner 逻辑；Forward viewport 和 `RenderTargetPool` 仍通过 backend `createRenderTarget()` 获得 legacy owner bundle，所以 `IRenderTarget` 的 image/view 创建职责还没有真正删除，只是 presentation 已经先脱离。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0，日志未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-16：`VulkanRenderTarget` 与 `IFrameBuffer::create()` 内部残留的 `App::get()` 依赖已继续收口到显式 backend 参数。`VulkanRender::createRenderTarget()` 现在把当前 `VulkanRender*` 直接传给 `VulkanRenderTarget`；`VulkanRenderTarget::onInit()` 与 `IFrameBuffer::create()` 则只消费调用方已提供的 render backend，不再自己回看全局 `App` 决定 API 或 renderer。
- 直接收益：Phase 8 的 owner/create 链边界更干净了。`createRenderTarget() -> VulkanRenderTarget -> IFrameBuffer::create()` 这条路径现在已经是纯 backend-owned 调用链，为下一步继续把 image/view/framebuffer physical owner 从 `IRenderTarget` 本体剥离，先清掉了一层全局状态耦合。
- 当前停止线：这一步仍然没有改变 `VulkanRenderTarget::recreateImagesAndFrameBuffer()` 负责分配 attachment image，并通过 framebuffer 派生默认 image view 的事实；也就是说，`IRenderTarget` 的 image/view 创建职责还在，只是创建链不再偷偷依赖 `App::get()`。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0，日志未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-16：obsolete `ya::createRenderTarget()` 全局工厂已删除，render target 创建职责收口到 backend-owned `IRender::createRenderTarget()`。`RenderRuntime` presentation、`ForwardRenderPipeline` viewport rebuild 和 `RenderTargetPool` 现都改由当前 render backend 直接创建 `IRenderTarget`；Vulkan 后端在 `VulkanRender` 内部实例化 `VulkanRenderTarget`，OpenGL 暂以 `UNIMPLEMENTED()` stub 保持编译边界，`IRenderTarget.cpp` 这条 `App::get() -> switch(api)` 的旧全局工厂路径已完全移除。
- 直接收益：render target 创建现在和 buffer/image/view/sampler 一样回到 backend owner 边界，Phase 8 又少了一条“抽象层静态入口偷偷回看全局 App 状态”的 legacy 逃生口。后续继续删除 `IRenderTarget` 的 image/view 创建职责时，可以直接把 replacement/owner 逻辑从 `IRender` 向更小的 runtime bundle 下沉，而不用再先清一轮全局 factory facade。
- 当前停止线：这一步只删除了废弃工厂路径，还没有删除 `IRenderTarget` 自己的 image/view/framebuffer 创建职责；Forward viewport 与 presentation 仍然通过 backend `createRenderTarget()` 拿到 legacy owner bundle，所以 Phase 8 下一优先级仍是把 physical resource owner 从 `IRenderTarget` 本体继续剥离。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `make r t=HelloMaterial r_args="--exit-after-frame=400 --log-level=info --log-detail-level=error"` 退出码 0，日志未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-15：runtime/editor 对 `IRenderTarget` 的“改内部 desc，等 beginRendering 时 flushDirty()/refreshIfNeeded() 被动修复”协议已进一步删除。RT Editor 现在只通过 active pipeline 的 owner-aware format API 改 pipeline-owned spec，不再回退写 `IRenderTarget::set*AttachmentFormat()`；presentation resize 也改为在 swapchain 回调里显式 `recreate()`，不再先 `setExtent()` 再依赖 Vulkan command buffer beginRendering 时兜底刷新。配套地，`IRenderTarget` 上那组 dead dirty helper / dirty reason facade 与 `VulkanCommandBuffer::beginRendering()` 里的 `flushDirty()` 兜底一起删除。
- 直接收益：这一步把“runtime 外部可随时 mutate render target 内部状态，再由 backend 在录制时偷偷修复”的旧协议收掉了。当前剩余的 `IRenderTarget` 语义更接近一个显式 owner + framebuffer bundle，而不是半个 mutable spec 容器；这让后续继续删除 RT 的 image/view 创建职责时，不必再额外顾虑 editor / presentation 仍在依赖 lazy dirty repair。
- 当前停止线：`IRenderTarget` 仍然负责创建并持有 framebuffer/image owner，`createRenderTarget()` / `VulkanRenderTarget::recreateImagesAndFrameBuffer()` 这条 factory/owner 路径还在；本批没有把 presentation/viewport owner 完全替换成更小的 bundle 类型，也还没触及 Forward graph 主链。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=info --log-detail-level=error` 退出码 0，日志过滤未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-15：`RenderingInfo` 已从嵌套 `ImageSpec`/外置 clear value 改为 non-owning `RenderAttachmentSet`。`RGRenderContext` 现在会把 renderArea、layerCount、clear/load/store/finalLayout 与 color/depth image view 一起快照成 attachment set；Vulkan manual dynamic rendering 路径直接消费这些 attachment，并在显式 resolve 情况下记录 resolve image/view/mode。Forward viewport pass 也同步切到显式 attachment snapshot，只把 `viewportRT` 暂留为 legacy resource bundle owner。
- 直接收益：Phase 8 终于有了真正可执行的 attachment 数据面，而不是继续围绕 facade 做无行为变化的小重构。submit-time 生命周期仍留在 command buffer retain 机制里，attachment set 自身不持有 owner；这让 Deferred graph、Forward viewport 和 Vulkan dynamic rendering 三条链第一次对齐到同一种 non-owning attachment 契约，同时没有把范围扩大到 render-pass/framebuffer 兼容路径。
- 当前停止线：Vulkan framebuffer/render-pass 路径还没消费 attachment set，`viewportRT` 也还保留 image/view owner 与 compatibility facade 职责；这两项继续留在 Phase 8 后续，而不是在本批里提前做全局 `IRenderTarget` 拆除。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=info --log-detail-level=error` 退出码 0，日志过滤未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-15：Vulkan `renderTarget` 兼容分支现在也会先解析成同一种 effective `RenderAttachmentSet`，再分别驱动 render-pass begin info、dynamic-rendering attachment info，以及 begin/end rendering 的 initial/final layout transition。显式 attachment set 路径会校验它与当前 framebuffer snapshot 一致；老的 `renderTarget`-only 调用则按当前 framebuffer + attachment desc 自动补出 non-owning attachment set。
- 直接收益：Phase 8 里 “manual image path 是 attachment set，render-target path 还是旧并行协议” 的分叉被收掉了。现在 Vulkan backend 的两条 graphics beginRendering 录制路径都会消费同一份 attachment 语义，后续继续削减 `IRenderTarget` 时，不必再先改一遍 render-pass / framebuffer 兼容层的数据面。
- 当前停止线：这一步仍然保留 `IRenderTarget` 负责 physical owner、framebuffer 切换与 compatibility begin/end frame；并没有在本批里提前删除 `flushDirty()`、dirty reason 或 factory 路径。下一批应继续围绕这些 owner/facade 职责收敛，而不是回到 attachment helper 层做无行为变化的小拆分。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 62/62 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=info --log-detail-level=error` 退出码 0，日志过滤未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|RenderGraph compile failed|VUID`
- 2026-07-14：Forward viewport 的 extent / attachment format 控制面已开始从 `IRenderTarget` dirty protocol 回收到 pipeline-owned spec。`ForwardRenderPipeline` 新增 `_viewportRTSpec`，viewport resize 和 RT editor color-format 变更现在都会先更新 spec，再在 frame boundary 显式 `recreateViewportRenderTarget()`；`refreshViewportSnapshot()` 也改为直接从 spec 取格式真相，而不是回读 RT desc。
- 直接收益：Forward 不再依赖 `viewportRT->setExtent()/setColorAttachmentFormat()/needsAttachmentRefresh()/refreshIfNeeded()` 这套 legacy 自修复协议来维持 viewport 资源一致性。当前帧 snapshot format、stage pipeline format 和实际 RT replacement 开始重新对齐，这和 Deferred 之前已经走过的收口方向一致。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
- 2026-07-14：`ShadowMapResources` 不再继续向 Forward / Deferred 暴露 `IRenderTarget` 的 dirty-repair 包装。shadow 资源现在只保留 render target owner、depth image owner 和 sampled view rebuild 职责；Forward 删除了无意义的 `flushShadowResources()`，Deferred 在 shadow 整套 destroy/init 后也不再额外调用 `refreshIfNeeded()`。
- 直接收益：shadow 生命周期边界与当前真实实现重新对齐了。两条 pipeline 的 shadow 刷新本来就是整套 replacement，这一步把 legacy RT “脏了就自修复”的旧协议从 shadow facade 里收掉，后续继续压缩 `IRenderTarget` 职责时，不会再被这层包装重新扩散。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*'` 45/45 通过
  - `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，过滤后未见 `Validation Error|[ERROR]|ASSERT|SIGTRAP|EXC_|stack buffer overflow|vkCreateImageView failed|Fatal|abort|AddressSanitizer`
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

- 2026-07-14：Deferred GBuffer 四个 color attachment、shared depth 与 viewport color 已从 `_gBufferRT/_viewportRT` owner 迁到统一主图的 `ERGResourceLifetime::Persistent` 资源。`RenderGraphResourceRegistry` 现在按 graph desc 负责初始创建、viewport resize 和 editor format change 的 replacement，pipeline snapshot 通过 shared resolve 保留当前 physical resource，并继续向 stage/debug/editor/automation 导出稳定 `RenderImage`。
- 直接收益：Deferred 主链不再依赖 `IRenderTarget` 创建、dirty/recreate 或 framebuffer attachment 回读；GBuffer、shared depth、viewport、SSAO、bloom 与 postprocess output 的 intermediate owner 已统一落在同一个 graph registry。shared depth 也只保留一个逻辑 handle，GBuffer 写入与后续 skybox/overlay depth load 的依赖和 barrier 不再被两个 imported alias 隔断。
- RT Editor catalog 已可直接消费 attachment snapshot（format、extent、owner/image view），Deferred 不再为了 editor preview 保留重复的 `IRenderTarget` owner；Forward/presentation 仍继续使用 legacy RT fallback，未提前扩大 Phase 8 范围。
- postprocess/bloom 增加 graph-handle 输入路径，Deferred viewport color 不再在同一张 graph 中被二次 import；Texture/RenderImage 兼容入口仍保留给 Forward 和独立 execute 路径。
- registry replacement/prune 增加 texture label + handle trace，调查确认 Deferred physical resources 只在初始化和显式 viewport resize 时 replacement；运行中持续 replacement 的 `Presentation.Output` 是独立既有 imported-wrapper 行为，不属于本批。
- 验证结果：
  - `make b t=HelloMaterial` 通过
  - `xmake run ya-testing -- --gtest_filter='RenderGraphCoreTest.*:ResourceStateTrackerTest.*:AppAutomationConfigTest.*:OffscreenAsyncTest.*'` 57/57 通过
  - 默认 Deferred `HelloMaterial --exit-after-frame=400 --log-level=warn --log-detail-level=error` 退出码 0，关键错误过滤为空
  - Deferred viewport resize automation 在 frame 50 应用 `960x540`，frame 220 正常退出，关键错误过滤为空
- Forward switch 调查：frame 200 切换可执行，但当前 Apple M5/Vulkan 基线会暴露 Forward 自身的 skinned descriptor layout、Unlit push constant/GLSL、geometry shader capability 与 shadow 未写 layer validation error。它们在 Forward pipeline 初始化/执行后出现，默认 Deferred 与 resize smoke 均不出现；按当前停止线记录为 Forward 基线问题，不混入本次 Deferred owner 迁移。
- 2026-07-14：Deferred shadow sampling handoff 已从 graph 外 `transitionImageLayoutAuto()` 迁入主图。主图以 full-array `View2DArray` subresource descriptor 导入 shadow depth，并在 Deferred Light pass 声明 sampled read；compiler/state tracker 因此会覆盖 directional reserved layers 与全部 point-light cube layers，同时 registry 保活 graph 创建的 full-array view。
- 直接收益：shadow pass 仍可只写本帧需要的 layer，但 lighting descriptor 可能覆盖的更宽 layer 集会由同一份 graph state plan 统一进入 `ShaderReadOnlyOptimal`，不再依赖 pipeline 手写 barrier。默认 Deferred 400 帧 smoke 退出码 0，未出现此前的 `VUID-vkCmdDraw-None-09600` 或其他 validation/error。
- 2026-07-14：RenderGraph imported buffer descriptor 已补 `initialState`，公共 resource state 增加 Host stage/access 到 Vulkan 映射；executor 首次接触 imported buffer 时会从声明状态 seed barrier，而不是默认从空状态推断。Deferred GBuffer 在 graph declaration 前完成当前 flight 的 CPU upload，并把 frame UBO、light UBO、skinning SSBO 以 `HostWrite -> ShaderRead` 契约导入；GBuffer/Deferred Light pass 分别声明实际读取集合。
- 直接收益：frame/light/skinning descriptor 背后的 buffer 首次成为 graph 可见依赖，而且没有用“未知 -> ShaderRead”的虚假 barrier 掩盖 CPU 写入来源。新增 core test 精确校验 Host/HostWrite 到 AllCommands/ShaderRead 的 barrier，相关测试 58/58 通过，默认 Deferred 400 帧 smoke validation/error 为空。
- 2026-07-15：RenderGraph buffer access 已从复用 texture access 拆为独立 `ERGBufferAccess`，新增 compute `ShaderReadWrite`、`IndirectRead` 与 buffer transfer 状态/usage/dependency 语义。`PointShadowCullPass` 现在持有持久 executor，将 instance/frustum reads、draw-command read-write、visible-instance write，以及后续 indirect/vertex reads 编译为两段 cull 子图；原有三条手工 `bufferMemoryBarrier()` 已删除。
- 直接收益：point-shadow compute -> indirect draw/vertex shader 的 barrier 由 graph compiler/executor 生成，draw-command 的 `StorageBuffer | IndirectBuffer` usage 也会被精确校验；既有 copy pass 也改为显式声明 `TransferSrc/TransferDst`，不再借用 shader read/write。新增 core test 覆盖 compute read-write 到 indirect-read dependency；相关测试 60/60 通过，默认启用 point indirect+cull 的 Editor 配置下 400 帧 smoke 正常退出且无 graph/Vulkan validation error。
- 2026-07-15：Directional shadow 与每个 point-light cube face 的 depth-only raster recording 已改由各自持久 `RenderGraphExecutor` 执行；depth layer view、frame/face UBO、skinning SSBO 以及 indirect draw/visible-instance buffer 都在 pass setup 中显式声明，pass callback 不再手工构造 `RenderingInfo` 或调用 command-buffer begin/end rendering。为此补齐了 `RGRenderContext` 的 depth-only raster 支持及 core test。
- 调查结论：这些 shadow pass 暂时保持独立子图。当前 compiler 以 handle 建依赖，不能识别同一 array image 的 per-layer imported view 与 full-array sampled view alias；因此不能在没有显式依赖/alias 语义的情况下宣称 shadow 已统一并入 Deferred 主图。相关测试 61/61 通过，默认 Editor 400 帧 smoke 正常退出且 graph/Vulkan validation/error 过滤为空。
- 2026-07-15：RenderGraph 新增显式 pass dependency；`ShadowStage`/`BasicShadowMapTechnique` 现在把 directional、optional point cull 与 point face raster pass append 到 Deferred 主图，Deferred Light 显式依赖最后一个 shadow pass。per-layer view 与 full-array sampled view 不再靠插入顺序维持先后，Deferred 路径也不再单独调用 legacy shadow `execute()`。
- 直接收益：Deferred shadow/main/postprocess GPU recording 已由同一主图调度，现阶段无需为单一路径引入通用 imported-view alias 推断。新增 core test 覆盖显式依赖边与拓扑顺序；相关测试 62/62 通过，默认 Editor 400 帧 smoke 正常退出且 graph/Vulkan validation/error 过滤为空。
- 2026-07-15：公共 `ShadowMapResources` 已删除 attachment-owning `IRenderTarget`，改为通过 resource factory 直接持有 cube-compatible depth array image 与派生 views；销毁顺序也固定为先 views、后 image。Deferred/Forward 的 shadow existence/refresh 判断统一改查显式 image owner。
- RT editor 的 shadow 条目同步改为消费显式 depth view/format/extent，depth format 修改通过 owner-aware pipeline API 推迟到安全 refresh 点，不再修改一个不会被 flush 的 legacy RT dirty state。旧的 `IRenderPipeline::setSharedDepthFormat()` facade 与 RenderRuntime 转发已删除。相关测试 62/62 通过，默认 Editor 400 帧 smoke 正常退出且 graph/Vulkan validation/error 过滤为空。

## 代码对账快照

本节基于当前主路径代码抽样复核，用于校正文档对现状的判断，不替代 `todo.md`。

### 资源模型 / Factory

- `IRenderResourceFactory` 已成为 buffer/image/view/sampler 的真实创建入口，buffer 迁移基本完成
- `RenderImage` 已作为中间 GPU image owner 稳定落地，`IImageView` 也已明确为 non-owning image projection
- `IRenderTarget.cpp` 这条全局 render target factory 已删除，`IRenderTarget` 创建职责现已回收到 backend-owned `IRender::createRenderTarget()`
- 但 `Texture.cpp`、`Swapchain.cpp` 等处仍残留 `App::get()` 或 legacy factory 依赖，说明“资源创建不依赖全局应用状态”尚未完成

### Resource State / Graph Core

- `ResourceStateTracker`、`RenderGraphExecutor`、`RenderGraphResourceRegistry` 已不再停留在设计稿，且已有独立 core tests 覆盖 generation handle、imported view range、replacement、executor smoke、render helper、state tracker 等契约
- `RenderGraphExecutor` 已能对 compiled texture/buffer state plan 发射最小 transition/barrier，并可驱动真实 runtime pass
- 但 executor 仍按“遍历 pass 时线性扫描全量 state plans”的最小实现工作，barrier backend 也还没有完全收口所有 legacy/manual 路径

### Deferred 主链

- Deferred 已经不是“少量试点 pass”，而是存在一个统一的 `executeDeferredMainGraph()`，将 GBuffer、SSAO、Deferred Light、Skybox、Scene Overlay、Viewport Overlay、Postprocess 串进同一张 graph
- `SSAOStage`、`BloomPostprocessing`、`PostProcessingStage` 都已改为持久化 `RenderGraphExecutor` + persistent graph texture 的真实 runtime 路径
- GBuffer、shared depth、viewport、SSAO、bloom 与 postprocess output 已由同一个 registry 持有并按 desc replacement；shadow image 的全 layer sampling handoff 也已由 graph state plan 生成，剩余缺口是 frame/light/skinning 与 point-shadow compute/indirect buffer 尚未形成完整 graph 声明

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
