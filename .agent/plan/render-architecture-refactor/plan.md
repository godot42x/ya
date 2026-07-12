# 渲染架构重构计划

## 1. 目标

这次重构不是继续做零碎接口修补，而是先把当前渲染架构中的几个高风险结构问题明确收口，为后续两件事做准备：

- 降低当前 Forward / Deferred / Shadow / Automation / Offscreen 之间的耦合和状态扩散
- 为未来迁移到 Render Graph 先建立稳定的数据边界、资源边界和执行边界

本计划保持现有命名口径：

- 顶层继续使用 `IRender`
- 顶层渲染路线继续使用 `RenderPipeline`
- 具体路线继续使用 `ForwardRenderPipeline` / `DeferredRenderPipeline`
- 中间执行单元继续使用 `Stage`
- 底层原生对象继续使用 `GraphicsPipeline` / `ComputePipeline` / `RenderPass`

目标不是一次性做完整 RHI 或完整 Render Graph，而是先把最妨碍演进的结构问题拆掉。

## 2. 当前架构判断

### 2.1 RenderRuntime 责任过重

`RenderRuntime` 当前同时承担了过多职责：

- 初始化渲染后端与 shader 系统
- 管理 screen/presentation 资源
- 持有 Forward / Deferred pipeline 实例
- 管理 skybox / environment lighting 的共享 descriptor 资源
- 驱动 offscreen task
- 驱动 RenderDoc automation
- 处理 editor viewport debug / render target catalog
- 负责一帧录制、submit、presentation

这导致 `RenderRuntime` 既像 runtime orchestrator，又像 shared resource owner，又像诊断服务入口，又像编辑器调试 facade。后续无论做 Render Graph、拆 Render Thread，还是新增新管线，都会继续把复杂度堆到这里。

### 2.2 渲染抽象层仍在泄漏 Vulkan 风格接口

`IRender` 和 `ICommandBuffer` 仍大量暴露底层同步与显式资源状态细节：

- `submitToQueue(...)` / `presentImage(...)`
- `createSemaphore(...)` / `destroySemaphore(...)`
- `getCurrentImageAvailableSemaphore()` / `getCurrentFrameFence()`
- `std::vector<void*>` 的 command buffer / semaphore / fence 传递
- `transitionImageLayout*()`、`copyImage()`、`copyBufferToImage()` 直接暴露给上层 stage / pipeline

这意味着很多上层逻辑实际上仍在写 Vulkan-style orchestration，而不是写引擎内稳定的渲染编排接口。这样会带来两个直接问题：

- 上层难以换后端，OpenGL 兼容层只能被动模仿 Vulkan 语义
- 后续做 Render Graph 时，图级资源依赖分析会被这些手工 barrier / layout 迁移打散

### 2.3 Pipeline 和 Stage 的数据边界仍不稳定

虽然已经引入了 `IRenderPipeline` 和 `IRenderStage`，但 pipeline 与 stage 之间的数据组织仍偏松散：

- `IRenderPipeline` 既承担执行入口，也暴露大量 debug texture / RT 查询能力
- `ForwardRenderPipeline::TickDesc` 与 `DeferredRenderPipeline::TickDesc` 结构重复度很高
- `RenderRuntime::FrameInput` 与 pipeline tick desc 又重复一层
- viewport、camera、clicked、sceneManager、editorLayer 等数据在多层之间重复搬运

这会导致未来新增新的 `RenderPipeline` 时，不是插一个新实现就够了，而是要复制一整套 tick desc、GUI、debug output、presentation 协议。

### 2.4 Shadow 数据流仍有跨层状态扩散

当前阴影链路已经比之前更整齐，但仍有明显的跨层状态重复：

- `App::_shadowSettings`
- `AppAutomation` 启动覆盖
- `DeferredRenderPipeline` 本地 config 读取 / merge / queue / apply
- `ForwardRenderPipeline` 直接读取 `App::get()->getShadowSettings()`
- `RenderFrameExtractor` 也直接读取 `App::get()->getShadowSettings()`
- `ShadowRuntimeState` 又重新拼装出一份运行期视图

这说明 shadow settings 还没有形成单一 owner。结果是：

- 配置来源散在 `AppAutomation`、pipeline GUI、extractor、pipeline apply 流程里
- Forward / Deferred 对 shadow 的行为口径并不一致
- 阴影设置既是“应用设置”，又在部分路径里变成“渲染阶段内部状态”

这会直接妨碍未来 Render Graph 化，因为 Render Graph 需要输入参数稳定、资源读写稳定，而不是让设置从多处侧向注入。

### 2.5 Shared render resources 的 owner 不清晰

当前 skybox / environment lighting / BRDF LUT 等共享渲染资源挂在 `RenderRuntime`，但它们实际服务的是多个 stage / pipeline：

- `RenderRuntime` 持有 descriptor pool / layout / fallback texture / bound scene texture 缓存
- stage 在需要时再反向向 `RenderRuntime` 取 descriptor set

问题在于这里混了三种不同职责：

- 共享 GPU 资源构建
- scene 相关资源选择与 fallback 逻辑
- descriptor set 缓存与脏更新策略

这些逻辑如果继续堆在 runtime 里，未来切到 Render Graph 时会很难把“资源提供者”和“图节点消费者”拆开。

### 2.6 Offscreen / Automation / Main-thread callback 仍与 render frame 主链路耦合

虽然 `TaskQueue::processMainThreadCallbacks()` 已经在 `AppFrameLoop` 里统一处理，但渲染主链路仍然承担了不少非 GPU frame 编排职责：

- `RenderRuntime::runFramePrologue()` 内驱动 `OffscreenTaskService`
- `RenderRuntime::renderPresentationPass()` 内混入 automation screenshot record
- `RenderDiagnosticsService` 的生命周期也附着在 render runtime

这说明当前 frame loop 的职责线仍不够清楚：

- 哪些是 app lifecycle service
- 哪些是 GPU frame service
- 哪些是 automation/offscreen side service

如果这条线不再拆清，后续做 render thread 或 render graph scheduler 时，side effect 会持续从运行时四处渗入。

### 2.7 资源生命周期管理仍偏保守且分散

当前很多路径仍直接 `_render->waitIdle()`：

- `RenderRuntime` begin frame
- pipeline switch
- shadow enable/disable
- postprocess / SSAO resize
- runtime shutdown

这说明资源重建与帧内录制之间还没有形成清晰的延迟销毁 / 延迟重建协议。继续这样下去的问题是：

- 吞吐被全局 `waitIdle()` 锁死
- 很难升级到多 flight frame
- Render Graph 即使引入，也会被全局同步削平

### 2.8 目录分层仍有抽象与业务混杂

`Engine/Source/Render/` 当前同时包含：

- Core 抽象
- Pipelines 业务模块
- Material / Model 资产
- 2D / 3D 功能
- `RHI/SceneRenderer.h` 这类空壳或历史残留

目录本身没有稳定表达“抽象层 / 功能层 / 资产层”的边界，新人会很难判断一段代码应该放到哪里。

## 3. 需要优先重构的问题

按优先级排序，当前最值得先处理的是下面六项。

### 3.1 建立稳定的 Render Frame 输入模型

先把当前 `RenderRuntime::FrameInput`、forward/deferred tick desc、部分 editor/debug 输入统一成一份稳定的 frame context / frame request 模型，减少重复传参和多层搬运。

第一阶段不必直接做完整 snapshot/proxy，但至少要做到：

- `RenderRuntime -> RenderPipeline` 之间只传一份统一 frame request
- Forward / Deferred 不再维护两套高度重复的 tick 输入结构
- `SceneManager*`、`EditorLayer*` 这类业务对象逐步从渲染执行入口中退出

这是后续做 Render Graph 输入规范化的第一步。

### 3.2 建立 ShadowSettings 的单一 owner

需要把 shadow 配置来源和运行时投影状态拆开：

- `ShadowSettings` 只表示高层配置
- pipeline/stage 只消费已解析好的 shadow runtime input
- config load / editor persist / automation override 合并到单一 service 或 helper

完成后要做到：

- Forward / Deferred 不再各自读取和改写 `App::get()->getShadowSettings()`
- `RenderFrameExtractor` 不再直接依赖全局 App shadow settings
- `ShadowRuntimeState` 只表示渲染执行输入，不再混做配置存储

### 3.3 把 shared environment resources 从 RenderRuntime 中拆成独立 provider

建议引入一个共享渲染资源提供者，先承接：

- BRDF LUT
- skybox fallback / scene descriptor set
- environment lighting fallback / scene descriptor set

这个 provider 只负责：

- 共享 GPU 资源创建与销毁
- 基于 scene 解析得到当前需要绑定的 descriptor set

RenderRuntime 只依赖 provider，不自己兼任 owner。

### 3.4 收窄上层对底层渲染 API 的直接感知

这一步先不做完整 RHI 重写，但要先收最危险的接口泄漏：

- 用 typed handle 替代上层 `void* semaphore/fence`
- 把 offscreen 提交、present、frame sync 这类 backend 细节收回 backend-owned service
- 将“上层自己拼 submit 参数”的模式改成更高层的 frame submit / async job submit 协议

目标是让 app/runtime/pipeline 层不再显式拼 Vulkan submit graph。

### 3.5 拆分 GPU frame 主链路与 side services

需要明确三条链路：

- `AppFrameLoop` 负责主循环调度
- `RenderRuntime` 只负责编排一帧 GPU frame
- `Automation` / `Offscreen` / `Diagnostics` 是独立服务对象，由 app lifecycle 驱动

这样后续无论做 render thread 还是 render graph scheduler，都不会把 screenshot / capture / offscreen 任务重新混进 frame 主链路。

### 3.6 建立 render graph 友好的资源与阶段边界

在还没真正引入 graph 之前，要先把当前 stage 改到至少满足这些前提：

- stage 输入输出明确
- 资源 owner 明确
- 外部依赖显式，而不是隐式从 `App::get()` 或 `RenderRuntime` 全局拉取
- 尽量减少 stage 内部的资源创建和临时重建

后续 graph 节点才能自然承接这些 stage，而不是再反向做一次大拆迁。

## 4. 面向 Render Graph 的前置原则

后续如果要迁移到 Render Graph，当前代码必须逐步满足以下原则：

### 4.1 Pass/Stage 只描述读写关系，不自己偷偷找资源

一个 stage 若要采样 shadow、GBuffer、SSAO、skybox，就应该通过显式输入拿到这些资源，而不是通过：

- `App::get()`
- `RenderRuntime::get...()`
- 全局单例

### 4.2 资源重建必须从“即时行为”改为“声明式脏标记”

现在很多 resize / format change 仍是检测到后立刻 `waitIdle()` 重建。Render Graph 下更合理的模式应是：

- 标记资源规格变化
- 在安全的 frame boundary 统一执行重建
- 让 graph compiler / resource allocator 决定何时替换物理资源

### 4.3 Draw extraction 与 GPU execution 分离

Render Graph 管的是 GPU 执行图，不应该同时承担 ECS 遍历和 authoring state 判断。当前 draw item 抽取、light list 抽取、shadow dependent camera data 计算，需要继续从 GPU stage 中剥离出来。

### 4.4 Presentation / UI / Automation 视作 graph 外围系统

ImGui、screenshot capture、RenderDoc capture summary 这些可以接在 graph 前后，但不应污染 graph 节点定义本身。

## 5. 分阶段实施计划

在已经明确“下一步目标是 Render Graph”的前提下，这份计划需要调整优先级：

- 值得继续做的，是那些能直接把 stage/pass 变成 graph-friendly 形态的工作
- 不值得继续深挖的，是“只是把 owner 从 `RenderRuntime` 挪到另一个 facade/service”但不会改变 graph 输入输出边界的工作

具体取舍如下：

- `Phase 1`、`Phase 2`：继续保留。这两部分直接收敛 frame 输入和 shadow 配置边界，是 Render Graph 前置条件
- `Phase 3`：做到“共享资源 owner 从 RenderRuntime 主体中拆出”即可，剩余“让各 stage/pipeline 直接依赖 provider”不再作为高优先级。因为 Render Graph 落地后，真正稳定的依赖对象更可能是 graph resource registry / blackboard，而不是当前这层 provider
- `Phase 4`：保留，但只做高风险 API 泄漏盘点与边界收口，不做大规模 typed handle 改造。真正的 submit/sync 协议会和 Render Graph scheduler 一起重新定义
- `Phase 5`：继续保留。automation / diagnostics / offscreen 从 frame 主链路中拆开，能直接减少 Render Graph 落地时的外围副作用
- `Phase 6`：提升为当前最高优先级。这部分才是 Render Graph 真正的直接前置工作
- `Phase 7`：降为最后处理。目录整理对 Render Graph 迁移帮助有限，放在结构稳定后再做

### Phase 0: 固化上下文与命名口径

目标：先把本次重构的原则、问题清单和 TODO 固化，避免后续工作继续在上下文漂移中进行。

完成标准：

- 有稳定的计划文档
- 有可执行的 TODO 清单
- 后续提交统一对齐 `RenderPipeline / Stage / GraphicsPipeline` 口径

### Phase 1: 收敛 Frame Input 和 Pipeline 协议

目标：把 `RenderRuntime::FrameInput` 与 forward/deferred tick desc 合并成统一的渲染帧输入模型。

建议动作：

- 引入统一 `RenderPipelineFrameContext` 或等价结构
- `RenderRuntime` 只向 active pipeline 传一份标准化输入
- Forward / Deferred 各自只保留本管线特有的附加状态
- 开始移除 `SceneManager*` / `EditorLayer*` 等业务对象直传

完成标准：

- `ForwardRenderPipeline::TickDesc` 与 `DeferredRenderPipeline::TickDesc` 不再平行复制
- `RenderRuntime` 到 pipeline 的执行协议稳定下来

### Phase 2: 收敛 Shadow 配置与运行时状态

目标：给 shadow 建立单一 owner 和单一路径。

建议动作：

- 提取 `ShadowSettingsService` 或 helper
- 合并 config load、automation override、editor persist
- 让 pipeline 只消费解析后的 `ShadowPipelineInput` / `ShadowRuntimeState`
- 让 extractor 通过显式输入获得阴影设置，而不是直接读 App 全局状态

完成标准：

- Forward / Deferred / Extractor 不再直接分散读取 `App::get()->getShadowSettings()`
- 阴影设置来源唯一

### Phase 3: 拆 shared resources provider

目标：把 environment / skybox / BRDF LUT 等共享资源从 `RenderRuntime` 中拆出来，并在这里停住，不继续把当前 provider 当成未来 graph 的长期抽象。

建议动作：

- 新建 `RenderSharedResourceProvider` 或等价 owner
- 收口 fallback texture、descriptor pool/layout、scene DS 更新逻辑
- 不再继续扩张 provider 责任；它只作为过渡期 owner
- 若某条调用链只是从 `RenderRuntime` 回调改成 provider 回调，但不会让资源输入输出更显式，则优先级降低

完成标准：

- `RenderRuntime` 不再自己维护 skybox/environment resource state machine
- 共享资源 owner 清晰

### Phase 4: 收窄 Render API 泄漏

目标：先盘清并收口最危险的 backend 细节泄漏，为后续 Render Graph scheduler 留出接口空间。

建议动作：

- 先盘点 submit/present/fence/semaphore/layout transition 的真实上层使用面
- 优先收掉会阻碍 Render Graph scheduler 的直接后端耦合点
- 封装 offscreen submit 协议
- 让 present / frame submit 走 frame service，而不是上层拼装原始同步参数
- typed handle 改造只在它能直接服务 scheduler / graph compiler 设计时再做
- `ICommandBuffer` 双语义问题先给出结论和迁移路线，不急于一次改完

完成标准：

- 上层不再直接处理 `std::vector<void*>` 同步对象
- `ICommandBuffer` 语义朝单一稳定模式收敛

### Phase 5: 拆 side services 与 frame orchestration

目标：把 automation / diagnostics / offscreen 从 GPU frame 主链路中拆出去。

建议动作：

- 让 `AppFrameLoop` 或 lifecycle service 驱动 automation service
- offscreen job service 不再挂在 `RenderRuntime::runFramePrologue()` 内隐式执行
- presentation capture 改成显式 service hook

完成标准：

- `RenderRuntime` 只关心 frame prepare / world pass / presentation / submit
- diagnostics / automation / offscreen 各自独立

### Phase 6: 建立 Render Graph 前置接口

目标：直接为 Render Graph 落地做前置接口准备。这是当前最高优先级。

建议动作：

- 为 stage 明确声明输入 / 输出资源
- 把 stage 内的隐式依赖改为构造期或执行期显式注入
- 明确哪些输入属于 draw extraction 结果，哪些属于 GPU resource handle，哪些属于 runtime service
- 梳理哪些 stage 可直接演化为 graph pass，哪些需要继续拆分

完成标准：

- stage 之间资源依赖可枚举
- graph 迁移不再需要重新发明一套资源边界

### Phase 7: 清理目录分层与历史残留

目标：最后再做目录和命名清理，避免在主风险未收敛前做装饰性重排。

建议动作：

- 清理 `Render/RHI/SceneRenderer.h` 这类空壳
- 按稳定职责重组 `Render/Core`、功能模块、资产模块
- 把真正属于 runtime app orchestration 的内容留在 `Runtime/App`

完成标准：

- 目录能清晰表达抽象层级
- 不再出现无 owner 的历史残留文件

## 6. 设计约束

后续实施时统一遵循这些约束：

- 不为了“看起来像 Render Graph”而提前引入过多抽象壳子
- 不在尚未建立资源生命周期协议前大规模删除现有 barrier / layout 逻辑
- 不在一次提交里同时做结构重构和行为修改，优先小步收口
- 新接口先服务当前 Forward / Deferred 两条主路径，不先为未来假设性管线设计过度通用层
- Vulkan 原生术语保留在底层，顶层继续统一使用 `RenderPipeline` 区分于 `GraphicsPipeline`

## 7. 验收标准

当下面这些条件成立时，说明这轮前置重构基本完成，可以继续推进 Render Graph：

- `RenderRuntime` 明显瘦身，不再兼任共享资源 owner、automation/offscreen driver、调试 facade
- pipeline 执行输入统一，Forward / Deferred 不再复制大段相同 frame 协议
- shadow settings 来源唯一，运行时状态不再散落在多个 owner 上
- shared environment resources 有独立 provider
- app/runtime 高层不再直接拼 Vulkan-style submit/sync 参数
- stage 资源依赖显式化，至少可以手工列出输入输出图
- 全局 `waitIdle()` 的使用明显下降，只保留在少数安全边界
