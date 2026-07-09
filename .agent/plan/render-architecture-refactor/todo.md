# 渲染架构重构 TODO

## 使用说明

这份清单对应 `Docs/RenderArchitectureRefactorPlan.md`。

推进顺序遵循两个原则：

- 先收口数据边界和 owner，再做目录/命名整理
- 每完成一项都要回填状态，避免后续继续丢上下文

状态约定：

- `[ ]` 未开始
- `[-]` 进行中
- `[x]` 已完成

## Phase 0: 固化上下文

- [x] 输出渲染架构重构计划文档
- [x] 输出渲染架构重构 TODO 文档
- [ ] 后续相关提交统一引用本计划文档，避免口径漂移

## Phase 1: 收敛 Frame Input 和 Pipeline 协议

- [x] 盘点 `RenderRuntime::FrameInput`、`ForwardRenderPipeline::TickDesc`、`DeferredRenderPipeline::TickDesc` 的公共字段与差异字段
- [x] 设计统一的 `RenderPipelineFrameContext` 或等价输入结构
- [x] 让 `RenderRuntime` 只向 active pipeline 传统一帧输入
- [x] 删除 forward / deferred 两套重复 tick desc 中的公共重复字段
- [x] 收敛 pipeline GUI/debug 查询接口，区分“执行协议”和“编辑器调试协议”
- [ ] 评估并开始移除 `SceneManager*` / `EditorLayer*` 从渲染执行入口的直传

完成标准：

- [ ] `RenderRuntime -> IRenderPipeline` 的运行期输入结构统一
- [ ] Forward / Deferred 不再平行维护一大套重复 tick 协议

## Phase 2: 收敛 Shadow 配置与运行时状态

- [ ] 盘点 shadow 配置当前所有来源：App 默认值、editor config、automation override、GUI 修改、extractor 读取、pipeline apply
- [ ] 提取单一 `ShadowSettings` owner/service 或 helper
- [ ] 合并 deferred 中的 config load / merge / persist 流程
- [ ] 把 forward 中对 `App::get()->getShadowSettings()` 的直接读取迁移到统一入口
- [ ] 把 `RenderFrameExtractor` 对全局 shadow settings 的直接依赖改成显式输入
- [ ] 定义“配置态”和“运行态”的边界，避免 `ShadowRuntimeState` 继续兼容两种职责
- [ ] 收敛 shadow GUI 的修改流程，统一走 queue/apply/sync 路径

完成标准：

- [ ] shadow settings 来源唯一
- [ ] Forward / Deferred / Extractor 不再各自旁路读取 App 全局状态

## Phase 3: 拆 shared resources provider

- [ ] 盘点 `RenderRuntime` 中共享资源 owner：BRDF LUT、skybox fallback、environment fallback、descriptor pool/layout、scene descriptor set cache
- [ ] 设计 `RenderSharedResourceProvider` 或等价对象的最小职责面
- [ ] 把 BRDF LUT 创建与持有从 runtime 主体中拆出
- [ ] 把 skybox descriptor set 更新逻辑拆出
- [ ] 把 environment lighting descriptor set 更新逻辑拆出
- [ ] 明确 scene resource resolve 与 descriptor cache 的边界
- [ ] 让 stage / pipeline 通过 provider 获取共享资源，而不是回调 runtime

完成标准：

- [ ] `RenderRuntime` 不再自己兼任共享资源缓存和 scene fallback 逻辑 owner

## Phase 4: 收窄 Render API 泄漏

- [ ] 盘点当前所有高层直接使用的 backend-style 接口：submit/present/semaphore/fence/layout transition
- [ ] 设计 typed sync handle，替换高层 `void*` 同步对象
- [ ] 收口 offscreen job submit 协议，不再让服务层自己拼原始 submit 参数
- [ ] 评估 `IRender::submitToQueue()` / `presentImage()` 的上层替代方案
- [ ] 评估并制定 `ICommandBuffer` 单一语义方案
- [ ] 明确 `YA_CMDBUF_RECORD_MODE` 是保留调试 wrapper 还是拆为独立类型

完成标准：

- [ ] app/runtime 高层不再直接操作原始同步对象
- [ ] `ICommandBuffer` 不再依赖编译宏切换两套核心语义

## Phase 5: 拆 side services 与 frame orchestration

- [ ] 盘点当前挂在 `RenderRuntime` 上的 side services：offscreen、diagnostics、automation capture hook、render target editor catalog
- [ ] 明确 GPU frame 主链路的最小职责边界
- [ ] 把 offscreen tick 从 `RenderRuntime::runFramePrologue()` 中拆出到独立 lifecycle service
- [ ] 把 presentation capture 从 `RenderRuntime::renderPresentationPass()` 中拆成显式 hook/service
- [ ] 明确 automation service 与 diagnostics service 的 owner
- [ ] 检查主线程 callback 处理位置是否仍唯一，避免语义回流到 render runtime

完成标准：

- [ ] `RenderRuntime` 只保留 GPU frame orchestration
- [ ] automation / diagnostics / offscreen 各有独立生命周期入口

## Phase 6: 建立 Render Graph 前置接口

- [ ] 列出现有 stage 的资源输入输出清单：Shadow、GBuffer、SSAO、Light、ViewportOverlay、PostProcess、ForwardViewport
- [ ] 标记每个 stage 仍依赖的全局状态或隐式输入
- [ ] 设计 stage 级显式输入结构，减少从 `App::get()` / `RenderRuntime` 全局反查
- [ ] 把 stage 内部即时资源重建改成脏标记 + 安全边界重建模式
- [ ] 梳理哪些 stage 可以直接映射为未来 graph pass
- [ ] 梳理哪些 stage 还需要继续拆分才适合 graph 化

完成标准：

- [ ] 每个 stage 至少可以显式列出输入资源、输出资源、外部服务依赖

## Phase 7: 目录与残留清理

- [ ] 盘点 `Engine/Source/Render` 中抽象层 / 功能层 / 资产层混杂点
- [ ] 删除或并入 `Render/RHI/SceneRenderer.h` 这类空壳/历史残留文件
- [ ] 规划 `Render/Core`、功能模块、资产模块的稳定目录边界
- [ ] 把仅属于 runtime app orchestration 的内容继续收敛在 `Runtime/App`
- [ ] 清理重构后失效的旧 helper / 中转接口

完成标准：

- [ ] 目录结构可以直接表达抽象层级和 owner

## 持续审查项

- [ ] 每次新增渲染功能时检查是否又把 owner 堆回 `RenderRuntime`
- [ ] 每次新增 stage 时检查输入输出是否显式
- [ ] 每次新增 settings 时检查是否出现多处 owner
- [ ] 每次新增资源重建逻辑时检查是否引入新的 `waitIdle()`
- [ ] 每次新增后端接口时检查是否把 Vulkan 细节继续泄漏到高层
