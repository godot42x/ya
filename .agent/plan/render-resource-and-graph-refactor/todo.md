# GPU 资源模型与 RenderGraph 联合重构 TODO

## 使用说明

本清单对应同目录 `plan.md`，并接替 `.agent/plan/render-architecture-refactor/todo.md`。

推进规则：

- 先建立最小资源核心，再让 RenderGraph 成为第一个完整消费者
- 不要求迁完所有旧资源调用点后才开始 RenderGraph
- 每项完成后立即更新状态和必要的迁移备注
- 纯 helper 拆分、callback 搬运和 provider 转发不计入有效进度
- OpenGL 本轮冻结，不作为构建和运行验收目标

状态：

- `[ ]` 未开始
- `[-]` 进行中
- `[x]` 已完成
- `[~]` 明确延后
- `[-x]` 停止，不再执行

## Phase 0: 旧计划收口与基线

- [x] 在旧 `render-architecture-refactor` 文档顶部标记已被本计划接替
- [x] 回退当前 `DeferredRenderPipeline::applyExternalGBufferMutation()` / `applyExternalViewportMutation()` 纯提取改动
- [x] 确认工作区没有其他仅格式化或无行为价值的残留修改
- [x] 记录当前 Vulkan validation 基线（见 `baseline.md`）
- [ ] 记录 Forward 默认场景截图基线
- [ ] 记录 Deferred 默认场景截图基线
- [x] 建立固定帧数运行入口（`--exit-after-frame=120`）
- [x] 修复 `--automation-config=<path>` 被解析但未生效的问题
- [ ] 运行 `AppAutomationConfigTest`（当前被既有 `ConstructorReflectionTest.cpp:46` 编译错误阻塞）
- [x] 建立 viewport resize 冒烟入口
- [x] 建立 shadow 开关/分辨率冒烟入口
- [ ] 建立 SSAO、bloom、postprocess、ACES 冒烟入口
- [x] 建立 Forward/Deferred switch 冒烟入口

基线备注：

- Codex 当前 macOS smoke 环境下，`HelloMaterial` 已可在补齐 Vulkan runtime env 后完成 `--exit-after-frame=3` 冒烟并正常退出；启动链上此前的 shadow cube layer 分配崩溃、Bloom/Postprocess/SSAO graph 悬空 handle 和 presentation graph layout/present 错误已被修复
- `smoke.viewportResize.{width,height,frame}` 与 `smoke.renderPipeline.{target,frame}` 现已接入 automation config，并在 `AppAutomationConfigTest` 中覆盖解析；运行期动作通过现有 editor pending viewport resize 与 `RenderRuntime::setPendingRenderPipeline()` 路径触发
- `shadow.resolution` 现已补入 automation overrides，并与已有 `shadow.quality / directionalEnabled / pointLightEnabled / filter ...` 一起通过 `ShadowSettingsConfig` 进入 runtime shadow settings；`AppAutomationConfigTest` 已覆盖解析

完成标准：

- [ ] 旧计划不会再误导 agent 继续低收益小改
- [ ] 后续资源或 graph 迁移有可重复行为基线

## Phase 1: 资源 API 盘点与契约

- [x] 盘点 `IBuffer::create()` 全部调用点并按用途分类（见 `resource-api-inventory.md`）
- [x] 盘点 `ITextureFactory` 全部调用点（见 `resource-api-inventory.md`）
- [x] 盘点 `Texture::from* / createCubeMap* / createRenderTexture()` 全部调用点（见 `resource-api-inventory.md`）
- [x] 盘点 `Texture::wrap()` 并区分 existing image、subresource view 和 fake texture binding（见 `resource-api-inventory.md`）
- [x] 盘点 cubemap/face/mip/layer 派生 view（见 `resource-api-inventory.md`）
- [x] 盘点 render target attachment、swapchain image 和 external image 创建路径（见 `resource-api-inventory.md`）
- [x] 盘点 sampler 和 framebuffer 创建路径（见 `resource-api-inventory.md`）
- [x] 盘点 `beginIsolateCommands()` upload/init 路径，分离 allocation、upload 和 initial transition（见 `resource-api-inventory.md`）
- [x] 盘点 `Texture*` 作为 GPU 中间资源 descriptor 输入的使用点
- [ ] 记录每类资源当前 owner、引用者和销毁顺序
- [x] 记录当前 `Texture / IImage / IImageView / IRenderTarget / IFrameBuffer` 所有权链（见 `resource-api-inventory.md`）
- [ ] 定义 `BufferDesc`
- [ ] 定义 `ImageDesc`
- [ ] 定义 `ImageViewDesc`
- [ ] 收敛现有 `SamplerDesc`
- [x] 定义 external/imported image descriptor
- [ ] 定义 imported image 的 native ownership、view ownership、debug name 和 initial/final state
- [ ] 定义 derived image-view identity/cache key
- [x] 定义资产 `Texture` 与 transient/persistent GPU image 的绑定边界（GPU 中间资源使用 `RenderImage`，后续由 graph registry 接管）
- [ ] 明确 `RenderTargetPool` 与 graph resource registry 的停止线
- [ ] 锁定 image view 不拥有 image 的生命周期规则
- [ ] 锁定 resource desc 创建后不可变规则

完成标准：

- [ ] 新 API 不包含 Vulkan 类型
- [ ] 每类资源 owner 和非拥有引用规则明确
- [ ] 新旧 API 有完整迁移映射

## Phase 2: 统一 Resource Factory

- [x] 定义 `IRenderResourceFactory` 的 Buffer/Sampler 最小职责
- [x] 由 `IRender` 暴露 backend-owned factory
- [x] Vulkan 实现 buffer 创建
- [x] Vulkan 实现 image 创建
- [x] Vulkan 实现 image view 创建
- [x] Vulkan 实现 sampler 创建
- [x] Vulkan 实现 external image wrapping
- [x] 统一新 factory 路径的 debug label 设置
- [ ] 为失败创建路径定义一致的 assert/error 行为
- [x] 删除 `Sampler::create()` 全局静态入口
- [x] 迁移并删除 `IBuffer::create()` 静态入口
- [x] 禁止新代码调用 Buffer/Sampler 静态 resource factory
- [x] 删除 `ITextureFactory` / `VulkanTextureFactory` 和专用 cubemap view API
- [x] 将残留高层描述从 `TextureFactory.h` 移到 `TextureCreateInfo.h`

完成标准：

- [ ] 新 factory 可独立创建全部基础 GPU 资源
- [ ] factory 不依赖 `App::get()`
- [ ] Vulkan native handle 只存在平台实现层

## Phase 3: Buffer 迁移

- [x] 迁移 staging/readback buffer
- [x] 迁移 vertex/index buffer
- [x] 迁移 uniform/storage buffer
- [x] 迁移 indirect buffer
- [x] 迁移 material descriptor pool 内 buffer
- [x] 迁移 shadow per-flight buffer
- [x] 迁移 screenshot readback buffer
- [ ] 统一 map/write/flush 范围和错误检查
- [x] 删除 `IBuffer::create()`
- [x] 删除 Buffer.cpp 中的 backend switch

完成标准：

- [x] 所有 buffer 通过 `IRenderResourceFactory` 创建
- [ ] buffer 生命周期和 memory usage 可从 desc 判断

## Phase 4: Texture / Image / ImageView 分层

- [ ] 提取 texture decode/import 职责
- [ ] 提取显式 texture upload service
- [ ] 迁移 2D 资产纹理
- [ ] 迁移 cubemap 资产纹理
- [ ] 迁移 fallback texture
- [ ] 让 `Texture` 显式拥有 image 和 default view
- [x] 将 SSAO render texture 改为 GPU image/view owner
- [x] 将 bloom render texture 改为 GPU image/view owner
- [x] 将 postprocess output render texture 改为 GPU image/view owner
- [x] 将 BRDF LUT 输出改为 GPU image/view owner
- [x] 将 shadow sampled view 改为显式 image/view owner
- [ ] 将 screenshot scratch texture 改为 GPU image/view owner
- [ ] 删除 `Texture::createRenderTexture()`
- [ ] 删除 `Texture::getTextureFactory()`
- [ ] 删除 `ITextureFactory`
- [ ] 删除资源创建路径中的 `App::get()`

完成标准：

- [ ] `Texture` 只表示资产纹理
- [ ] GPU 中间资源不再包装为资产 `Texture`
- [x] image/view 创建入口唯一

迁移备注：

- `RenderImage` 仅组合并拥有 `IImage` 与 default `IImageView`，不承载资产语义、采样器或资源状态
- `RenderingInfo::ImageSpec` 已直接引用 image/view，dynamic rendering attachment 协议不再依赖 `Texture`
- BRDF LUT、Deferred SSAO 与 bloom intermediates 已迁移
- postprocess output 已由 `PostProcessingStage` 以 `RenderImage` 形式持有；剩余 `Texture::wrap()` 兼容层只保留在 pipeline viewport 输出侧，供 editor viewport / screenshot fallback 复用
- shadow sampled views 已由 `ShadowMapResources` 显式拥有；shadow pass 内残留的 `Texture::wrap()` attachment adapter 归入 Phase 8
- screenshot scratch 仍待迁移

## Phase 5: Resource State Tracker

- [x] 盘点所有 `transitionImageLayout*()` 调用点
- [x] 盘点 attachment initial/final layout 语义
- [x] 盘点 swapchain acquire/present 状态
- [x] 盘点 cubemap mip/layer transition
- [x] 定义 buffer/image resource state
- [x] 定义 mip/layer/aspect subresource key
- [x] 实现 command-buffer-local `ResourceStateTracker`
- [x] 将 legacy image transition 和 dynamic-rendering attachment transition 接入 tracker
- [x] 为冲突状态和遗漏 transition 增加 debug validation
- [x] 定义 imported resource initial/final state
- [x] 停止以 `IImage::getLayout()` 作为执行状态真相
- [x] 删除或降级 image 全局 layout API

完成标准：

- [ ] 单个 command buffer 内状态变化可完整追踪
- [ ] 多 layer/mip transition 不再依赖单一 image layout
- [ ] graph 和 legacy 路径共用 barrier backend

迁移备注：

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

## Phase 6: RenderGraph Core

- [x] 定义带 generation 的 `RGTextureHandle`
- [x] 定义带 generation 的 `RGBufferHandle`
- [x] 定义 `RGTextureDesc` / `RGBufferDesc`
- [x] 实现 imported/transient/persistent resource declaration
- [x] 实现 `RGPassBuilder::read()`
- [x] 实现 `RGPassBuilder::write()`
- [x] 实现 color/depth attachment declaration
- [x] 实现 `RGPassContext` resource resolve
- [x] 实现 dependency graph 构建
- [x] 实现稳定拓扑排序
- [x] 实现 cycle 检测
- [x] 实现 read-before-write 校验
- [x] 实现非法 writer/usage 校验
- [x] 实现 compiled graph debug dump
- [x] 实现 `RenderGraphResourceRegistry`
- [-] 实现 Vulkan `RenderGraphExecutor`
- [-] 将 compiled state plan 接入 `ResourceStateTracker`
- [x] 建立最小 clear/copy graph 冒烟测试

完成标准：

- [ ] graph core 单元测试不需要启动完整 App
- [ ] Vulkan executor 可正确执行最小图
- [ ] pass execute 无需访问 graph 内部结构

迁移备注：

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
- shadow pass 内部录制路径也已开始脱离 `Texture` adapter：directional / point shadow depth attachment 现在直接使用 `IImage/IImageView`，`Texture::wrap()` 兼容对象仅保留给调试/预览输出，不再作为实际 beginRendering 输入
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

## Phase 7: Deferred Graph 迁移

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

### GBuffer

- [ ] 声明 GBuffer color/depth transient resources
- [ ] 迁移 GBuffer pass setup/execute
- [ ] 明确 frame/light/skinning buffer 为 imported 或 graph buffer
- [ ] 移除 GBuffer attachment 的 `IRenderTarget` ownership

### SSAO

- [x] 声明 GBuffer read 和 AO write
- [x] 迁移 SSAO pass
- [x] 删除 SSAO resize 手工重建
- [x] 由 graph registry 按 extent replacement

### Deferred Light

- [x] 声明 GBuffer、AO、shadow、environment reads
- [x] 声明 viewport HDR output
- [x] 迁移 fullscreen light pass
- [x] 移除 `GBufferStage*` 资源反查

### Overlay

- [x] 迁移 skybox pass
- [x] 迁移 debug overlay pass
- [x] 将 scene-derived overlay 输入作为 graph setup 输入（descriptor set 保持执行期参数，不建模为 imported resource）

### Postprocess

- [x] 迁移 bloom extract
- [x] 迁移 bloom blur ping/pong
- [x] 迁移 bloom composite
- [x] 迁移 ACES/tone map
- [x] 声明 presentation output
- [x] 删除 postprocess 手工 resize owner

### Pipeline 收口

- [ ] RenderGraph 接管 Deferred pass 顺序
- [ ] RenderGraph 接管 Deferred intermediate resource owner
- [ ] RenderGraph 接管 pass 间 barrier
- [x] 删除 Deferred `refreshDirtyResources()` attachment 修复路径
- [ ] 删除 Deferred viewport/SSAO/postprocess dirty resource state

完成标准：

- [ ] Deferred 主链路完全由 RenderGraph 执行
- [ ] resize 不调用 attachment `flushDirty()`
- [ ] 截图与迁移前基线一致
- [ ] Vulkan validation 无新增 error

## Phase 8: RenderTarget 收敛

- [ ] 定义 `RenderAttachmentSet`
- [ ] 将 load/store/clear value 从 resource owner 中分离
- [ ] Vulkan dynamic rendering 消费 attachment set
- [ ] Vulkan framebuffer/render pass 路径消费 attachment set
- [ ] legacy Forward 使用临时 resource bundle adapter
- [ ] 删除 `IRenderTarget` 的 image/view 创建职责
- [ ] 删除 `IRenderTarget::flushDirty()`
- [ ] 删除 dirty reason 通用重建协议
- [ ] 删除废弃 render target factory 路径

完成标准：

- [ ] attachment set 只引用资源，不拥有资源
- [ ] physical resource owner 唯一
- [ ] resize 表示资源规格变化和替换，不是对象内部突变

## Phase 9: Forward Graph 迁移

- [ ] 迁移 Forward shadow pass group
- [ ] 迁移 skybox pass
- [ ] 迁移 PBR pass
- [ ] 迁移 Phong pass
- [ ] 迁移 Unlit pass
- [ ] 迁移 Simple/direction overlay pass
- [ ] 迁移 debug pass
- [ ] 迁移 Forward postprocess
- [ ] 删除 ForwardViewport legacy attachment adapter
- [ ] 删除 Forward dirty render target refresh

完成标准：

- [ ] Forward 主链路完全由 RenderGraph 执行
- [ ] Forward pass 不再通过 stage callback 查询 GPU 资源
- [ ] Forward/Deferred switch 冒烟通过

## Phase 10: 外围 GPU 工作流迁移

- [ ] 评估 environment preprocess 使用独立 graph 还是 shared executor
- [ ] 迁移 cylindrical-to-cubemap
- [ ] 迁移 irradiance map
- [ ] 迁移 prefiltered environment map
- [ ] 迁移 BRDF LUT generation
- [ ] 迁移 screenshot copy/readback
- [ ] 明确 swapchain acquire/present graph 外边界
- [ ] 删除剩余 compatibility adapter

完成标准：

- [ ] 所有主要 GPU image 工作流使用统一资源和状态模型
- [ ] offscreen scheduler 仍在 graph 外，不与 graph owner 混合

## Phase 11: Editor Render Extension API

### Draw Submission Foundation

- [ ] 盘点 Deferred/Forward 现有 draw bucket、pipeline variant、material key、skinning 和 indirect 输入
- [ ] 定义 frame/view-local `RenderItem`，包含 mesh/submesh、material、transform、bounds、visibility 和 editor tag
- [ ] 定义 renderer-internal `DrawPacket`，包含稳定 pipeline/material/geometry key 和 per-draw data
- [ ] 定义 frame/view-local `DrawList`，只保存或引用 packet，不拥有 mesh/material 资产
- [ ] 将 visibility、LOD、queue 分类放在 packet/list 构建前
- [ ] 实现 opaque/material/pipeline 排序 key 和 stable ordering
- [ ] 保留 instancing/indirect grouping 边界，但首版继续使用 CPU list 和现有 draw path
- [ ] 将 Deferred 与 Forward 至少一个主材质路径接入共同 DrawList，证明不是 extension-only facade
- [ ] 为 gizmo、线框和 procedural geometry 保留独立 debug/primitive stream
- [ ] 禁止公开立即执行式 `drawMesh(mesh, material, transform)`

### Shader Parameter Foundation

- [ ] 盘点现有 Slang 生成头、pipeline layout 和 descriptor write 的参数映射
- [ ] 定义生成 parameter block 的资源字段、常量字段和 sampler policy
- [ ] 实现 parameter block 到内部 pipeline/descriptor binding 的 binder
- [ ] 保持 binder 为内部能力，公开类型不暴露 set/binding 和 push constant offset

### Extension Facade

- [ ] 定义 `IRenderExtension::build(RenderGraphBuilder&, const RenderView&)`
- [ ] 定义 shadow 后、lighting 后、tone mapping 前、viewport composite 前四个固定扩展点
- [ ] 定义 builtin texture/buffer identifier，并映射到当前 frame graph handle
- [ ] 提供不暴露 execute callback 的受限 `RenderGraphBuilder` facade
- [ ] 提供 `addFullscreenPass()`
- [ ] 提供 `addRasterPass()`
- [ ] 提供 `addComputePass()`
- [ ] 提供 `copyTexture()` 和 `compositeToViewport()`
- [ ] 由 Slang 反射/生成链产生强类型 shader parameter block
- [ ] 编译期校验 parameter block 字段与 texture/sampler/buffer 类型
- [ ] 禁止公开 API 使用 descriptor set/binding 和 push constant offset
- [ ] 定义 `RenderView::queryDrawList()`
- [ ] 支持按 render queue、材质域、可见性层和 editor selection tag 筛选 DrawList
- [ ] 迁移 selection/debug overlay 作为首批 extension API 消费者
- [ ] Render Diagnostics 展示 extension pass/resource/state dump
- [ ] 验证 extension 注册、禁用、热重载、viewport resize、多视图和移除
- [ ] 校验 extension 不跨帧保存 graph handle、resolved resource 或 frame-local DrawList

完成标准：

- [ ] 扩展作者可以添加 fullscreen、DrawList raster 和 compute pass
- [ ] extension public headers 不 include `CommandBuffer.h`、Vulkan header 或 descriptor/pipeline-layout 类型
- [ ] extension 不负责资源同步、transient 生命周期、descriptor allocation 或 pass 排序
- [ ] 非法资源依赖和 shader 参数不匹配在 graph/shader 编译阶段失败
- [ ] Deferred/Forward 和 extension 至少共享同一种 DrawList/DrawPacket 提交协议

## Phase 12: 清理与 OpenGL 恢复评估

- [ ] 删除旧 resource interfaces 和 factory
- [ ] 删除旧 render target dirty/recreate helper
- [ ] 删除失效 callback/provider forwarding
- [ ] 检查没有 `App::get()` 资源创建路径
- [ ] 检查没有 render attachment 使用资产 `Texture`
- [ ] 检查没有 graph pass 内资源创建或 `waitIdle()`
- [ ] 检查 Runtime/App 的非 executor/pass-internal 代码不直接调用 layout transition、descriptor binding 或 beginRendering
- [ ] 检查 editor extension public headers 不依赖 RHI command recording 类型
- [~] 实现 OpenGL resource factory
- [~] 实现 OpenGL graph executor 降级语义
- [~] 恢复 OpenGL 构建和运行测试
- [~] 目录重组和历史空壳清理

完成标准：

- [ ] Vulkan 主路径只保留新资源模型和 RenderGraph
- [ ] OpenGL 恢复工作有独立计划，不污染本轮 Vulkan 收口

## Test Matrix

### Resource Tests

- [ ] buffer 创建、map、write、flush、readback
- [ ] image 创建和销毁
- [ ] image view subresource range
- [ ] external image 非拥有销毁
- [ ] view-before-image 销毁顺序
- [ ] texture decode/upload 失败路径

### Graph Tests

- [ ] 空 graph
- [ ] 单 pass 单 output
- [ ] linear dependency
- [ ] branch and merge dependency
- [ ] cycle rejection
- [ ] read-before-write rejection
- [ ] stale generation handle rejection
- [ ] imported initial/final state
- [ ] resize physical resource replacement
- [ ] mip/layer subresource transition
- [ ] extension pass dependency and invalid usage rejection
- [ ] generated shader parameter block type mismatch rejection

### Draw Submission Tests

- [ ] DrawList queue/material/visibility/editor-tag filtering
- [ ] opaque sort key stable ordering
- [ ] RenderItem/DrawPacket 不拥有 mesh/material 资产
- [ ] Deferred/Forward 共用 DrawList 后截图与 draw count 基线一致
- [ ] debug/primitive stream 不进入普通 surface DrawList

### Runtime Tests

- [ ] `make b t=HelloMaterial`
- [ ] `make test`
- [ ] Forward 固定帧运行
- [ ] Deferred 固定帧运行
- [ ] viewport 连续 resize
- [ ] shadow 开关和分辨率变化
- [ ] SSAO 开关
- [ ] bloom/postprocess/ACES 开关
- [ ] Forward/Deferred switch
- [ ] screenshot/readback
- [ ] shutdown 无 validation/lifetime error
- [ ] 关键截图基线对比
- [ ] extension 注册、禁用、热重载和移除
- [ ] extension viewport resize 和多视图

## 持续审查项

- [ ] 新 GPU 资源是否都通过 `IRenderResourceFactory` 创建
- [ ] 新资源是否有唯一 owner
- [ ] view 是否可能比 image 活得更久
- [ ] render attachment 是否被错误包装为 `Texture`
- [ ] graph pass 是否声明了全部读写资源
- [ ] graph execute 是否出现资源创建、全局查询或 `waitIdle()`
- [ ] resize 是否通过 desc change + safe replacement 完成
- [ ] layout/barrier 是否只有 graph plan/tracker 一个状态来源
- [ ] 是否引入了无实际迁移价值的 helper/provider facade
- [ ] 是否把 OpenGL 冻结误解为允许公共接口泄漏 Vulkan 类型
- [ ] editor extension API 是否泄漏 command buffer、layout/barrier、descriptor 或 native handle
- [ ] extension 是否绕过 DrawList 直接提交逐 mesh 绘制
