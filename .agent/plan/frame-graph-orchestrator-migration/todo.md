# Frame Graph 顶层编排与资源收口 TODO

## 使用规则

- worker 开始前先读 `worker-guide.md`，然后领取第一个依赖已满足的任务。
- `[-]` 表示正在执行；同一时间只允许一个架构任务处于 `[-]`。
- 每个任务默认对应一个可 review 的提交批次；标明“调查”的任务可以只更新计划，不强行产出代码。
- 所有实现提交必须执行 `make test`、`make b t=HelloMaterial` 和任务指定 smoke。
- 不修改或提交与任务无关的现有工作区文件。

状态：`[ ]` 未开始，`[-]` 进行中，`[x]` 完成，`[~]` 延后，`[-x]` 停止。

## P0 基线与计划接替

- [ ] `FG-001` 记录当前 Deferred/Forward graph dump、pass 顺序、frame buffer owner、局部 executor 和 graph 外 descriptor 更新 inventory
  - 依赖：无
  - 修改：本计划 `progress.md`；必要时增加只读诊断脚本/测试，不改运行行为
  - 验收：inventory 覆盖 Deferred、Forward、Shadow、Postprocess、Presentation
  - 提交：`[plan/render] baseline frame graph orchestration gaps`

- [ ] `FG-002` 固化 Deferred/Forward 固定机位截图、draw count、validation 和 pipeline-switch 基线
  - 依赖：FG-001
  - 修改：复用旧计划 automation；仅缺失时补配置或测试
  - 验收：1500 帧内退出；日志无 Validation Error/VUID/[Error]；记录截图 hash 与相机参数
  - 提交：可与 FG-001 合并为一个 plan/validation 提交

- [ ] `FG-003` 修正旧计划的完成表述并添加接替入口
  - 依赖：FG-001
  - 修改：`../render-resource-and-graph-refactor/plan.md`、`todo.md`、`progress.md`
  - 验收：旧 Phase 7 明确为 graph-backed execution；Forward/顶层 orchestrator 工作链接到本计划
  - 提交：与 FG-001/002 合并

## P1 RenderGraph 核心前置

- [ ] `FG-101` 为 persistent texture/buffer 定义稳定 resource key 契约
  - 依赖：FG-001
  - 修改：`RenderGraph.h/.cpp`、core tests
  - 实现：新增 typed key；拒绝空 key、同帧重复 key 的 type/desc 冲突；handle 仍为 frame-local
  - 验收：同 key 同 desc 跨两张 graph 复用；创建顺序变化仍复用；不同 key 不混用
  - 禁止：在本任务顺带改变 imported/transient identity；buffer aliasing 由 FG-106 至 FG-110 独立实现
  - 提交：`[render/graph] add stable persistent resource identity`

- [x] `FG-102` 让 registry 以 stable key 管理 persistent physical resource
  - 依赖：FG-101
  - 修改：`RenderGraphResourceRegistry.h/.cpp`、`RenderGraphExecutor`、core tests
  - 实现：persistent map 与 frame handle resolution 分离；spec change 延迟退休旧 owner；本帧未声明时保留还是 prune 必须由显式 policy 决定并测试
  - 验收：pass enable/disable、创建顺序变化、resize replacement、clear/shutdown tests
  - 停止线：若现有 executor 生命周期无法区分 pipeline persistent scope，先写调查结论并拆出 scope-owner 任务
  - 提交：`[render/graph] persist graph resources by stable key`

- [x] `FG-103` 增加 pass-scoped resource resolve/access validation
  - 依赖：FG-101
  - 修改：`RenderGraph.h/.cpp`、`RenderGraphExecutor`、core tests
  - 实现：execute resolve texture/buffer 时检查 handle 属于当前 pass 且声明了兼容 access；debug assert 给出 pass/resource label
  - 验收：未声明 resolve、write-as-read、stale handle、合法 resolve 测试
  - 提交：`[render/graph] validate pass resource resolution`

- [x] `FG-104` 补 buffer range 和明确 state 语义
  - 依赖：FG-103
  - 修改：RG buffer usage/state plan、ResourceStateTracker、tests
  - 实现：pass 可声明 offset/size；HostWrite、UniformRead、StorageRead/Write、IndirectRead 映射明确
  - 验收：不重叠 range 不产生错误依赖；重叠 write/read 有正确顺序；whole-buffer 保持兼容
  - 禁止：多 queue ownership、通用 hazard optimizer
  - 提交：`[render/graph] track graph buffer ranges`

- [x] `FG-105` 定义 frame graph execution result/export owner
  - 依赖：FG-102、FG-103
  - 修改：RenderGraph/Executor 公共内部 API、tests
  - 实现：构图时标记 viewport/debug/export texture；execute result 返回 completion-safe shared owner；不暴露 registry map
  - 验收：export owner 在下一帧 replacement 后仍活到 submit completion；未 export 资源不可外取
  - 提交：`[render/graph] export frame graph outputs explicitly`

- [ ] `FG-106` 计算 compiled transient buffer lifetime interval
  - 依赖：FG-104
  - 修改：`RenderGraph.h/.cpp`、compiled graph dump、core tests
  - 实现：按最终拓扑序记录每个 transient buffer 的 first/last use；Imported/Persistent 单独标记，不参与 alias
  - 验收：linear、branch/merge、explicit dependency、unused buffer、optional pass 的确定性 lifetime tests
  - 提交：`[render/graph] compile transient buffer lifetimes`

- [ ] `FG-107` 生成 transient buffer physical slot allocation plan
  - 依赖：FG-106
  - 修改：compiler allocation plan 与 tests
  - 实现：对不重叠 interval 做 deterministic slot coloring；memoryUsage 兼容、size 取最大、usage 取并集、alignment 显式
  - 验收：三个 logical buffer 可收敛到更少 physical slots；重叠 lifetime、Imported/Persistent、incompatible memory class 不 alias
  - 禁止：texture aliasing、Vulkan memory handle 泄漏、多 queue lifetime
  - 提交：`[render/graph] allocate transient buffer slots`

- [ ] `FG-108` 让 registry materialize 并跨帧池化 physical buffer slots
  - 依赖：FG-102、FG-107
  - 修改：`RenderGraphResourceRegistry`、`RenderGraphExecutor`、tests
  - 实现：logical handle -> physical slot 映射；每 slot 只创建一个 IBuffer；兼容 spec 跨帧 pool hit，扩容/usage 变化安全 replacement
  - 验收：创建顺序变化、optional pass、连续 frame、clear/shutdown、deferred deletion tests
  - 提交：`[render/graph] pool transient buffer slots`

- [ ] `FG-109` 实现 buffer alias boundary barrier 与 state reset
  - 依赖：FG-103、FG-108
  - 修改：compiled state plan、executor、ResourceStateTracker、tests
  - 实现：同 physical slot 切换 logical identity 时插入 memory barrier；不能让前一 logical state 泄漏到后一 logical resource
  - 验收：compute write -> aliased transfer write/read、storage -> indirect 等 alias sequence 产生正确 barrier；无重叠 logical resource 同时 resolve
  - 提交：`[render/graph] synchronize transient buffer aliases`

- [ ] `FG-110` 增加 buffer reuse diagnostics 和完成门禁
  - 依赖：FG-109
  - 修改：graph debug dump/diagnostics、core tests
  - 实现：输出 logical count/bytes、physical slots/bytes、assignment、pool hit/miss、reuse ratio
  - 验收：测试图必须满足 physical slot count < logical transient count；连续 frame 必须观察到 pool hit；禁用 alias 时结果可对照
  - 提交：`[test/render] lock transient buffer reuse`

- [ ] `FG-111` 实现 completion-safe per-flight FrameUploadArena
  - 依赖：FG-104
  - 修改：Render resource/common runtime 层、descriptor buffer slice tests
  - 实现：每 flight host-visible backing buffer；按 backend-agnostic alignment 分配 offset/range；fence 后 reset；capacity growth 延迟退休
  - 验收：多个 UBO slice 共用一个 backing buffer且 offset 不重叠；descriptor offset/range 正确；未完成 flight 不被覆盖
  - 禁止：硬编码 Vulkan alignment；为了 arena 改写 shader-facing struct；把 CPU 预写 slice 纳入 transient alias
  - 提交：`[render/resource] add per-flight upload arena`

## P2 Deferred FrameResourceSet

- [ ] `FG-201` 新建 DeferredFrameResourceSet 并迁移 frame/light buffer owner
  - 依赖：FG-110、FG-111
  - 修改：新增 `DeferredFrameResources.*`，调整 `DeferredRenderPipeline`、`GBufferStage`
  - 实现：frame/light 使用 current-flight upload arena slices；pipeline-level owner upload/import；GBufferStage 只接收当前 flight binding
  - 验收：frame/light descriptor 指向同一 arena backing buffer 的不同 offset/range；删除 GBufferStage `_frameUBO/_lightUBO` owner/getter
  - smoke：Deferred baseline、shutdown
  - 提交：`[runtime/deferred] centralize frame and light buffers`

- [ ] `FG-202` 迁移 Deferred skinning buffer owner 与 capacity replacement
  - 依赖：FG-201
  - 修改：DeferredFrameResourceSet、GBufferStage、deferred tests/smoke
  - 实现：owner 负责 capacity；replacement 只在 frame boundary；旧 owner保活到 completion
  - 验收：零 palette、capacity growth、连续场景切换；删除 GBufferStage `_skinningSSBO` owner/getter
  - 提交：`[runtime/deferred] centralize skinning frame resources`

- [ ] `FG-203` 迁移 SSAO frame buffer owner
  - 依赖：FG-201
  - 修改：DeferredFrameResourceSet、SSAOStage
  - 验收：SSAO frame data 使用 upload arena slice；SSAOStage 不再创建/持有 frame UBO；SSAO enable/disable smoke 通过
  - 提交：`[runtime/deferred] centralize ssao frame resources`

- [ ] `FG-204` 迁移 skybox frame buffer owner
  - 依赖：FG-201
  - 修改：DeferredFrameResourceSet、ViewportOverlayStage
  - 验收：skybox frame data 使用 upload arena slice；Stage 不再持有 `_skyboxFrameUBO`；IBL/PBR 球体固定机位基线保持
  - 提交：可与 FG-203 合并为一个 reviewable auxiliary-frame-resource 提交

- [ ] `FG-205` 迁移 shadow raster/cull/indirect per-flight buffer owner
  - 依赖：FG-202、FG-104
  - 修改：DeferredFrameResourceSet 或经调查确认的 common shadow frame owner；Directional/Point/Cull/Indirect passes
  - 实现：先分类 host-prewritten 与 GPU-only scratch；前者进入 frame owner/arena，后者改为 graph transient slot；按 directional、point raster、point cull 三个小批次
  - 验收：shadow resolution/enable、NoCull/compute cull、capacity growth、shutdown smoke
  - 停止线：若 Forward 同时依赖 owner，先抽最小 `ShadowFrameResources`，不把整个 Deferred owner 公共化
  - 提交：2-3 个 `[runtime/shadow] ...` 提交

- [ ] `FG-206` 用真实 Deferred 路径证明 buffer pool/reuse
  - 依赖：FG-205、FG-110
  - 修改：优先选择 shadow cull/indirect 或调查确认的 GPU-only scratch consumer
  - 验收：至少一个真实 runtime logical buffer 由 graph registry/slot owner；连续帧有 pool hit；若同帧存在兼容非重叠资源，必须观察到 physical slot alias
  - 停止线：不得为了制造漂亮 reuse ratio 添加无业务用途 buffer；若当前没有同帧 alias 候选，记录 inventory，保留 core alias test，并以真实跨帧 pool hit 作为本阶段 runtime 证明
  - 提交：`[runtime/deferred] consume graph transient buffer pool`

## P3 Typed resources 与 pass parameters

- [ ] `FG-301` 定义 DeferredFrameGraphResources 与 frame import result
  - 依赖：FG-201、FG-202、FG-203、FG-204、FG-205、FG-206
  - 修改：新增 `DeferredFrameGraphTypes.h`，调整 pipeline build 代码
  - 实现：集中保存本帧所有 RG handles；不保存 resolved pointer；optional resource 显式 optional
  - 验收：顶层不再散落局部 handle 命名；类型可由单元测试构造
  - 提交：`[runtime/deferred] add typed frame graph resources`

- [ ] `FG-302` 为 GBuffer pass 增加单一参数对象
  - 依赖：FG-301、FG-103
  - 修改：GBuffer pass module/pipeline
  - 实现：同一 params 驱动 setup 和 execute resolve；frame/light/skinning 从 binding context 获取
  - 验收：execute 不调用 Stage buffer/resource getter；resolve validation 通过
  - 提交：`[runtime/deferred] parameterize gbuffer graph pass`

- [ ] `FG-303` 为 SSAO 和 Deferred Light 增加参数对象
  - 依赖：FG-301、FG-203
  - 修改：SSAOStage、LightStage、pipeline
  - 验收：删除 `setSSAOTexture()` 和 GBuffer resolved image 回灌；descriptor input 只从 pass params resolve
  - smoke：SSAO on/off、IBL PBR sphere
  - 提交：可拆 SSAO/Light 两个提交

- [ ] `FG-304` 为 Skybox、Scene Overlay、Viewport Overlay 增加参数对象
  - 依赖：FG-301、FG-204
  - 验收：execute 只消费 frame snapshot + handles；没有 scene/service query；overlay callback 变为明确 pass builder input
  - 提交：`[runtime/deferred] parameterize overlay graph passes`

- [ ] `FG-305` 为 Bloom/ToneMap/Postprocess 增加参数对象和显式 exports
  - 依赖：FG-301、FG-105
  - 修改：PostProcessingStage、BloomPostprocessing、BasicPostprocessing
  - 验收：删除 `resolvePreparedResources()/clearPreparedResources()` 反向回灌；debug bloom/output 通过 execution result export
  - 提交：`[runtime/deferred] export postprocess graph outputs`

- [ ] `FG-306` 为 Shadow 子图增加 typed inputs/outputs
  - 依赖：FG-205、FG-301
  - 验收：shadow append 返回资源/最后 pass 的 typed result；Light 参数显式引用 sampled shadow；不通过 ShadowStage getter 找资源
  - 提交：`[runtime/shadow] expose typed shadow graph outputs`

## P4 Deferred 顶层 Orchestrator

- [ ] `FG-401` 新建 DeferredFrameGraphOrchestrator 并搬迁纯 build 顺序
  - 依赖：FG-302、FG-303、FG-304、FG-305、FG-306
  - 修改：新增 orchestrator 文件；缩短 `DeferredRenderPipeline::executeDeferredMainGraph()`
  - 实现：build 函数按 Shadow -> GBuffer -> SSAO -> Light -> Overlay -> Postprocess 顺序；pipeline 负责 frame boundary 和 execute
  - 验收：单个入口可读出完整流程；不改变截图/draw count；无公共虚基类
  - 提交：`[runtime/deferred] expose top-level frame graph orchestration`

- [ ] `FG-402` 删除 Deferred 主链内局部 RenderGraphExecutor
  - 依赖：FG-401
  - 修改：SSAO/Postprocess/Bloom/Directional/Point/Cull 独立 execute compatibility path
  - 实现：主链 append helper 不持有/调用 executor；确需 utility standalone 的入口由独立 utility owner 明确持有
  - 验收：Deferred world frame 只有 pipeline/orchestrator 一个 executor；shutdown order 明确
  - 提交：按 shadow/postprocess 两批清理

- [ ] `FG-403` 使用 execution result 导出 viewport/debug resources
  - 依赖：FG-401、FG-105
  - 验收：`DeferredGBufferResources`/`DeferredViewportResources` 不再作为 owner snapshot 双写；editor/debug API 不反查 Stage/registry
  - smoke：render target editor、screenshot、resize
  - 提交：`[runtime/deferred] publish frame graph outputs`

- [ ] `FG-404` 增加 Deferred orchestrator 结构测试和 graph dump baseline
  - 依赖：FG-401、FG-402、FG-403
  - 验收：可选 SSAO/shadow/postprocess 的 pass presence/dependency；固定 dump 不依赖 GPU/App 启动
  - 提交：`[test/render] lock deferred frame graph structure`

## P5 Pass binding 收口

- [ ] `FG-501` 实现内部 RGPassBindingContext
  - 依赖：FG-103、FG-401
  - 修改：RenderGraph internal API 与 tests
  - 实现：resolve declared handles 并辅助写现有 descriptor；生命周期挂到 command buffer completion
  - 禁止：公开 Vulkan/set/binding；手写 shader layout mirror
  - 提交：`[render/graph] add pass-scoped resource binding`

- [ ] `FG-502` 迁移 SSAO/Light/Skybox descriptor binding
  - 依赖：FG-501
  - 验收：这些 pass 不缓存 graph-owned image view handle；输入变化由当帧 params 驱动
  - smoke：SSAO、IBL、pipeline switch、resize
  - 提交：按 SSAO+Light、Skybox 两批

- [ ] `FG-503` 迁移 GBuffer frame/skinning binding
  - 依赖：FG-501、FG-302
  - 验收：frame/skinning descriptor 来自 current-flight params；material descriptor cache 保持独立
  - 提交：`[runtime/deferred] bind gbuffer frame resources from graph`

- [ ] `FG-504` 调查并设计 generated ShaderParameterBlock
  - 依赖：FG-502、FG-503
  - 产物：在 `progress.md` 写真实 shader generation/layout inventory 和最小方案
  - 停止线：没有第二个实际 consumer 前不实现通用 public API
  - 提交：plan-only 或与首个最小 binder implementation 分开

## P6 Presentation 与 Capture

- [ ] `FG-601` 将 presentation capture 声明为 graph copy/readback pass
  - 依赖：FG-105
  - 修改：RenderRuntimeFrame、AppScreenshotCapture
  - 验收：无 graph 外裸 `recordPresentationCapture(cmdBuf)`；source/final state 在图中可见
  - 提交：`[runtime/capture] declare presentation readback in graph`

- [ ] `FG-602` 调查 presentation 合图收益与状态边界
  - 依赖：FG-401、FG-601
  - 产物：记录合并/保持独立决定、swapchain identity、ImGui 和 multi-image executor 约束
  - 默认：保持独立，除非能删除真实重复状态/executor 且不复杂化 swapchain scope
  - 提交：plan-only

- [ ] `FG-603` 按 FG-602 决策收口 presentation orchestrator
  - 依赖：FG-602
  - 验收：顶层 `renderFrame()` 顺序清晰；acquire/submit/present 仍在 graph 外；capture dependency 显式
  - 提交：`[runtime] clarify frame graph and presentation flow`

## P7 Forward 全量迁移

- [ ] `FG-701` 建立 ForwardFrameResourceSet 和 typed graph resources
  - 依赖：FG-404、FG-501
  - 实现：复用经 Deferred 证明的公共部分；先迁 frame/light/skinning owner
  - 验收：ForwardViewportStage 不持有这些 per-flight buffer
  - 提交：2 个 `[runtime/forward] ...` 提交

- [ ] `FG-702` 迁移 Forward PBR/Phong passes
  - 依赖：FG-701
  - 验收：顶层 graph 分别可见 PBR/Phong pass 和资源；Stage 不再隐藏顺序
  - smoke：PBR sphere IBL、Phong object、shadow

- [ ] `FG-703` 迁移 Forward Unlit pass
  - 依赖：FG-701
  - 验收：Unlit 独立 graph pass/params；截图一致

- [ ] `FG-704` 迁移 Skybox/Simple/Direction/Debug/Viewport Overlay
  - 依赖：FG-701
  - 验收：每个逻辑 pass 在 graph dump 可见；scene input 来自 frame snapshot

- [ ] `FG-705` 迁移 Forward postprocess 和 output export
  - 依赖：FG-702、FG-703、FG-704
  - 验收：复用 common postprocess append contract；viewport/debug output 由 execution result 导出

- [ ] `FG-706` 新建 ForwardFrameGraphOrchestrator 并删除 executePasses 固定顺序
  - 依赖：FG-702、FG-703、FG-704、FG-705
  - 验收：顶层可读完整 Forward 流程；一个 world-frame executor；删除 dirty refresh compatibility path
  - 提交：`[runtime/forward] expose top-level frame graph orchestration`

- [ ] `FG-707` 增加 Forward graph structure tests 和 pipeline switch matrix
  - 依赖：FG-706
  - 验收：Forward graph dump、可选 pass、Deferred/Forward switch、resize/shutdown 全通过

## P8 GPU Resource API 收尾

- [ ] `FG-801` 统一基础 resource desc immutable/replacement 契约
  - 依赖：FG-102、FG-701
  - 修改：RenderResourceFactory、Buffer/Image/View/Sampler desc；保持兼容 adapter 有明确删除点
  - 验收：resource spec 可比较；replacement 不做对象内部 resize；无 Vulkan 类型泄漏

- [ ] `FG-802` 统一 buffer map/write/flush range 与失败行为
  - 依赖：FG-104、FG-801
  - 验收：unit tests 覆盖 host visible/non-visible、越界、flush/readback

- [ ] `FG-803` 提取 Texture decode/import 与 TextureUploadService
  - 依赖：FG-801
  - 修改：Resource/Texture、Render/Core/Texture；先迁 2D 资产，后迁 cubemap/fallback
  - 验收：Texture 不自行 begin/end isolate commands；上传服务显式依赖 factory/command submission

- [ ] `FG-804` 删除 Texture 全局 factory 与 render attachment API
  - 依赖：FG-803、FG-706
  - 验收：删除 `Texture::getResourceFactory()`、`Texture::createRenderTexture()`；资源创建路径无 `App::get()`；GPU intermediate 无资产 Texture

- [ ] `FG-805` 审计所有 GPU 资源创建和 owner
  - 依赖：FG-804
  - 验收：新增资源走 factory；owner 分类符合 plan；view-before-image 和 submit-time lifetime tests 通过

## P9 完成清理

- [ ] `FG-901` 删除 obsolete Stage resource getter/setter、局部 executor 和 compatibility execute 入口
- [ ] `FG-902` 检查 pass execute 中无 create/resize/destroy/waitIdle/App::get/scene query/未声明 transition
- [ ] `FG-903` 完成 Deferred/Forward graph dump 与视觉基线对比
- [ ] `FG-904` 运行完整 unit/build/editor smoke matrix 并记录 artifacts
- [ ] `FG-905` 更新 `render-arch`、`resource-system`、`debug-review` 和旧计划状态
- [ ] `FG-906` 独立规划 DrawList/ShaderParameterBlock/Editor Extension 后续，不混入本计划收尾
