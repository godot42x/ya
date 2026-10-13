## 当前架构要点

1. `App` 不再直接编排整条渲染管线。`RenderDeviceState` 持有录制所用的机制、对顺序没有主张；一帧的顺序（offscreen pump → flight → declare → extract → prepare → build → acquire → record → extras → submit）由应用侧 `GameRuntime/Render/RuntimeRenderContext` 逐步写出（`render-application-boundary` AB7）。`IRender` / `VulkanRender` 才是 RHI backend。不要再引入名为 Coordinator 的第二套产品层概念。
2. 渲染抽象仍以 `IRender` 为核心；Vulkan 是唯一参与编译的后端。`Backend/OpenGL/` 不构建（`RHI/Backend/xmake.lua`），缺 indirect / compute / buffer barrier，不要按「兼容后端」去维护它；新 GPU 能力走 RHI 能力开关并保留降级路径（`showcase-roadmap` R8），为以后可能的 GL / 低端后端留口。
3. `IRenderPass` / `IRenderTarget` 仍然是有效抽象，不要假设项目已经完全去掉 render pass 概念。
4. 离屏预处理（如 cylindrical -> cubemap、cubemap -> irradiance）由 `ResourceResolveSystem` + `OffscreenJobRunner` 编排，不要把这类流程重新塞回组件里。
5. 资源时序仍是 Vulkan 改动时的第一检查项：避免在 frame recording 中途重建正在使用的 GPU 资源，必要时延迟到下一帧。
6. FrameGraph execute callback 只消费构图阶段生成的 immutable snapshot、typed params
   和 graph-resolved handles；active scene、ECS registry、ResourceResolveSystem 查询
   必须在 graph build 前完成。
7. Deferred/Forward world frame 各自只保留一个顶层 executor；Stage/pass helper 只
   append graph pass 或记录当前 pass，不自行 execute 子图。
8. Game UI 唯一 live 事实源是 `WidgetTree`（`ya-gui-widgets`）；Scene 只保存
   `SceneWidgetEntry` authoring data（inline UIDocument / .yaui 引用）。
   command recording 只消费 `UIFrameSnapshot`（RenderGraph 前由
   `WidgetTree::buildSnapshot` 生成），绝不遍历 live widget 树。
9. 旧 Node2D UI 语义已移除（Phase 6）：不要在新代码里把 UI 挂回 scene tree，
   也不要用 `UISceneRenderer`；场景只保存新格式 `widgetEntries`（inline
   UIDocument / .yaui 引用），`nodeType` legacy UI 迁移已删除（不再支持
   旧格式 scene 文件）。
10. `ya-gui-widgets` 无 Scene/ECS/Render3D/Host/RHI 依赖；texture 归资产缓存、
    font 由 snapshot item 强引用；UI 合成在 world graph 之后（不进 bloom）。
11. 一帧按 Camera 链组织，不是按窗口：`graphics → UI → view compose` 写该相机离屏 RT；
    `display compose → present` 才碰 swapchain。当前 `RenderFramePlan` 仍同时携带 Scene
    与 `PresentFrameInput`，`beginFrameCommandBuffer()` 无 Surface 则拒绝录制，因此
    Scene recording 仍与 present Surface 耦合。acquire/present 由 host `FPresentFrame`
    配对。`CameraFrameInput` 在 graph build 前携带 owner 计算的 view / projection /
    viewProjection / offscreen extent；Forward/Deferred/debug/overlay 只消费该包，
    不从 swapchain/window 猜尺寸。view compose 经 `recordCameraViewCompose` 写 Camera
    离屏 RT；display compose 经 `PresentationGraphService::recordDisplayCompose` 写
    `swapchain[imageIndex]`。swapchain blit 使用 Surface 生命周期的 tone-map CIS，
    不要把 View `post.toneMap` 或空 `RenderDesc` 默认值拿去 bind。`GUIRenderSurface` 只是 compose target，不 acquire/present、
    不读 live WidgetTree。Present 消费方只走 `IRenderSurfaceContext` / `ISwapchain` /
    `buildPresentationImages`，禁止 `as<VulkanSwapChain>()`。不要为 Material/UI 窗复制
    Renderer。对象模型见 `./.agent/plan/archive/gui-multi-os-window-editor/c2_view_model.md`
    与 `c2_present_compose_model.md`；R2 ownership 收口见
    `./.agent/plan/render-view-family/plan.md` 4.0.3。
12. 宿主视口是哪个 View 只有一个判据：`SceneViewDesc::ownsHostViewport()`
    （`composeOntoViewId == 0`，结构属性）。按 id 去找宿主 View 会造出第二个定义，
    并让一个 `viewId=7` 的声明在 plan 里成为 display root 却不是宿主相机的来源。
    **View 身份是 owner-scoped 的**：`SceneViewKey{owner, local}`，owner 由 producer
    自己命名（`ISceneViewProducer::viewOwner()`），local 是它自己的编号；交给输出表
    的扁平 `viewId` 由 key 派生（`owner << 32 | local`），未命名的 owner 或 0 local
    读作 0＝“没有 View”。所以游戏的世界视口与编辑器的作者视口可以各自都是 primary
    而不撞号——全局小整数时代它们只能轮流占 1，"这是哪个 View" 只能去读声明方。
    不要退回全局 id 常量，也不要按 registration 顺序发 owner：那样 key 会随别人插入
    一个 producer 而漂移，`ViewHistoryStore` 这类跨帧历史的键就不稳定了。
    `SceneRenderPlan::displayRootTask()` 在没人 owns 时返回 null，
    这是正常答案（该 tick 不往宿主视口显示任何东西）；`ExtractedSceneRender::
    hostFrameData()` 同理返回 null，绝不回落到“配对 slot 0”——配对顺序是声明顺序，
    与谁是宿主 View 无关。
13. 宿主视口的图片/尺寸访问器只有**一个来源**，没有兜底链：
    `getActiveViewportImageShared` / `getViewportDisplayImageShared` /
    `getViewportExtent` / `getViewOutput` 都只读本帧 published 的 display root，
    未发布就返回 `nullptr` / `{}`。要回落的调用方自己回落（host camera 用 `hostView.viewportRect.extent`），因为只有调用方知道“没有视口时该显示
    什么”。回落必须落在**本 tick 的输入**上：不要写 `resolveViewportExtent` 那种“先读 device
    已发布的尺寸、读不到再回落”的投影函数（2026-09-19 已删除）——它把上一帧的输出尺寸当成了这一帧
    的输入尺寸，而且 init 之后那个分支恒非零，后面的兜底永远不可达。
    published 身份由 `RenderDeviceState::publishViewOutputIdentity` 一处写入
    （`0` 清空），不要回到“遍历 family 逐个写、最后写入者赢”，也不要让 pipeline 上
    再留一份上次发布的图当兜底。
14. 录制期不许向“当前状态”提问。帧级事实在 `FramePacket`（tick/flight/clock、host render
    scale、shadow settings、UI snapshot），View 级事实在该 View 自己的
    `SceneViewDesc` / `SceneViewportTask` / `RenderFrameData` 上。没有 `CameraFrameInput`
    这种“既是这一帧又是主 View”的包，也没有 `cameraForViewRecording` 这种“拷贝宿主再
    逐字段覆盖”的 patching；需要 Scene 相关的 GPU 绑定（skybox / IBL descriptor set、
    env lighting 派生资源）时，由 `RenderDeviceState::resolveViewSceneResources` 在
    `beginFrameCommandBuffer` 之前按 View 解析进 `RenderFrameData.sceneResources`，
    而不是让 pass 在录制中途问 owner“现在哪个 Scene 是当前的”。
15. 不要在 pass 之间共享一个 `IRenderRuntimeServices` 式接口去取 device 上的东西。
    需要的东西要么进 View 数据（上面的 scene resources / clock），要么在构造时注入
    （`DebugRenderSystem` 走 InitDesc）。`getGameplayResourceBinding()` 这类只有声明没有
    消费者的接口方法，出现即删——否则它会成为下一个“录制期全局查询”的入口。
    同一条判据适用于**字段和整条 pass**：如果一个数据通道（`FramePacket::overlay` 的四路可选
    vector）没有任何生产者，而“空输入”又是合法输入，那“没有生产者”和“这一帧没有 overlay”在代码里
    长得完全一样，它会带着**一整条每帧空跑的 pass** 活很久（`kTopologyPassOverlay` 就这么活到
    2026-09-19）。删除前先回答“生产者是谁”：找不到生产者的字段 / 通道 / pass 一律删除，不要为它
    补一个消费者。
16. 一帧的录制顺序只写在一个地方：**应用侧** `GameRuntime/Render/RuntimeRenderContext::record`。
    renderer 只提供顺序里的每一步机制（`prepareFrameRecord` / `beginFrameCommandBuffer` /
    `recordViewFamilies` / `retainPublishedViewOutputs` / `endFrameCommandBuffer` / `sealFrame` /
    `acquireSurfacePresentation`），**不再有** `RenderDeviceState::record` 这样的整帧入口——
    那等于让 Framework 拥有「这一帧渲染哪些 View、哪个窗口 present」的排布。host 通过
    `IFrameRecordExtensions`（`recordViewCompose` / `recordBeforeDisplayExtensions` /
    `recordDisplayExtensions` / `appendDisplayCapture`）在这些阶段里录自己的东西，
    阶段名是 Render3D 的词汇。**不要**把 `std::function` 放进 `RenderFramePlan`：
    plan 是数据，行为挂在数据上会让顺序一半在 host 构造处、一半在 renderer 调用处，
    两边都读不出完整时序。每个阶段无条件被调用，“这一步什么都不录”用默认空实现表达
    （headless / UI-only 帧合法如此），不要用断言把合法帧判成错误。
    它拆成 `prepareFrameRecord()`（所有改状态 / 备 GPU 资源的动作）与「录命令」那几步：
    "safe point 在哪"必须是一条能被读出来的边界，不是没写下来的约定。
17. **窗口是呈现面，渲染分辨率是设置。** `HostViewState::renderResolution` 是"宿主视口要渲染
    多大"，`INativeWindow::getWindowSize()` 是"窗口多大"，两者可以不同且**互不派生**：
    presentation pass 把渲染图拉伸到 swapchain image 上，所以 resize 窗口不改变渲染内容
    （默认分辨率与窗口创建尺寸一致，于是开箱是 1:1 呈现）。因此：

    - 不要把窗口尺寸喂给 View 的 rect / camera aspect / UI 逻辑视口。跟着窗口走会让"渲染内容"依赖
      一个只影响呈现的量。
    - 不要让"渲染分辨率"以"窗口大小"为唯一来源却没有设置通道；调分辨率是一个产品能力（降分辨率换
      性能），它需要一个显式的设置入口（`AppRenderServices::setRenderResolution`），
      而不是一个只能由 CLI 尺寸冻结的隐式值。
    - View 的 rect 属于声明它的那一方。不要把某个 View 的 rect 抄进宿主状态再让别处从宿主读——
      "这一帧实际渲染成多大"问 renderer 的已发布输出（`getViewportExtent()`）。
    - presentation 的拉伸是**按渲染图像素 1:1** 贴上去的（只 tone map，不做 fit）。宽高比不同的
      窗口会被拉伸；letterbox / fit 是一个需要先决定"多出来的像素画什么"的呈现特性，要单独做。
    - `FollowWindow` 写入的分辨率是窗口 **drawable**（`SDL_GetWindowSizeInPixels`），不是逻辑点。
      窗口以 `SDL_WINDOW_HIGH_PIXEL_DENSITY` 创建，这样 swapchain 才是设备像素。`Hold`（编辑器
      面板）把 View 的 `outputRect.extent` 写成 `logical * pixelDensity`，`pixelDensity` 是设备像素
      每逻辑点，挂在 `SceneViewDesc` 上，由拥有这块表面的一方填写。逻辑布局是 `extent / pixelDensity`。
      `renderScale` 是超采样设置，不参与这个密度，也不决定 View 尺寸。`ExplicitStretch` 的分辨率就是
      调用方要的像素，View 密度为 1。
18. **View target 资源只有一套所有权，落在 `ViewTargetStore`。** 对任意一张 View texture，
    生命周期是固定的：由 store 在安全点按 `ViewTargetRequest` 创建（exact reuse，generation 递增标记
    replacement）；长期持有者是 store 的 live View allocation；本帧使用由 `RenderSubmission` 持有的
    `ViewTargetLease`（一个 allocation 整体 retain，不做逐 attachment 保活）；graph 以 imported
    texture 访问；只在 graph execute **成功后**本 flight 才 publish（`ViewTargetStore::findPublication
    (flight, viewId)` 是 present / picking / debug 唯一查询入口）；request 变化时在安全点替换；
    `unregisterView()` 是唯一的正确性 GC——立即撤掉所有 flight 的可见发布并释放长期引用，已被录制
    submission / flight 引用的旧 generation 活到 fence 完成。因此：

    - View 存在（registered）与"这一帧渲染不渲染"（request 缺不缺席）是两件事：某帧没有 request
      只意味着这一帧没有新输出，registered View 的 allocation 继续驻留。
    - 不要用 "连续 N 帧没看到就猜销毁" 的 GC 代替 `unregisterView()`，也不要让 pipeline / stage 保存
      "上一帧哪个 View 的资源"（`ViewResourceTable` / `reconcilePublishedViews` / `_publishedViews`
      这类东西已删除，出现即删）。
    - attachment 身份用**稳定角色有限枚举** `EViewAttachment`（SceneColor / SceneDepth / DisplayColor /
      EntityId / GBuffer0..3 / SSAO / BloomExtract / BloomBlur / BloomComposite），不要退化成
      `unordered_map<string, texture>` 或 pipeline 私有字符串 key；PSO format variant 在新 allocation
      创建前准备好，录制中途不许重建 target。Shadow map 不属于 View target（shadow strategy 持有，
      经独立 shadow diagnostic provider 发布）。

### 复盘：一个字段同时承担多种语义时怎么发现
2026-09-19 修掉的两处都是同一形态——**同一个字段在不同模式下指不同东西，因此谁也不敢删它**：
`HostViewState::viewportRect` 在三种模式下分别是"CLI 冻结尺寸 / 编辑器面板 rect / 上一次残留"，
`AppRenderServices::setViewportRect` 于是既像设置又像测量。查法是把每个写者和读者按模式列一遍，
问"这一行在每种模式下读到的是什么"；答不出来的那个字段就是错的。修法通常是**按语义拆成两个名字
不同的东西**（`renderResolution` 设置 vs `getWindowSize()` 测量），而不是继续加注释解释它。



## 世界管线的组合层（recipe + feature）

两条世界管线各有一份 typed recipe，是"这帧画什么、按什么顺序、什么数据流向谁"的唯一地图：

- `Render3D/include/Render3D/Deferred/DeferredPipelineRecipe.h`：GBuffer → SSAO(可选) → Lighting → forward-opaque 槽 → Skybox → Sprites → Bloom(可选) → Overlay → EntityId → Finalize。
- `Render3D/include/Render3D/Forward/ForwardPipelineRecipe.h`：Opaque(clear + 全 shading model) → Skybox → transparent 槽 → EntityId → Bloom(可选) → Finalize。

**新增一个 pass**：写一个满足 `Feature` concept 的 feature（`resolve` / `prepareBindings` / `append`，见 `Composition/FeatureProtocol.h`），在 recipe 的 `resolve`/`prepareBindings`/`appendGraph` 各接一行。数据流靠 feature 之间传 typed handle（`outputs.gbuffer.albedo → lightingInputs.albedo`），不需要改 RDG core、pipeline context 或任何 bindings bag——`FeatureProtocolTest.TestOnlyFeatureRunsTheFullChainWithoutCorePlumbing` 就是这条承诺的可执行形式。两个 recipe 头部各有 `# Adding a pass` 小节，实现时读它。

**feature 协议是被强制的**：每个 feature 头文件有 `static_assert(Feature<X>)`，签名漂移会编译失败而不是静默退化。

**跨管线共享**：`Composition/Features/` 下的 `BloomFeature` / `EntityIdFeature` / `FinalizeFeature` / `ShadowFeature` 被两条管线共用；管线各自的 pass 名与 import 标签由 `Settings` 从 `DeferredPassNames.h` / `ForwardPassNames.h` 传入（feature 内不写死）。

**诊断**：`--render-graph-trace` 下，两管线在 resolve 时打印每个 feature 的开关与声明的 targets，在 append 后打印本次贡献的 pass 区间（`passes [a..b)`），可与 graph dump 的 pass 列表逐条对照。



## Dynamic Rendering / Layout 约束

1. Vulkan 路径仍依赖显式 layout transition，不要假设驱动会自动兜底。
2. `AttachmentDescription::initialLayout` / `finalLayout` 必须与实际 begin / end rendering 约定一致。
3. 多 layer image 的 barrier 必须覆盖所有 layer / mip，避免只过渡 layer 0。
4. 若怀疑 layout 问题，先看 `VulkanCommandBuffer`、`VulkanImage`、`VulkanRenderTarget` 的 transition 路径是否一致。



## 2D draw path

两条路径，不要混成一条。

**世界精灵**（`Sprite2DStage`，场景图里、深度只读、在 skybox 之后 bloom 之前）：

- 一个共享 quad 网格，binding 0 顶点速率。每个精灵是 `Sprite2DWorld.slang` 里
  `[YaVertexInput] struct SpriteInstance`（72 字节：worldCenter、textureIndex、
  axisX、axisY、uvRect、tint；翻转已折进 uv）。顶点入口是
  `[[vk::location(3)]] SpriteInstance`，SPIR-V location 仍是 3–8。生成头的
  `SpriteInstanceFields` 是 location / offset 的事实源，`Sprite2DStage` 用
  `vertexAttributeFormat` 把它填进 binding 1 的顶点属性。实例数据走
  binding 1、`EVertexInputRate::Instance`（`VertexBufferDescription::inputRate`；
  速率是管线状态，不是 SPIR-V 修饰）。不要改回逐精灵 push constant，也不要改成
  host-visible SSBO。
- 纹理槽下标是 flat varying（`nointerpolation`），片元用 `uTextures[slot]`，和
  `Sprite2DScreen.slang` 一样，不依赖 descriptor indexing。
- 合批在 `Render2D/TextureTableBatch.h`：`planInstancedDraws` 是纯 CPU。已排好的
  顺序不许为合批重排。连续精灵共用一张 16 槽表，槽 0 是白纹理；下一张纹理装不下
  就切批并在新表里重映射，不丢精灵。键是绑定身份（image view + sampler），epoch
  直接映射，O(1)，不再每帧线性 `slotFor`。
- 实例缓冲是 stage 上按 flight 的 `FrameUploadArena`（只作 VertexBuffer），容量只增
  不减，增长和描述符集池的增长都在 graph 录制之前（`updateTextures`）。同一
  submission 内不覆写已上传的区间。描述符集按 flight 复用，用完才长。
- 管线状态仍只有一条：SrcAlpha / OneMinusSrcAlpha，深度 LessOrEqual 只测不写，
  `a < 0.01` discard，背面剔除 + CCW。空列表不加 pass。

**屏幕绘制**（`ScreenDrawList` / `ScreenDrawRecorder`）仍是展开顶点，不是实例：

- 顶点格式仍是 `ScreenVertex`。列表用 `TextureTableCursor` 做整表目录（`fromTexture`，
  槽 0 是 null/白，容量远大于 16，所以**不**在 command 边界上按纹理切）。录制时
  `planScreenDrawRemap` 用同一张 16 槽表重映射：槽 0 白，键相等复用，装不下就
  flush 并在新表重映射。null 纹理进槽 0。clip 变化和 `MaxVertexCount` /
  `MaxIndexCount` 只 flush 几何、不清纹理表；表满的判断在放三角形之前，和迁移前
  一样（未映射的槽在表已满时会切批，包括还没映射过的 null）。
- 采样模式（Coverage / SDF / Opaque）、圆角、字体图集、clip 栈、帧环和描述符池
  都不变。UI 的键只有 `Texture*`；sampler 来自纹理的 sampler category，同一张
  `Texture*` 不会带两套 sampler。世界精灵的键仍是绑定身份。


