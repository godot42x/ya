---
name: ya-render-arch
description: YA Engine 渲染架构、Renderer 边界与 shader 生成链路。
---

## 适用场景

- 修改渲染管线、Renderer/DeviceState、后端实现或 RenderTarget / RenderPass
- 需要判断改动应放在抽象层、运行时编排层，还是平台后端层
- 关键词涉及：`Renderer`、`RenderDeviceState`、`IRender`、`IRenderTarget`、`IRenderPass`、`VulkanRender`、`OpenGLRender`

## 当前架构要点

1. `App` 不再直接编排整条渲染管线。当前产品层持有 `RenderDeviceState` + `RenderFrameCoordinator`；这是一次不完整拆分（Coordinator 经 friend 写 DeviceState），不是两个独立 owner。目标是公开 `Renderer::recordFrame(plan, surfaceTarget) -> RecordedFrame`。`IRender` / `VulkanRender` 才是 RHI backend。不要再引入名为 Coordinator 的第二套产品层概念。
2. 渲染抽象仍以 `IRender` 为核心；Vulkan 是主后端，OpenGL 仍是兼容后端。
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

## 帧索引与缓冲规范（多窗口世界的五个计数器）

单窗口时代 `hostTick ≈ deviceFrameIndex ≈ surfaceSlot`，混用不出错；多窗口后影子消失，
每个量必须用在自己的问题上。判据只有一条：**资源的复用由谁守卫，就用谁的索引。**

| 量 | Owner | 环大小 | 推进时机 | 合法消费 |
| --- | --- | --- | --- | --- |
| `hostTick`（`App::_hostTick`） | 应用 | 单调无环 | 每次 `iterate` | UBO `frameIdx`、动画、frame token、automation 对账 |
| device frame index（`IRender::recordedFrameIndex()`） | 渲染设备 | 单调无环 | `beginRecordedFrame()`（唯一推进点） | frame fence 槽、deferred deletion 世代、GPU timing 环 |
| flight index（= deviceFrameIndex % framesInFlight，纯函数） | 设备帧序的派生 | `kFramesInFlight`（device 环，唯一环大小源） | 不自己推进 | per-frame command buffer、submission 槽与 keepalive、View 发布表 |
| surface frame index（`currentFrameIdx`） | **每个 surface 自己** | `flightFrameSize`（= `kFramesInFlight`） | 每次 `surface->begin()` | image-available semaphore 环、present-complete fence 环 |
| swapchain image index（acquire 返回值） | swapchain / driver | image count（driver 协商） | 每次 acquire | 该图本体、per-image render-finished semaphore、GUI per-image command buffer / compose target、present fallback |

规则：

1. **flight index 是 device 的量，不是窗口的量。** 它是 device frame index 的纯函数
   （`resolveFlightIndex`），守卫它的 fence 是 device frame fence。任何"用 device 计数器
   推 surface 环"或反过来的写法都是旧单窗口遗留，多窗口下静默错位。
2. **surface 环各走各的。** 窗口最小化/acquire 拒绝时该 surface 的环停走（不 acquire 就
   不推进），device frame 环照常走，其他窗口不受影响——这正是分层的目的，不是要修的坑。
   多窗口后不存在"全局的 per-swapchain frame index"，一窗口一个。
3. **image index 只在 acquire→present 之间有效**，由 driver 决定（可乱序可重复）。录制期
   禁止回查 swapchain 取图（用 plan 携带的 acquired token，见 `PresentFrameInput::imageIndex`）。
   GUI 按 image index 组织 per-image 资源是合法模式（守卫 = 图被重新领走）。
4. **`hostTick` ≠ device frame index。** 前者是产品节拍（UBO/动画/frame token 的轴），
   后者是 GPU 帧世代。今天 1:1，但设备建立前、headless、跳渲染时分叉；字段名必须区分
   （`FramePacket::hostTick` / `RenderStageContext::hostTick`）。
5. **缓冲深度的旋钮只有一个：`kFramesInFlight`**（`RenderDefines.h`，device 的 CPU/GPU
   重叠深度，全引擎共享）。`MAX_FLIGHTS_IN_FLIGHT` 只是 per-frame 表的容量上界（有
   `static_assert` 保证环 fits）。swapchain image count 不归这个旋钮管——它与 presentation
   engine 协商（`minImageCount` 夹 capabilities），索引的是图不是帧。调大
   `kFramesInFlight` 前先读 `temporal_semantics.md` M4：所有 per-frame 表要一起验证轮转。

## 目录锚点

- `Engine/Source/Applications/GameRuntime/`：产品应用循环、生命周期与自动化
- `Engine/Source/Framework/RHI/`：渲染硬件接口层（`Render.h` / `Core/` / `Shader/`）
- `Engine/Source/Framework/RHI/Backend/Vulkan/` 与 `Backend/OpenGL/`：后端实现
- `Engine/Source/Framework/Render/Render3D/`：DeviceState/Coordinator、管线与渲染服务
- `Engine/Source/Framework/Render/Graph/`：RenderGraph 编译与执行
- `Engine/Shader/`：Slang 源与生成头

公开头一律从公开路径读（`RHI/Render.h`、`Render3D/RenderDeviceState.h`），
模块根目录只有私有头与 `.cpp`。

## 相关 skills

- `ya-build`：改到 shader 生成、xmake 目标、测试入口时一起看
- `resource-system`：涉及 offscreen preprocess、skybox / environment lighting 时一起看
- `material-flow`：涉及材质上传、descriptor、runtime material 时一起看
- `cpp-style`：需要收敛类边界、生命周期、最小改动时一起看
- `debug-review`：做 Vulkan 问题复盘或提交前自检时一起看

## Windows DLL Boundary

1. Windows 下若某个库包含单例、全局状态、registry、延迟初始化队列或 UI context，不要让多个 DLL 各自静态持有一份。
2. 这类库要么集中到唯一 shared owner，要么由 engine API 做边界封装。
3. 宏/模板注册允许多模块执行，但注册目标必须共享，且注册 API 要幂等。
4. 具体案例与排查单见 `./windows-dll-boundary.md` 与 `../../memories/windows_dll_boundary.md`。

## 决策原则

1. 公共能力先落抽象层，再按后端补实现。
2. 后端差异只在平台层扩散，不反向污染上层接口。
3. 渲染编排优先放在公开 `Renderer` / pipeline / system，不要把 orchestration 打散到 component，也不要再并列一个几乎无状态的 Coordinator。
4. 遇到渲染异常时，优先检查初始化顺序、资源生命周期、layout transition，再查 shader / pipeline state。
5. 生成文件是只读产物；shader 头不对时修 `Shader.xmake.lua`、`slang_gen_header.py`，不要直接改 `Generated/*`。

## Shader 生成流程

Slang 是引擎唯一的 shader 语言（GLSL / shaderc 路径已退役，`Engine/Shader/GLSL` 与 `GLSLProcessor` 都已删除）。当前链路是配置 + Slang 两段：

```text
Engine/Config/Engine.jsonc
  -> Shader.xmake.lua Step 0
  -> 生成 Engine/Shader/Slang/Common/Limits.slang

Slang 源文件
  -> slang_gen_header.py
  -> Engine/Shader/Slang/Generated/*.slang.h

C++
  -> include 生成头
  -> 使用 slang_types:: 命名空间中的常量与结构体
```

落地规则：

1. 不要手写与 shader-facing layout 对应的 C++ 结构体，包括 UBO、SSBO、push constant、indirect command，以及标了 `[YaVertexInput]` 的顶点输入结构。
2. C++ 侧必须 include `Engine/Shader/Slang/Generated/*.slang.h`，消费 `slang_types::` 里的生成类型。
3. 若生成头缺少需要的 shader-facing 类型，修 shader 源或生成脚本，不在 C++ 侧补 mirror struct。
4. 顶点输入默认不进生成头（varying 绑定没有 uniform offset）。需要 C++ 记录时，结构体标 `[YaVertexInput]`（`Common/VertexInput.slang`），并作为顶点入口参数。Slang 把字段 `binding.index` 记成相对该参数的下标，SPIR-V location 是参数 `binding.index`（基址）加上这个下标。`Sprite2DWorld` 的网格输入占 0–2，所以实例参数写 `[[vk::location(3)]]`、字段不再写 location，生成表里的 location 才是 3–8。若把 3–8 写在字段上，反射下标仍是 3–8，但 SPIR-V 会再加基址变成 6–11。缺 location 的字段生成失败。布局按 glm 默认对齐，并发出 `VertexInputField`（location、offset、分量数、标量种类）。没标的顶点输入保持不发。不要用一条顶点阶段不读的 push constant 去喂生成器。`VertexInputScalar` 留在各 shader 的生成命名空间里；C++ 用 `vertexAttributeFormat` 模板映射到 `EVertexAttributeFormat`，不另做一份共享枚举。
5. 配置常量优先以 `Engine/Config/Engine.jsonc` 为单一事实源，其余文件只消费不重定义。
6. 若值变更，优先改配置或生成脚本，再运行 `xmake ya-shader`。

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

## 退出条件

- 能明确回答一段逻辑属于抽象层、Renderer / pipeline 编排层，还是平台后端层
- shader 常量、生成头与 C++ 使用链路一致
- 渲染问题已经定位到初始化、时序、layout、shader 或 pipeline state 之一，而不是混在一起
