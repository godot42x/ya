# GPU 资源模型与 RenderGraph 联合重构计划

## 1. 计划定位

本计划接替 `.agent/plan/render-architecture-refactor`，作为后续 GPU 资源重构和 RenderGraph 迁移的主计划。

上一轮已经完成了 frame input、shadow settings、shared resource owner、side service 和部分 stage/pass 边界收敛。继续拆 helper、搬 callback 或扩张 `RenderSharedResourceProvider` 的收益已经很低；下一阶段应直接解决 RenderGraph 会依赖的资源模型、资源状态和执行图问题。

本计划包含三条联合推进的主线：

- 重建 `Buffer / Image / ImageView / Sampler / Texture / RenderTarget` 的职责和所有权模型
- 以新资源模型实现 RenderGraph，并依次迁移 Deferred、Forward 和外围 GPU 工作流
- 在 RenderGraph 验证稳定后提供面向渲染扩展作者的声明式 Editor Render API，隔离 command buffer、同步和 descriptor

这不是先完成一次全项目 RHI 重写、再开始 RenderGraph。新的资源模型先建立最小稳定核心，RenderGraph 作为第一个完整消费者，其余旧调用点按资源类型和渲染路径逐步迁移。

## 2. 已有工作与停止线

### 2.1 从上一轮继承的有效成果

以下成果继续保留，不重新设计：

- `RenderRuntime -> IRenderPipeline` 已统一使用 `RenderPipelineFrameContext`
- shadow 配置和运行态输入已基本形成显式路径
- `RenderSharedResourceProvider` 已从 `RenderRuntime` 接管 BRDF LUT、skybox/environment fallback 和 descriptor cache owner
- offscreen、automation capture 和 diagnostics 已有独立 service/lifecycle 入口
- Forward 已形成显式 pass 顺序以及统一 `PassContext`
- Deferred 主要 stage 的场景输入已前移到 pipeline frame preparation
- viewport、SSAO、shadow 和 postprocess 的多处重建已推迟到 frame boundary
- `.agent/plan/render-architecture-refactor/stage-io-inventory.md` 可作为第一版 pass/resource 迁移清单

### 2.2 明确停止的工作

以下工作不再作为 RenderGraph 前置任务继续推进：

- 继续拆分 `ForwardViewportStage` 的 helper/owner，仅为了缩短类文件
- 让更多 stage 直接依赖 `RenderSharedResourceProvider`
- 只把 callback 从一个 facade 搬到另一个 facade
- 在 graph scheduler 设计前大规模改造 semaphore/fence typed handle
- 在资源和 graph 结构稳定前整理目录
- 全面消除 `waitIdle()`，但不改变资源替换和延迟销毁协议
- 当前未提交的 `DeferredRenderPipeline::applyExternal*Mutation()` 纯函数提取

这些项目若不能改变资源身份、读写声明、生命周期或执行依赖，就不应继续消耗迁移时间。

## 3. 当前资源模型问题

### 3.1 Texture 混合资产与 GPU 资源职责

当前 `Texture` 同时承担：

- 文件和内存纹理资产
- staging upload
- GPU image 和默认 image view 组合
- render texture 创建
- cubemap 创建
- 全局 texture factory 查询

其中 `Texture::getTextureFactory()` 通过 `App::get()` 获取后端，导致资源创建依赖全局应用状态。SSAO、bloom、shadow、render target attachment 等 GPU 中间资源也被包装成 `Texture`，资产生命周期和 frame graph 生命周期因此混在一起。

### 3.2 Buffer 与 Image 使用不同创建协议

- `IBuffer::create(IRender*, BufferCreateInfo)` 使用静态 factory 和后端 switch
- image/image view 通过 `ITextureFactory`
- render target 又在后端内部直接创建 image、view 和 framebuffer
- sampler、framebuffer 等对象还有各自独立入口

这使资源创建、debug naming、native handle wrapping 和销毁规则无法统一，也无法由 RenderGraph resource registry 集中管理。

### 3.3 Image layout 存在错误的全局状态语义

`IImage::getLayout()` 暗示 image 在任意时刻只有一个当前 layout，但实际 layout 可能因 command buffer、queue、mip、layer 和 aspect 而不同。若 RenderGraph compiler 和 image 对象同时维护 layout，会出现两个状态真相。

资源状态应属于一次已排序的 GPU 执行计划，而不是 image 的永久属性。

### 3.4 RenderTarget 职责过重

当前 `IRenderTarget` 同时负责：

- attachment specification
- image/view/framebuffer ownership
- render pass 或 dynamic rendering begin/end
- extent/format/sample 修改
- dirty reason
- `flushDirty()` 即时重建

RenderGraph 需要分别控制逻辑资源、物理资源、attachment 使用和 pass 执行。继续沿用当前 `IRenderTarget` 会让 graph resource registry 与 render target 重复拥有 attachment。

### 3.5 所有权和非拥有引用缺乏统一规则

现有代码混用 `shared_ptr`、裸指针、opaque handle 和由 view 间接持有 image 的方式。资源 owner、descriptor 引用、pass 临时引用和 imported native resource 没有明确分类，resize 和 shutdown 时容易发生悬空引用或过度共享。

### 3.6 与成熟商业引擎的实际差距

当前 RHI 作为 Vulkan/D3D12 风格的薄抽象并非明显落后一代；主要差距是 RHI 之上的中层能力尚未闭环：

- 缺少统一 RenderGraph 编译、资源状态和 transient registry，导致 stage 手工管理 layout、resize 和 pass 顺序
- 缺少 shader parameter block，导致业务渲染代码直接操作 descriptor set、pipeline layout 和 binding
- 缺少跨 pipeline 的 DrawList/DrawPacket，导致 stage 直接遍历 mesh 并录制 bind/draw
- 缺少受限 extension facade，导致自由度表现为“可以绕过规则”，而不是“可以安全声明自定义渲染”

因此本轮先进性目标是补齐现代 renderer 中层闭环，而不是改写一套更厚的 RHI。transient aliasing、async compute、多 queue、bindless 和完整 GPU-driven rendering 必须排在 graph、parameter block 和 DrawList 稳定之后。

## 4. 目标资源模型

### 4.1 GPU 基础资源

公共资源层统一为：

- `IGpuBuffer`
- `IGpuImage`
- `IGpuImageView`
- `IGpuSampler`

创建描述统一为：

- `BufferDesc`
- `ImageDesc`
- `ImageViewDesc`
- `SamplerDesc`

共同规则：

- 创建描述包含 label、usage、memory usage、extent、format、mip、layer、sample 等稳定规格
- 资源规格创建后不可变；resize 或 format change 创建新资源并替换 owner
- 公共接口不出现 Vulkan 类型
- CPU map/write/flush 只属于 buffer
- image 不保存 graph/executor 使用的全局 current layout
- image view 明确记录 subresource range，但不拥有 image

### 4.2 统一资源工厂

引入 backend-owned `IRenderResourceFactory`，由 `IRender` 提供：

```cpp
std::unique_ptr<IGpuBuffer> createBuffer(const BufferDesc& desc);
std::unique_ptr<IGpuImage> createImage(const ImageDesc& desc);
std::unique_ptr<IGpuImageView> createImageView(
    IGpuImage& image,
    const ImageViewDesc& desc);
std::unique_ptr<IGpuSampler> createSampler(const SamplerDesc& desc);
```

资源所有权规则：

- durable owner 和 graph physical registry 使用 `unique_ptr`
- frame/pass 内只传引用、非拥有指针或 graph logical handle
- image view owner 必须在 image owner 之前销毁
- swapchain 或外部 native image 使用显式 imported descriptor，标记 native handle 非拥有
- 不新增全局 singleton factory

迁移完成后删除：

- `IBuffer::create()`
- `ITextureFactory`
- `Texture::getTextureFactory()`
- 资源创建路径中的 `App::get()`

### 4.3 Texture 只保留资产语义

`Texture` 保留为材质和资产系统使用的高层对象：

- 文件路径和资产 metadata
- 加载状态
- owning GPU image
- owning default image view
- 可选默认 sampler 或 sampler policy

职责拆分：

- 文件解码属于 texture loader/importer
- staging buffer 和 copy command 属于显式 upload service
- `Texture` 不提供 render attachment 创建入口
- SSAO、GBuffer、bloom、shadow 等中间资源直接使用 GPU image/view 或 graph handle
- 删除 `Texture::createRenderTexture()`

迁移期间允许使用最小 `RenderImage` owner 组合 image 与 default view。它不包含资产、sampler、upload 或状态跟踪语义，RenderGraph resource registry 落地后由 registry 替代，不继续扩展为第二套 texture abstraction。

### 4.4 Attachment 与 RenderTarget

将当前 `IRenderTarget` 拆为两个概念：

- `RenderAttachmentSet`：一组非拥有 image view、load/store、clear value 和 attachment role
- backend rendering/framebuffer object：Vulkan executor 根据 attachment set 创建或缓存

资源创建和 resize 不再属于 attachment set。RenderGraph 路径由 resource registry 创建 image/view；legacy 路径暂时由明确 owner 的 resource bundle 创建。

`flushDirty()` 不进入新模型。迁移期 legacy owner 只能在 frame boundary 调用显式 `recreateResources(newDesc)`。

### 4.5 资源状态跟踪

引入 command-buffer-local `ResourceStateTracker`：

- 状态至少按 image 的 mip/layer/aspect subresource 跟踪
- legacy transition API 通过 tracker 记录和校验
- RenderGraph compiler 生成 pass 间资源状态计划
- executor 将状态计划提交给同一个 tracker/barrier backend
- imported resource 必须声明 initial state 和 required final state
- image 对象不再作为 current layout 的单一事实源

首版只覆盖 graphics queue。queue ownership transfer 和 async compute 延后。

## 5. RenderGraph 最小设计

### 5.1 公共概念

首版包含：

- `RGTextureHandle` / `RGBufferHandle`：带 generation 的逻辑句柄
- `RGTextureDesc` / `RGBufferDesc`：graph 资源规格
- `RGPassBuilder`：声明 pass 读写关系
- `RGPassContext`：执行期将逻辑句柄解析为 GPU resource/view
- `RenderGraph`：收集资源和 pass
- `RenderGraphCompiler`：校验并生成执行计划
- `RenderGraphResourceRegistry`：管理 imported、transient 和 persistent physical resource
- `RenderGraphExecutor`：按计划录制现有 `ICommandBuffer`

### 5.2 资源类别

- `Imported`：swapchain、资产纹理、environment、shadow 等图外资源
- `Transient`：GBuffer、SSAO、light accumulation、bloom intermediate 等图内资源
- `Persistent`：明确需要跨帧保留的 history/cache 资源

首版 transient 每个逻辑资源拥有独立物理资源，不实现 aliasing。

### 5.3 Pass 声明规则

setup 阶段只允许：

- 创建逻辑资源
- import 外部资源
- 声明 buffer/image read/write
- 声明 color/depth attachment
- 声明最终输出

execute 阶段只允许：

- 从 `RGPassContext` 解析资源
- 绑定 pipeline/descriptor
- 录制 draw、dispatch、copy

execute 中禁止：

- 创建、resize 或销毁 GPU 资源
- `App::get()` 和 scene/service 全局查询
- `waitIdle()`
- 修改 graph 结构
- 未声明资源的 transition/copy/read/write

### 5.4 Compiler 和 Executor 首版能力

必须实现：

- pass dependency 构建和稳定拓扑排序
- cycle 检测
- read-before-write 和非法多 writer 校验
- imported resource 合法性校验
- physical resource 创建和 resize replacement
- image/buffer state transition 计划
- debug label 和 compiled pass/resource dump

明确延后：

- transient aliasing
- pass culling
- async compute
- 多 queue scheduling
- subpass fusion
- 自动 descriptor allocation

### 5.5 Editor Render Extension API

RenderGraph Core 和 executor 是引擎内部 API；面向编辑器渲染扩展作者再提供一层受限 facade：

- `IRenderExtension::build(RenderGraphBuilder&, const RenderView&)` 在固定扩展点声明 pass
- `RenderGraphBuilder` 提供 fullscreen、raster、compute、copy 和 viewport composite 操作
- shader 参数使用 Slang 反射生成的强类型 parameter block，不暴露 descriptor set/binding
- 几何提交通过 `RenderView::queryDrawList()` 筛选引擎构建的 DrawList，不开放逐 mesh 即时提交
- extension 只使用 graph handle 和 builtin resource identifier，不接触 `ICommandBuffer`、image layout、barrier、pipeline layout、framebuffer 或 native handle

首版固定扩展点为 shadow 后、lighting 后、tone mapping 前和 viewport composite 前。原生命令 callback 只允许引擎内部 pass 使用，不属于公开扩展 API。

Graph 编译必须拒绝 extension 引入的 stale handle、read-before-write、重复 writer、usage/format 不匹配和 cycle。extension 不得跨帧保存 graph handle、resolved resource 或 frame-local DrawList。

### 5.6 DrawList 与 Mesh 提交边界

公开 API 不提供立即执行的 `drawMesh(mesh, material, transform)`。统一数据流为：

```text
ECS extraction -> RenderItem -> visibility/LOD -> DrawPacket -> sort/batch -> DrawList -> RHI draw/indirect draw
```

- `RenderItem` 保存场景对象、mesh/submesh、material、transform、bounds 和 visibility metadata
- `DrawPacket` 是 renderer 内部可执行单元，固定 pipeline/material/geometry key 和 per-draw data，不拥有资产
- `DrawList` 是 frame/view-local 的 packet 索引集合，按 queue、材质域、visibility layer 和 editor tag 查询
- renderer 负责剔除、LOD、排序、instancing 和未来 indirect grouping；extension 只能筛选并提交 DrawList
- gizmo、临时线框和 procedural geometry 使用独立 debug/primitive stream，不通过公开即时 mesh API 绕过 DrawList

首版允许 CPU 构建和排序 DrawList，不要求 GPU-driven。数据结构必须保留稳定 key 和批处理边界，使后续 GPU culling/indirect draw 不需要改 extension API。

## 6. 迁移顺序

### Phase 0: 收口旧计划和行为基线

- 回退无行为价值的当前 dirty 提取改动
- 标记旧计划停止执行
- 固化 Forward/Deferred 截图、validation 和功能冒烟基线

### Phase 1: 统一资源描述与 Factory

- 建立新 desc、接口和 `IRenderResourceFactory`
- 先迁移 buffer 创建路径
- 再迁移 sampler、image 和 image view
- Vulkan 完整实现；OpenGL 暂时冻结

### Phase 2: 拆 Texture 与 GPU Image

- 分离 decode、upload 和 GPU resource ownership
- 迁移资产纹理和 cubemap
- 将 render texture 调用点迁移为显式 GPU image owner
- 删除全局 texture factory 入口

### Phase 3: Resource State Tracker

- 为 legacy command recording 建立 tracker
- 迁移现有 layout transition
- 补齐 subresource 和 imported state 语义
- 删除 graph 对 `IImage::getLayout()` 的依赖

### Phase 4: RenderGraph Core

- 实现 logical resource、pass builder、compiler、registry 和 executor
- 添加纯 CPU graph 单元测试
- 使用最小 clear/copy graph 验证 Vulkan executor

### Phase 5: Deferred RenderGraph

首批固定迁移链路：

```text
Shadow imported resources
          |
GBuffer -> SSAO -> DeferredLight -> Skybox -> DebugOverlay
                                      |
                         Bloom -> ToneMap -> Presentation
```

迁移后 RenderGraph 接管：

- GBuffer/SSAO/viewport/bloom 中间资源创建
- viewport resize 后的物理资源替换
- pass 顺序和 attachment 使用
- pass 间 layout/barrier

ECS extraction、material upload、ImGui 和 presentation orchestration 仍在 graph 外。

### Phase 6: 收敛旧 RenderTarget

- Deferred 不再依赖 attachment-owning `IRenderTarget`
- 拆除 `flushDirty()` 和 dirty-rebuild 协议
- legacy Forward 暂时使用 resource bundle + attachment set adapter
- 删除不再使用的 render target factory 职责

### Phase 7: Forward 与外围迁移

- 迁移 Forward shadow、opaque、skybox、overlay、postprocess
- 再迁移 offscreen environment preprocess 和 screenshot copy
- 删除剩余旧 factory、旧 resource type 和 compatibility adapter

### Phase 8: Editor Render Extension API

- 定义固定扩展点和 `IRenderExtension` 生命周期
- 提供受限 `RenderGraphBuilder` facade、builtin resources 和 `RenderView`
- 接入 Slang 强类型 parameter block 生成与校验
- 先统一 `RenderItem -> DrawPacket -> DrawList`，再提供 DrawList 查询并迁移 selection/debug overlay 作为首批消费者
- 在 Render Diagnostics 中展示 extension pass/resource/state dump
- 验证注册、禁用、热重载、resize、多视图和移除流程

### Phase 9: OpenGL 恢复评估

本轮不实现 OpenGL。Vulkan 和公共接口稳定后，单独评估：

- 新资源 factory 的 OpenGL 实现
- graph state/barrier 在 OpenGL 下的降级语义
- framebuffer/render attachment cache
- OpenGL 自动化冒烟恢复条件

## 7. Graph 外围边界

首版明确放在 RenderGraph 外：

- ECS scene extraction
- draw bucket 和 skinning palette 构建
- material dirty upload
- editor/ImGui UI 构建
- swapchain acquire、queue submit 和 present
- automation orchestration 和 RenderDoc lifecycle
- offscreen task queue scheduling
- editor extension 注册和生命周期管理

外围系统可通过 imported resource、graph setup input 或 graph 前后 hook 接入，但不能成为 pass 内部隐式 service 查询。

## 8. 提交与验证策略

每个阶段按职责分类提交：

- `[render/resource] ...`
- `[render/graph] ...`
- `[runtime/deferred] ...`
- `[runtime/forward] ...`
- `[vulkan] ...`
- `[test/render] ...`
- `[editor/render] ...`
- `[plan/render] ...`

避免在同一提交中同时包含：

- 资源公共 API 重构和 pass 行为修改
- graph compiler 修改和 shader 修改
- 文件格式化与逻辑修改
- Vulkan 实现和无关目录整理

基础验证命令：

```bash
make b t=HelloMaterial
make test
make r t=HelloMaterial r_args="--exit-after-frame=300"
```

RenderGraph 迁移阶段还必须覆盖：

- viewport resize
- shadow 开关和分辨率变化
- SSAO 开关
- bloom/postprocess/ACES 开关
- Forward/Deferred 切换
- shutdown 和连续资源重建
- Vulkan validation 无新增 error
- 关键输出与迁移前截图对比

## 9. 设计约束

- 只使用 XMake，不引入新构建系统
- 生成文件只读
- shader-facing 类型继续以生成头为单一事实源
- 不在 frame recording 中创建、替换或销毁正在使用的资源
- 不用共享所有权掩盖 owner 不清
- 不让 `Texture` 再承担 render attachment 职责
- 不让 image 永久对象成为 graph layout 状态真相
- 不让 graph pass 访问 `App::get()`、active scene 或 runtime service singleton
- 不为尚未实现的多 queue/aliasing 预建复杂抽象
- OpenGL 冻结期间，公共接口仍禁止 Vulkan 类型，避免未来无法恢复

## 10. 完成标准

当以下条件全部成立，本轮联合重构完成：

- GPU buffer/image/view/sampler 统一由 backend-owned resource factory 创建
- 资源创建不再依赖 `App::get()` 或静态 backend switch
- `Texture` 只表示资产纹理，不再创建 render attachment
- RenderGraph registry 是 transient attachment 的唯一 owner
- pass 资源读写和 attachment 使用可由 graph 枚举
- graph compiler 是 pass 间资源状态计划的单一事实源
- Deferred 和 Forward 主链路都通过 RenderGraph 执行
- Deferred、Forward 和 editor extension 共用 `RenderItem -> DrawPacket -> DrawList` 提交协议
- editor render extension 可声明 pass、强类型 shader 参数和 DrawList 查询，且公开头不依赖 command buffer/Vulkan/descriptor 类型
- `Runtime/App` 的非内部 pass/executor 代码不再直接操作 layout transition、descriptor binding 或 rendering scope
- 旧 `ITextureFactory`、`Texture::createRenderTexture()`、`IBuffer::create()` 和 `IRenderTarget::flushDirty()` 已删除
- Vulkan validation、功能冒烟和截图基线通过
- compatibility adapter 已删除或有明确的剩余 owner 和删除任务
