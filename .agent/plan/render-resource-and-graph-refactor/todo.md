# GPU 资源模型与 RenderGraph 联合重构 TODO

## 使用说明

本清单对应同目录 `plan.md`，并接替 `.agent/plan/render-architecture-refactor/todo.md`。

推进规则：

- 先建立最小资源核心，再让 RenderGraph 成为第一个完整消费者
- 不要求迁完所有旧资源调用点后才开始 RenderGraph
- 每项完成后立即更新状态；阶段进展、调查结论和迁移备注统一写入 `progress.md`
- 纯 helper 拆分、callback 搬运和 provider 转发不计入有效进度
- OpenGL 本轮冻结，不作为构建和运行验收目标

当前焦点：

- 先闭合 Deferred 主链 graph/resource-state/lifetime
- 再删除 legacy attachment owner、dirty repair 和 `IRenderTarget` 重建语义
- Forward、extension API、OpenGL 恢复继续后置

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
- [x] 运行 `AppAutomationConfigTest`
- [x] 建立 viewport resize 冒烟入口
- [x] 建立 shadow 开关/分辨率冒烟入口
- [x] 建立低噪音 smoke 日志入口（`--log-level` / `--log-detail-level` 与 `smoke.log.*`）
- [ ] 建立 SSAO、bloom、postprocess、ACES 冒烟入口
- [x] 建立 Forward/Deferred switch 冒烟入口
- [x] 将 submit-time 生命周期 / non-owning view 规则沉淀到 `AGENTS.md`、`cpp-style`、`debug-review`

基线备注：

- 详见 `progress.md` 的 Phase 0 基线记录

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
- [x] 记录每类资源当前 owner、引用者和销毁顺序（见 `resource-ownership-inventory.md`）
- [x] 记录当前 `Texture / IImage / IImageView / IRenderTarget / IFrameBuffer` 所有权链（见 `resource-api-inventory.md`）
- [ ] 定义 `BufferDesc`
- [ ] 定义 `ImageDesc`
- [ ] 定义 `ImageViewDesc`
- [ ] 收敛现有 `SamplerDesc`
- [x] 定义 external/imported image descriptor
- [ ] 定义 imported image 的 native ownership、view ownership、debug name 和 initial/final state
- [x] 定义 derived image-view identity/cache key
- [x] 定义资产 `Texture` 与 transient/persistent GPU image 的绑定边界（GPU 中间资源使用 `RenderImage`，后续由 graph registry 接管）
- [x] 明确 `RenderTargetPool` 与 graph resource registry 的停止线
- [x] 锁定 image view 不拥有 image 的生命周期规则
- [x] 锁定 command-buffer / queue-submit 相关 attachment、descriptor、imported view 的最小保活边界
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
- [x] 将 screenshot scratch texture 改为 GPU image/view owner
- [x] 将 framebuffer/render-target attachment owner 改为 `RenderImage`
- [ ] 删除 `Texture::createRenderTexture()`
- [ ] 删除 `Texture::getTextureFactory()`
- [ ] 删除 `ITextureFactory`
- [ ] 删除资源创建路径中的 `App::get()`

完成标准：

- [ ] `Texture` 只表示资产纹理
- [ ] GPU 中间资源不再包装为资产 `Texture`
- [x] image/view 创建入口唯一

迁移备注：

- 详见 `progress.md` 的 Phase 4 迁移记录

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

- 详见 `progress.md` 的 Phase 5 迁移记录

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

- [x] graph core 单元测试不需要启动完整 App
- [x] Vulkan executor 可正确执行最小图
- [x] pass execute 无需访问 graph 内部结构

迁移备注：

- 详见 `progress.md` 的 Phase 6 迁移记录

## Phase 7: Deferred Graph 迁移

执行记录与调查结论：

- 详见 `progress.md` 的 Phase 7 执行记录

### GBuffer

- [x] 声明 GBuffer color/depth graph-owned persistent resources（editor/debug/automation 需要跨帧稳定身份，不采用 transient）
- [x] 迁移 GBuffer pass setup/execute
- [x] 明确 frame/light/skinning buffer 为 imported buffer，并声明 HostWrite initial state
- [x] 移除 GBuffer attachment 的 `IRenderTarget` ownership

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

- [x] Deferred postprocess graph 输入切到 `RenderImage` attachment owner
- [x] 迁移 bloom extract
- [x] 迁移 bloom blur ping/pong
- [x] 迁移 bloom composite
- [x] 迁移 ACES/tone map
- [x] 声明 presentation output
- [x] 删除 postprocess 手工 resize owner

### Pipeline 收口

- [x] RenderGraph 接管 Deferred pass 顺序
- [x] RenderGraph 接管 Deferred intermediate resource owner
- [x] RenderGraph 接管 Deferred 主链与 point-shadow cull 的 pass 间 barrier
- [x] Directional/point shadow depth-only raster recording 改为 graph-backed executor
- [x] 通过显式 pass dependency 将 shadow 子图统一并入 Deferred 主图
- [x] 删除 Deferred `refreshDirtyResources()` attachment 修复路径
- [x] 删除 Deferred viewport/SSAO/postprocess dirty resource state

完成标准：

- [x] Deferred 主链路完全由 RenderGraph 执行
- [x] resize 不调用 attachment `flushDirty()`
- [ ] 截图与迁移前基线一致
- [x] Vulkan validation 无新增 error（默认 Deferred 400 帧与 Deferred viewport resize smoke）

## Phase 8: RenderTarget 收敛

- [x] `ShadowMapResources` 删除 `IRenderTarget` owner，直接持有 depth image/views
- [x] 定义 `RenderAttachmentSet`
- [x] 将 load/store/clear value 从 resource owner 中分离
- [x] Vulkan dynamic rendering 消费 attachment set
- [x] Vulkan framebuffer/render pass 路径消费 attachment set
- [x] legacy Forward 使用临时 resource bundle adapter
- [x] 删除 `IRenderTarget` 的 image/view 创建职责
- [x] 删除 `IRenderTarget::flushDirty()`
- [x] 删除 dirty reason 通用重建协议
- [x] 删除废弃 render target factory 路径

完成标准：

- [x] attachment set 只引用资源，不拥有资源
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
- [x] 删除 ForwardViewport legacy attachment adapter
- [ ] 删除 Forward dirty render target refresh

完成标准：

- [ ] Forward 主链路完全由 RenderGraph 执行
- [ ] Forward pass 不再通过 stage callback 查询 GPU 资源
- [ ] Forward/Deferred switch 冒烟通过

## Phase 10: 外围 GPU 工作流迁移

- [x] 评估 environment preprocess 使用独立 graph 还是 shared executor
  - 2026-07-16 代码结论：当前保持 `ResourceResolveSystem -> OffscreenJobRunner -> OffscreenTaskService` 的独立 offscreen 调度更合理，不并入 Deferred/Postprocess 那类 caller-owned shared `RenderGraphExecutor`。原因是 offscreen preprocess 现在由 `AppFrameLoop` 在主渲染前单独 `tick()`、单独 fence/submit、按 job 生命周期跨帧完成；其三条 preprocess pipeline 仍直接面向 `ICommandBuffer` 录制 dynamic rendering 与手工 subresource transition，而不是像 Deferred/BRDF LUT/postprocess 那样在调用点同步构图并立即执行 graph。后续若继续推进，应优先考虑 source/result state 边界或把 offscreen job 内部收成 dedicated graph execute，而不是把 scheduler/owner 语义混进现有 shared executor。
- [x] 迁移 cylindrical-to-cubemap
- [x] 迁移 irradiance map
- [x] 迁移 prefiltered environment map
- [x] 迁移 BRDF LUT generation
- [x] 迁移 screenshot copy/readback
- [x] 明确 swapchain acquire/present graph 外边界
- [-x] 删除剩余 compatibility adapter
  - 2026-07-16 调查停止线：scene query contract、provider compat wrapper cache、derived preprocess 输入、derived output compat texture、scene skybox 依赖路径下的重复 source cache 已清理；额外代码审计确认 `SkyboxRuntimeState::cubemapTexture / sourcePreviewTexture` 与 `EnvironmentLightingRuntimeState::cubemapTexture` 剩余承担的都是 cubemap asset source、scene-skybox texture source、cylindrical source preview/fallback 等真实 source 语义，而不是纯 compat cache。当前未再发现值得做 source/result state 拆分的高收益切口；继续推进大概率会变成围绕字段名的低收益清理。本项停止在这里，仅顺手删除了已无调用点的 `wrapRenderImageAsTexture()` 死 compat helper。

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
- [x] imported initial/final state
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
