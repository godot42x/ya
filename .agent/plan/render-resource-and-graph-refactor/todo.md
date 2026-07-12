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
- [ ] 建立 viewport resize 冒烟入口
- [ ] 建立 shadow 开关/分辨率冒烟入口
- [ ] 建立 SSAO、bloom、postprocess、ACES 冒烟入口
- [ ] 建立 Forward/Deferred switch 冒烟入口

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
- [ ] 盘点 `beginIsolateCommands()` upload/init 路径，分离 allocation、upload 和 initial transition
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
- [ ] 将 postprocess output render texture 改为 GPU image/view owner
- [x] 将 BRDF LUT 输出改为 GPU image/view owner
- [ ] 将 shadow sampled view 改为显式 image/view owner
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
- postprocess output 仍通过 `IRenderPipeline -> App -> automation screenshot` 的 `Texture*` 契约暴露，应与 screenshot scratch 生命周期一起迁移
- shadow sampled view 和 screenshot scratch 仍待迁移

## Phase 5: Resource State Tracker

- [ ] 盘点所有 `transitionImageLayout*()` 调用点
- [ ] 盘点 attachment initial/final layout 语义
- [ ] 盘点 swapchain acquire/present 状态
- [ ] 盘点 cubemap mip/layer transition
- [ ] 定义 buffer/image resource state
- [ ] 定义 mip/layer/aspect subresource key
- [ ] 实现 command-buffer-local `ResourceStateTracker`
- [ ] 将 legacy transition API 接入 tracker
- [ ] 为冲突状态和遗漏 transition 增加 debug validation
- [ ] 定义 imported resource initial/final state
- [ ] 停止以 `IImage::getLayout()` 作为执行状态真相
- [ ] 删除或降级 image 全局 layout API

完成标准：

- [ ] 单个 command buffer 内状态变化可完整追踪
- [ ] 多 layer/mip transition 不再依赖单一 image layout
- [ ] graph 和 legacy 路径共用 barrier backend

## Phase 6: RenderGraph Core

- [ ] 定义带 generation 的 `RGTextureHandle`
- [ ] 定义带 generation 的 `RGBufferHandle`
- [ ] 定义 `RGTextureDesc` / `RGBufferDesc`
- [ ] 实现 imported/transient/persistent resource declaration
- [ ] 实现 `RGPassBuilder::read()`
- [ ] 实现 `RGPassBuilder::write()`
- [ ] 实现 color/depth attachment declaration
- [ ] 实现 `RGPassContext` resource resolve
- [ ] 实现 dependency graph 构建
- [ ] 实现稳定拓扑排序
- [ ] 实现 cycle 检测
- [ ] 实现 read-before-write 校验
- [ ] 实现非法 writer/usage 校验
- [ ] 实现 compiled graph debug dump
- [ ] 实现 `RenderGraphResourceRegistry`
- [ ] 实现 Vulkan `RenderGraphExecutor`
- [ ] 将 compiled state plan 接入 `ResourceStateTracker`
- [ ] 建立最小 clear/copy graph 冒烟测试

完成标准：

- [ ] graph core 单元测试不需要启动完整 App
- [ ] Vulkan executor 可正确执行最小图
- [ ] pass execute 无需访问 graph 内部结构

## Phase 7: Deferred Graph 迁移

### GBuffer

- [ ] 声明 GBuffer color/depth transient resources
- [ ] 迁移 GBuffer pass setup/execute
- [ ] 明确 frame/light/skinning buffer 为 imported 或 graph buffer
- [ ] 移除 GBuffer attachment 的 `IRenderTarget` ownership

### SSAO

- [ ] 声明 GBuffer read 和 AO write
- [ ] 迁移 SSAO pass
- [ ] 删除 SSAO resize 手工重建
- [ ] 由 graph registry 按 extent replacement

### Deferred Light

- [ ] 声明 GBuffer、AO、shadow、environment reads
- [ ] 声明 viewport HDR output
- [ ] 迁移 fullscreen light pass
- [ ] 移除 `GBufferStage*` 资源反查

### Overlay

- [ ] 迁移 skybox pass
- [ ] 迁移 debug overlay pass
- [ ] 将 scene descriptor 作为 graph setup 输入/imported resource

### Postprocess

- [ ] 迁移 bloom extract
- [ ] 迁移 bloom blur ping/pong
- [ ] 迁移 bloom composite
- [ ] 迁移 ACES/tone map
- [ ] 声明 presentation output
- [ ] 删除 postprocess 手工 resize owner

### Pipeline 收口

- [ ] RenderGraph 接管 Deferred pass 顺序
- [ ] RenderGraph 接管 Deferred intermediate resource owner
- [ ] RenderGraph 接管 pass 间 barrier
- [ ] 删除 Deferred `refreshDirtyResources()` attachment 修复路径
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

## Phase 11: 清理与 OpenGL 恢复评估

- [ ] 删除旧 resource interfaces 和 factory
- [ ] 删除旧 render target dirty/recreate helper
- [ ] 删除失效 callback/provider forwarding
- [ ] 检查没有 `App::get()` 资源创建路径
- [ ] 检查没有 render attachment 使用资产 `Texture`
- [ ] 检查没有 graph pass 内资源创建或 `waitIdle()`
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
