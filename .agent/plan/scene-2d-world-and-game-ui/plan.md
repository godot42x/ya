# Scene 2D World 与 Game UI 闭环计划

## 0. 计划状态与架构结论

- 状态：planned / 部分前置工作已落地。Render2DList 值化、screen/world shader 与顶点布局拆分、
  textureRef typed 化，以及 Render3D→GUI Compose 解耦均有实现和验证记录。P0 混合语义、资源责任与
  graph 策略未冻结；World2D 仍未接入 Scene workload。
- Game UI compose 与 Scene runtime rendering 必须分开：WidgetTree/UIFrameSnapshot 只进入 GUI
  compose；authored sprites 属于 Scene rendering。不要让 GUI framework 变成 World2D renderer，
  也不要为 UI 复制 RHI device、RenderGraph 或应用主循环。
- World2D 属于 Scene runtime rendering，与 GUI Compose 是不同的 graphics path。不能新增第二套
  Scene scheduler / app frame loop；但纯 2D View 也不能被迫支付 3D attachments/stages。具体由
  runtime graph 里的可选 stage 还是轻量 2D-only graph 承载，留给 P0 按真实成本作决定。
- Render3D 不依赖 ya-gui-compose，也不携带 UIFrameSnapshot。当前代码已由
  GameRuntime::RuntimeRenderContext 在应用录制顺序中显式调用 GUI compose；这条边界已落地，P0
  只需防止后续把它塞回 Render3D。
- 不新增 Transform2D、Camera2D、Node2D 或第二套 Scene 树。
- authored 2D 对象使用现有 Node3D + TransformComponent；XY 是候选平面，vec3.z 如何影响深度遮挡
  或 sprite 顺序由 P0 冻结，不预先把 world depth 和 painter order 当成同一语义。
- 相机仍使用 CameraComponent + owner 的 TransformComponent；正交只是 projection mode。
  具体投影矩阵由 view owner 提供有效 aspect，CameraComponent 不读取 Window/View 全局状态。
- Game UI 仍是 UIDocument + WidgetTree + UIFrameSnapshot；不挂回 ECS hierarchy。
- 2D draw 语义（2026-09-27 review，见 §2.4）：底层上传机制共用；坐标系由 draw list 类型固定
  （`ScreenDrawList` 像素 / `WorldDrawList` 世界）；时序由 pass owner 固定。全局 `Render2D`
  删除，共享 pipeline 归持有 `IRender` 的一方，录制器归画这个目标的一方。GUI 只见屏幕类型；
  场景 sprite 是 SceneSnapshot candidate，不是即时列表。
- 分屏不在本计划范围；多窗口合并提交（render-application-boundary AB4-2d）延后，本计划不依赖它。

## 1. 当前链路与必须保留的边界

当前已经存在的主链路：

    Scene/ECS/Node3D/TransformComponent
      → SceneViewProducer::collectSceneViews
      → SceneRenderScheduler::seal
      → buildSceneSnapshots / ExtractedSceneRender
      → RenderFrameExtractor::prepareView
      → RenderDeviceState::recordViewFamilies
      → view target / view compose / display compose / present

    SceneWidgetEntry(documentPath)
      → UIDocumentStore
      → WidgetTree
      → UIFrameSnapshot
      → GameRuntime/GameEditor 显式调用 ya-gui-compose

主要代码锚点：

- Engine/Source/Framework/Scene/Core/Scene.cpp
- Engine/Source/Framework/Scene/Scene3D/include/Scene3D/TransformComponent.h
- Engine/Source/Framework/ECS/Systems/include/ECS/Systems/Components/CameraComponent.h
- Engine/Source/Framework/Render/Render3D/include/Render3D/Common/SceneViewDesc.h
- Engine/Source/Framework/Render/Render3D/include/Render3D/Common/SceneViewProducer.h
- Engine/Source/Framework/Render/Render3D/include/Render3D/Common/SceneRenderScheduler.h
- Engine/Source/Applications/GameRuntime/Render/RuntimeRenderContext.cpp
- Engine/Source/Applications/GameRuntime/Render/RenderFrameExtractor.cpp
- Engine/Source/Framework/GUI/Runtime/Compose/Render2DComposePass.cpp
- Engine/Source/Applications/GameRuntime/GUI/GameUI/GameUIHost.cpp
- Engine/Source/Applications/GameEditor/UI/Viewport/EditorGameUIPreview.cpp

分层契约：

| 层 | 负责 | 不负责 |
| --- | --- | --- |
| ECS/Scene | authored entity、Node3D、Transform、Scene serialization | GPU material、WidgetTree、swapchain |
| ECS/Scene data | CameraComponent、Sprite2D authored data（具体模块待依赖审计） | command recording、GPU 资源 |
| ya-render-2d（目标） | 上传 ring、flush、纹理槽表；`ScreenDrawList` + `ScreenDrawPipelines` + `ScreenDrawRecorder` | 相机、深度、Scene/ECS、全局单例 |
| 世界空间即时绘制（目标，Render3D 侧） | `WorldDrawList` + `WorldDrawPipelines` + `WorldDrawRecorder`，需要 View 相机与深度 | GUI include、场景 authored 内容 |
| GUI Compose | UIFrameSnapshot 到 `ScreenDrawList` 的 replay，目标由调用方给出 | 相机、深度、scene color、Scene/ECS、World2D extraction |
| Scene runtime renderer | Forward/Deferred 3D stages 与 authored sprite draw；具体纯 2D graph 方案由 P0 决定 | GUI compose、读取 live WidgetTree |
| GUI Widgets | WidgetTree、UIDocument、UIFrameSnapshot、输入/时间策略 | Scene/ECS、Render3D |
| GameRuntime | 一帧的 declaration、extraction、Scene runtime record 与 Game UI compose 顺序 | 把产品顺序隐藏在 Framework |
| GameEditor | 2D/3D authoring camera profile、UI preview、editor overlays | 改写 GUI pipeline |

## 2. 当前真实链路与目标插入点

当前代码可证实的录制顺序是：

    prepare → active Forward/Deferred family graph → Game UI view compose
      → IFrameRecordExtensions::recordViewCompose → display compose → present

SceneViewFamilyPlan 按 Scene/revision/policy 分组；RenderDeviceState::recordViewFamilies
把每个 family 交给单个 active ISceneViewFamilyRenderer。Forward/Deferred 各自构建完整
family graph 并发布 View output。现在没有 World2D recording step，因此不能把目标顺序写成
已经成立的「World3D → World2D → UI」。

目标是把 sprite workload 加入 Scene runtime rendering，同时保证纯 2D 不走无用的 3D graph stages；
不新增第二套 Scene scheduler 或 app loop。Game UI 仍在 world graph 之后写入 View 的 display image；display compose
最后才写 surface/swapchain。World2D 的 graph 插入点必须在 P0 冻结它与 3D opaque、3D
transparent、depth 和 postprocess 的视觉关系之后，不能借用 GUI 的 view-compose 扩展点。

### 2.1 三条渲染职责的语义

1. Scene runtime：负责 Scene authored content。World3D 的 Forward/Deferred 与 World2D sprite
   raster 是 runtime rendering 能力；它们可以共享 RHI、RenderGraph、资源和明确的录制顺序，
   但不能因此共用一套混合语义的 GUI shader / draw state。
2. GUI Compose：单独的 screen-space graphics path，只消费不可变 UIFrameSnapshot；Y-down、clip、
   文字和圆角属于
   GUI compose。它不遍历 Scene/ECS，也不参加 Scene snapshot extraction。
3. GUI Compose 与 Scene runtime sprite draw 使用不同的 graphics-pipeline configuration/state contract；
   不同 contract 不强迫创建两套高层 renderer class，二者仍可由同一低层 Render2D resource owner 管理。
   这也不要求 GUI 拥有独立 device、queue、frame loop 或复制 RenderGraph。仅在证明兼容时复用 shader
   module、geometry、upload 等机制。
4. shader module 的最小数量由 P0 的调用方与状态矩阵决定；screen 与 world pipeline config/state
   必须可区分，底层资源 owner 可共用。不得预先规定 GUIPrimitive、
   GUIImage、GUIText 三套 pipeline，也不以拆分为目标制造 QuadResourcePool、多个 batch owner
   或策略层；只拆已经证明有不兼容数据/状态或资源生命周期的路径。

共用 RHI/RenderGraph/command buffer 等底层能力不等于共用 shader 状态、draw item、排序或
生命周期。反过来，概念分开也不自动要求复制一套完整 quad batching 实现。P1 已完成 screen/world
shader 与顶点布局拆分、typed texture slot 和 Render2DList 值化；World2D 的真实 Scene graph producer
尚未实现。禁止继续由 textureRef bit、global session 或 mode flag 暗中切换语义。

### 2.2 空间与生命周期

- World2D 使用 Scene 世界坐标；UI 使用 Render2D 左上角/Y-down 逻辑像素，二者不混用。
  P0 必须冻结 World2D 的 up/forward、sprite 朝向、z/depth/sort 关系；不能把 GUI 的 Y-down
  坐标直接扩散进 TransformComponent。
- View 的 outputRect 是 View 自己声明的 offscreen rect；窗口尺寸只属于 presentation。
- View 不存在于本 tick 时不提交该 View 的 task/request；同 Scene 的其他可见 View 仍可需要共享
  Scene extraction。已注册 target 的销毁仍由 ViewTargetStore::unregisterView 管理，不能用“连续 N 帧
  没看到”猜 GC。
- Scene snapshot 由 Scene+revision 共享；camera、cull、排序、view output 只属于 View。
- Scene revision 不代表异步资产状态版本。sprite 的 authored asset reference 留在组件；每 tick
  的抽取在同一 Scene 遍历中读取当前 resource version/state，输出不可变、保活到 submit/fence
  完成的 render candidate。Pending/failed/placeholder 和热重载行为必须在 P3/P4 定义并测试。

## 2.3 Architecture Review：2026-09-26

本次 review 发现原计划有几处会把复杂度重新引回来的假设，已按以下结论修正：

| 发现 | 原计划风险 | 修正后的执行口径 |
| --- | --- | --- |
| 把已完成的 Render3D→GUI 解耦误列为待办 | 计划会重复改已落地的边界，且进度与代码不一致 | 当前 RuntimeRenderContext 显式 compose UI；RenderFrameInputs / Render3D xmake 不再依赖 GUI Compose；P0 只做回归守卫 |
| 把“不要第三个 renderer”写成绝对规则 | 纯 2D View 可能被迫创建 3D GBuffer/lighting/shadow/bloom 资源，省概念却增加 GPU 成本 | P0 比较同一 runtime graph 的 content-gated path 与独立 2D-only graph；必须证明纯 2D 不运行/分配 3D stages，再选最小方案；不复制 scheduler/frame loop |
| 在 SceneViewDesc 增加 family mask | View 逐渐变成 renderer policy flags 垃圾桶 | 不加 family mask；确有需要时只加小型 SceneViewContents，不表达 renderer identity |
| 预先规定 GUIPrimitive/GUIImage/GUIText/WorldSprite 四套 shader | 通过“拆文件”制造 PSO、descriptor、batch 和缓存复杂度 | 先做 capability matrix，只拆真正不兼容的 shader/pipeline |
| 把 World2D 固定在 bloom/finalize 某个位置 | 位置同时决定与 3D transparent 的遮挡、是否进 bloom、tone-map 与 alpha blend 语义 | P0 定义并用混合场景验证 MVP 合成顺序；不能只看 pass 名或避免 bloom 来定位置。若 MVP 不支持 sprite bloom，应明说是产品限制，不把它伪装成 renderer 不变量 |
| CameraComponent 读取 outputRect 或继续提供隐式最终 projection/view | 组件依赖窗口/View，或 getOrbitView 修改 Transform | producer 传 effective aspect；CameraComponent 只生成 projection，view 由 producer/controller 生成 |
| Sprite2DComponent 放在 ECS/Systems | gameplay systems 被迫依赖 renderer/asset 语义 | 默认沿用现有 Render3D authored component domain，P0 再确认是否抽到中性 Scene/RenderScene |
| 只写“texture asset reference” | 执行时容易在 component/extractor 内偷偷 resolve GPU 资源 | P0 审计 TextureSlot / AssetManager / ResourceResolveSystem 的现有职责，选唯一的 pre-extraction resolved-binding producer；snapshot 只消费 resolved binding |
| 只提到 entity-id/picking | 2D editor 可能先实现 GPU picking，增加 pass 和同步 | MVP 先用 CPU quad hit-test；GPU picking 单独决策 |
| Game UI 与 editor UI 的 target 没分开 | compose helper 继续吞掉 View/surface/display 三种语义 | 调用方提供明确 target 数据（image、extent、encoding、load/store 与输入逻辑尺寸）；列出的场景是验收样例，不据此创建四套产品 renderer |
| 新计划重复 game-ui-authoring 的 mount/designer phase | 两条计划会同时改 GameUIHost/Scene UI composition | game-ui-authoring 负责 UIDocument/mount/designer；本计划只定义交接契约 |
| Render2D 仍在 QuadRender.cpp 里直接判断 Vulkan，并依赖 Backend TextureLibrary | UI/World2D 拆分后把后端耦合复制到多个 batch；OpenGL 路径更难维护 | P0 查清现有 backend/resource owner 与目标支持范围；只抽确有跨后端消费者的 seam，不预先为未来后端造抽象；新路径不得复制 Vulkan 分支 |
| 旧 Render2D process-global session/cursor | 后一次 begin/flush 可能覆盖前一次状态，且隐式承载 draw kind | 已由 Render2DList build/record 值化移除；P0 保留回归检查，并继续核对 record 阶段 GPU upload/flight 生命周期，不重新引入 begin/end 全局会话 |
| SceneRenderPlan::displayRootTask() 取第一个 bDisplayRoot | 多 OS window 下一个全局 display root 不能表达多个 surface 的输出归属 | 本计划不再扩展 root bool；多窗口先由 surface-scoped View output/presentation 计划解决，2D feature 依赖其契约 |

这些修正不是可选优化，而是后续 checkpoint 的前置条件。若实施中再次出现“新增一个总 pipeline、
一个中心 registry、一个跨域 service 或一个 shader bit 就能接上”的建议，应先退回本节重新审查。

## 2.4 Architecture Review：2026-09-27（2D draw 语义与持有者）

P1 拆开了 screen/world shader 与顶点，但 draw list、录制上下文和 compose owner 仍然混在一起。
本节冻结 2D draw 的语义分层；执行见 §4A（D1–D3）。

### 现状问题（代码可证）

| 发现 | 位置 | 问题 |
| --- | --- | --- |
| 一个列表装两套坐标 | `Render2DList`：`makeSprite/makeText/drawRoundedRect` 是目标像素；`makeWorldLine/makeWireBox/makeWireSphere` 是世界坐标 | 名字叫 2D，内容一半需要相机；读调用方无法判断坐标系 |
| 录制上下文混装 | `FRender2dContext` 同时带 `windowWidth/Height` 与 `view/viewProjection` | GUI 调用方传单位矩阵，同一字段在一半调用里无意义 |
| GUI Compose 替编辑器画 View overlay | `ERender2DComposePassKind::EditorViewportCompose`：`FRender2DComposePassDesc::camera`、`sceneSourceTexture`、`depthTarget` | GUI 模块持有相机、scene color 和场景深度；“GUI 不碰场景”边界的最后一个反向依赖 |
| overlay 相位靠调用顺序 | `recordEditorWorldViewportOverlays`：网格线 → gizmo 屏幕 quad → HUD → 视锥线 → 物理线 → 包围盒线 | 屏幕 quad 不写深度，后画的世界线会盖住 gizmo handle |
| 全局 `Render2D` 无单一 owner | `GUIAppHost` 用 swapchain 格式 init；`PipelineCoordinator` 用 3D 管线格式 init、shutdown 时 destroy；`composePassSlot` 函数级 static slot 池 | 谁先到谁决定格式；设备级资源挂在 Render3D 管线协调器下；pass slot 是给全局单例打的隔离补丁 |
| `makeSprite(mat4)` 收 `mat4` | GUI 线段、gizmo 轴线 | 像素空间旋转与世界矩阵同形，世界矩阵能从这里漏进来 |
| GUI 线段偏向一侧 | `Render2DComposePass.cpp` 的 `EKind::Line`：第二条边是 `lineFrom + nrm * thickness` | 线没有以端点连线为中心 |
| 屏幕空间没有 line / path | 屏幕批次索引是初始化时写死的 quad 模式 `0,1,3,0,3,2` | 只能画 quad；`Line` batch 从来都是世界线（GUI 传单位矩阵时坐标被当成裁剪空间） |

### 目标语义：四层

| 层 | 内容 | 共用 / 拆分 |
| --- | --- | --- |
| RHI / RenderGraph | 不关心 space | 共用 |
| 上传机制 | 顶点/索引 ring × flight、flush 分段、纹理槽表 | 共用一份实现（内部类型），屏幕与世界录制器各自持有实例 |
| shader / PSO | `Sprite2DScreen`（屏幕）、`Sprite2DLine`（世界空间即时绘制）、`Sprite2DWorld`（场景 sprite，P4） | 按图元语义拆分；同一 shader 按目标格式出 PSO 变体不算拆分 |
| draw list 类型 | `ScreenDrawList`：像素坐标、左上原点、clip 栈、2D 仿射。`WorldDrawList`：世界坐标，录制需要 `viewProjection` 与深度策略，无 clip 栈 | 按坐标系拆分 |
| pass 时机 | 场景 sprite（scene graph）→ View overlay（先世界相位、再屏幕相位）→ 屏幕 compose（每 surface） | 按 owner 拆分 |

归属判据：**一个命令能否不知道相机就被正确画出来？** 能 → `ScreenDrawList`；不能 →
`WorldDrawList`。区分轴是坐标系，不是 line/quad：像素空间的线属于屏幕列表，
世界空间的调试点/文字以后也进 `WorldDrawList`。名字只表达坐标系，不绑定用途（编辑器标注、
runtime 调试线、脚本 DebugDraw 都是其内容）；“不装场景 authored 内容”由契约保证：列表即时、
每帧丢弃、不进 SceneSnapshot。

### 三类“带深度的世界 2D”不共用一个列表

| | 世界空间即时绘制（网格、视锥、线框、包围盒、物理 debug、脚本 DebugDraw） | 场景 sprite（`Sprite2DComponent`） | Billboard（`BillboardComponent`） |
| --- | --- | --- | --- |
| 形状 | 即时列表 `WorldDrawList` | SceneSnapshot 里的不可变 candidate，instanced draw | `ViewOverlayStage` push constant + quad mesh |
| 生命周期 | 每 View 每帧，用完即弃 | Scene+revision 抽取一次，多 View 共享，保活到 fence | 每 View 每帧从 ECS 读取 |
| 画面位置 | View 输出之上，建议 tone map 之后，不进 bloom | SceneColor 内，随 3D 后处理 | forward-transparent overlay |
| 深度 | 测试，不写 | P0 冻结 | 测试 `LessOrEqual`，不写 |
| 可见性 | 编辑器 / 调试 View | 所有 View | 按 `FRenderFeatureMask` |

不建通用 `World2DList`：让游戏代码每帧往世界列表推 sprite，等于在场景里重建全局 `Render2D`，
绕过 snapshot 去重和唯一 resolve producer。

### 持有者

共享层与目标层分开持有（命名只表达坐标系，`ScreenDraw*` / `WorldDraw*` 对称）：

| 层 | 内容 | 数量 |
| --- | --- | --- |
| `ScreenDrawPipelines` / `WorldDrawPipelines` | shader module、pipeline layout / DSL、按（颜色格式，深度格式）懒建的 PSO 缓存、白纹理引用、静态索引 | 每个 `IRender` 一份 |
| `ScreenDrawRecorder` / `WorldDrawRecorder` | 上传 ring × flight、frame UBO、descriptor set、flush 游标 | 每个“画到某张图”的 owner 一份；取代 pass slot |

录制器开销与今天一个 pass slot 按需分配的资源相同；销毁走 `DeferredDeletionQueue`。flight 槽位
由录制器在 begin 时向设备取（`framesInFlight` / `recordedFrameIndex`）。record 签名显式带目标：
屏幕 `{cmd, extent, colorFormat}`；世界 `{cmd, extent, formats, viewProjection, depth}`。

| 分叉 | 共享层 owner | 录制器 owner |
| --- | --- | --- |
| GUIApp（GUIWorkbench 等） | `GUIAppHost`（它创建 `IRender`）持有 `ScreenDrawPipelines`；无世界层，也不链接 Render3D | 每个窗口 session（主窗与 `GUIWindowManager` extras）各持 present / offscreen 两个 `ScreenDrawRecorder` |
| `ya::App`（GameRuntime / GameEditor） | App 设备状态（`RenderDeviceState` 一侧）持有两种 Pipelines；**不放 `PipelineCoordinator`** | Game UI compose owner（`RuntimeRenderContext`）；编辑器窗口 session；`EditorViewportCompositor` 每 View 一对（世界 + 屏幕）；`EditorUIDesignerSession`；以后 runtime 调试线归 runtime View producer |

`TextureLibrary` / `FontManager` 是资产级单例，本计划不动，只登记。

### 业界参照（只取分层，不取命名）

- Unreal：共用 `FBatchedElements`；`FCanvas`（屏幕，按目标实例化）与 `FPrimitiveDrawInterface`
  （世界编辑器图元，场景渲染器里带深度）分开；Slate 按窗口批次、不碰相机。编辑器图元 → foreground
  gizmo → canvas 的相位顺序。
- Unity SRP：Gizmo 是相机渲染阶段（`DrawGizmos(camera, Pre/PostImageEffects)`）；UI Toolkit / uGUI
  Screen Space Overlay 在相机栈之后。
- Godot：`canvas_item` 与 3D immediate 从服务器接口起就分开；canvas 统一 GUI 与玩法 2D 与本项目
  “场景 sprite 与 3D 共用深度”冲突，不采用。
- ImGui / Slate：屏幕线与路径 CPU 三角化进同一 UI shader，外圈羽化做 AA；不用 GPU `LINE_LIST`
  （1px、`wideLines` 不可移植、无 AA / cap / join）。

### 命名约束

不使用 `Device` 后缀（与 `IRender` / RHI device 混淆），不使用 `F` 前缀一类 UE 风格命名。

## 3. Phase 0：审计并冻结契约

### 目标

先把 UI screen、world billboard、editor overlay、UI canvas preview、未来 World2D 分开命名；
没有完成本 phase 不新增 Sprite2DComponent 或新 shader。Render3D 与 GUI Compose 的依赖边界
已经由应用侧 RuntimeRenderContext 显式 compose 收口；本 phase 只校验该边界，并冻结 Scene
runtime draw 与 GUI draw 的能力、目标和生命周期契约。

### 工作项与参考

1. 审计 Render2DList builder/recordRender2DList、FRender2dContext、FQuadRender::PassPipelines；
   旧 begin/end/session API 已移除，不把它们列为待实现接口。
   参考：Render/Render2D/include/Render2D/Render2D.h、QuadRender.h、Render2D.cpp、QuadRender.cpp。
2. 列出 makeSprite、makeText、makeRectFilledMultiColor、drawRoundedRect、makeWorldLine、
   makeWireBox、makeWireSphere 的全部调用方，按 UI/World2D/EditorOverlay 归类（makeWorldSprite
   已删除）。结论已写入 §2.4，由 §4A D1 执行。
3. 审计 Render2DPassSlot owner。2026-09-27 结论：slot 是全局单例的隔离补丁，D1 以按 owner 持有的
   录制器取代，不再保留 slot 概念。
4. 审计 SceneSnapshot 多 View 共享边界，记录 extraction 与排序是否重复。
5. 审计 EditorViewportCompositor、EditorGameUIPreview、recordCameraViewCompose 的顺序，
   明确 UI Designer canvas 与 World2D authoring 不是同一模式。
6. 回归核对 Render3D/Common/RenderFrameInputs.h、Render3D/xmake.lua 与
   GameRuntime/Render/RuntimeRenderContext.cpp：确认 ya-render-3d 不 include GUI/Compose、
   不暴露 UIFrameSnapshot，UI compose 仍由应用在 View output 与 surface display compose 之间
   显式调用。若 editor layout helper 或 GUI 类型重新进入 Render3D，先归还所属层，不在本计划
   添加兼容桥。
7. 审计 Forward/Deferred graph 的真实阶段：opaque、skybox、transparent、entity-id、
   bloom、finalize。记录 3D opaque/transparent、World2D sprite、bloom、tone-map 的可见性和颜色
   顺序。纯 2D View 必须避免无用的 3D attachment 与 stage；P0 可以批准独立 2D-only graph，
   但不新增第二套 Scene scheduler / frame loop。
8. 审计 CameraComponent 的 view/projection 责任：当前 `getOrbitView()` 会修改 owner's
   TransformComponent，`getFreeView()` 在组件中解析 owner。把这类 view 计算列为迁移项；
   CameraComponent 最终只保存 projection data，view 由 producer/controller 根据 Transform 生成。
9. 审计 authored render component 的模块归属。现有 Mesh/Material/Billboard 都在
   `ya-render-3d` 的 ECS component 目录，不能把 Sprite2DComponent 预先放进
   `ya-ecs-systems`，否则 gameplay systems 会反向承载 renderer/asset 语义。

### 校验

- rg -n "Render2D::(begin|end|makeSprite|makeWorldSprite|makeText)|drawRoundedRect|makeWorldLine" Engine/Source
- rg -n "textureRef|kTexture(Index|Mode)|Sprite2D" Engine/Source/Framework/Render/Render2D Engine/Shader/Slang
- 把调用方表写入本目录 progress.md；不以移动文件或新增 registry 作为完成。
- 发现一个调用方同时依赖 UI 与 World2D 状态时，先拆调用责任，不增加 flag。
- 产出一张 `P0-contract-matrix.md`（可追加到 progress.md）：每个数据/资源/target 的 owner、
  producer、consumer、生命周期、允许的录制阶段。没有 owner 的字段、pass 或 callback 不进入下一 phase。
- P0 必须给出三种输出的明确答案：
  1. `World3D + World2D` 的混合 View；
  2. 纯 World2D View；
  3. 只有 Game UI 或只有 Editor UI 的 target。

P0 还必须冻结 Render2DList build 与 record 的并发边界：list 构建是否并行、record 是否串行、
资源上传与 GPU 引用如何跨同一提交保活。`begin/end` 已移除，不再讨论嵌套 begin 的接口契约。

上述 UI target 是“场景 × target contract”的验证样例，不是按产品名称创建 renderer/API 的依据。compose
入口消费显式 target 数据；只有证明存在不同的生命周期或录制 owner 后，才增加实现类型。

## 4. Phase 1：GUI/Scene runtime draw 边界与 2D draw path（已完成）

### 目标

保持已落地的应用级 GUI composition 边界；确保 GUI Compose 与 Scene runtime sprite 使用可区分的
graphics-pipeline config/state，删除 world/screen 混用字段和 textureRef 高位 flag。shader module、
geometry、upload 是否共享由 capability/lifetime 证据决定，同时保持现有 GUI 像素基线。已记录的
Render2DList 值化、screen/world shader/vertex split 与 textureRef typed 化不重复实施；未完成项是把
Scene runtime sprite workload 接入 Scene graph。

### 已完成的 P1 结果（不重复实施）

- GameRuntime 显式持有 GUI compose 顺序；Render3D 不依赖 GUI/Compose，也不携带 UIFrameSnapshot。
- Render2DList 把绘制构建与 GPU record 分开；旧的 global begin/end/session/立即绘制 API 已删除。
- GUI screen 与 world draw 使用不同 shader/vertex layouts 和 pipeline config；`textureRef` 的 packed
  texture/mode flags 已改为 typed `textureSlot` + `sampleMode`。没有按 primitive 创建多套 batch facade。
- progress.md 记录了 build/test、GUI parity 与 smoke 证据；feature_matrix.json 对应项标为 verified。
  若后续改动触及这些路径，再运行对应回归，不把 P1 重新打开作为新 checkpoint。

### 后续仍适用的约束

- 低层 batch/resource owner 可继续共享；只有 cursor、descriptor、resource-retain 或 flush 生命周期
  被证明不兼容时才拆分，不制造 `QuadResourcePool + GUIQuadBatch + WorldSpriteBatch` 空壳。
- 不恢复 process-global pending kind/cursor 或跨窗口隐式状态；P7 仍需验证 GPU upload 在同一提交中不被
  覆写，且引用活到 submit/fence 安全点。
- Game UI compose 顺序仍由应用 record 函数明确写出；World2D 归 Scene runtime rendering，后续按 P0
  选定的 graph/workload 接入，不能通过 GUI compose 或 recordViewCompose 偷塞。

### 2026-09-27 更正

P1 完成的是 shader、顶点布局与 texture 字段的拆分。draw list 仍混装两套坐标、录制上下文仍混装
extent 与相机、GUI Compose 仍拥有编辑器 View overlay、全局 `Render2D` 仍无单一 owner——
这些不是 P1 的遗留 bug，而是 §4A 的新 checkpoint。P1 不重新打开。

### 回归参考（代码改动触及时运行）

- 参考：Engine/Shader/Slang/Sprite2DScreen.slang、Sprite2DWorld.slang（迁移中的路径，开工时以仓库
  实际状态为准）、Render/Render2D/QuadRender.h/.cpp、
  GUI/Runtime/Compose/Render2DComposePass.cpp、GameRuntime/Render/RuntimeRenderContext.cpp、
  Render3D/Common/RenderFrameInputs.h、
  Engine/Shader/Shader.xmake.lua。
- xmake ya-shader
- xmake b ya-render-2d ya-gui-compose ya-render-3d ya-game-runtime ya-game-editor
- xmake r ya-gui-closure-test
- 运行 Script/automation/gui/run_workbench_gpu_parity.py 和 HelloMaterial 90 帧冒烟。
- rg 检查 Render3D 无 GUI/Compose 或 UIFrameSnapshot 依赖；UI/world draw 的坐标空间、采样模式、
  alpha mode 不再由高位 bit 隐式编码。
- 不接受按 primitive 名称制造四套 shader/batch；但必须能从代码中分别找到 GUI Compose 与 Scene
  runtime sprite 的 pipeline owner/state contract。
- UI Workbench、EditorSurface、Game UI compose 截图与迁移前一致；只允许 label 变化。

## 4A. 2D draw 语义收口（D1–D3）

依据 §2.4。三个 checkpoint 与 Phase 2 互不依赖，可排在 Phase 2 前或后；D2 依赖 D1，D3 依赖 D1。

### D1：draw list 按坐标系拆分，删除全局 Render2D

1. `Render2DList` 拆成 `ScreenDrawList`（ya-render-2d）与 `WorldDrawList`（Render3D 的 View
   overlay 一侧，复用 ya-render-2d 导出的上传机制）。GUI 模块 include 不到世界类型。
2. `ScreenDrawList` 的变换重载收 2D 仿射（3×2），不收 `mat4`。
3. `recordRender2DList` 拆成屏幕 / 世界两个 record；删除 `FRender2dContext` 与
   `ERender2dBatchKind::Line`。屏幕录制签名不出现相机字段。
4. 删除静态 `Render2D`：`ScreenDrawPipelines` / `WorldDrawPipelines` 按 §2.4 持有者表创建；
   `ScreenDrawRecorder` / `WorldDrawRecorder` 由目标 owner 持有。删除
   `acquirePassSlot/releasePassSlot`、`composePassSlot` 静态池、`FRender2DComposePassDesc::passSlot`，
   以及 `PipelineCoordinator` 与 `GUIAppHost` 对 `Render2D::init/destroy` 的调用。
   `FRender2dDebugState` 随录制器或 diagnostics 走。
5. quad-line-quad 夹层测试改为“同一列表内保序”；在 D2 补“世界相位先于屏幕相位”。

验收：parity md5 不变；编辑器截图不变；GUIWorkbench 链接图不含 ya-render-3d；
`rg "Render2D::|acquirePassSlot|FRender2dContext" Engine/Source` 零命中；
`rg "WorldDraw|viewProjection" Engine/Source/Framework/GUI` 零命中。

### D2：编辑器 View overlay 离开 GUI Compose

1. GameEditor 自己拥有 View overlay pass：在 View display image 上先以 View 深度画
   `WorldDrawList`（测试不写），再画 gizmo / HUD 的 `ScreenDrawList`。
2. 删除 `ERender2DComposePassKind::EditorViewportCompose`、`FRender2DComposePassDesc::camera`、
   `sceneSourceTexture` 与 `recordRender2DComposePass` 的 `depthTarget` 参数。
3. Overlay 画在 tone map 之后的 display image 上（用户确认）。线条在已分级的颜色上
   混合，不再写进 HDR scene color。

验收：`rg "viewProjection|depthTarget|sceneSourceTexture" Engine/Source/Framework/GUI/Runtime/Compose`
零命中；gizmo handle 不再被线框覆盖（截图证据）；编辑器 smoke 通过。

### D3：屏幕空间 stroke / path

1. 屏幕录制支持任意三角形：每条命令自带索引段，取代写死的 quad 索引模式。
2. `ScreenDrawList` 增加 `strokeLine`、`strokePolyline`、`strokeRect`、`fillConvexPoly`；arc / bezier
   先采样为路径。AA 用几何羽化，`Sprite2DScreen` 不新增分支；圆角矩形 SDF 分支保留。
3. `UIFrameDrawItem::EKind::Line` 与 gizmo 轴线迁到 `strokeLine`，修正线段偏向法线一侧。
4. `WorldDrawList` 的粗线不在本项；需要时由世界 shader 在 VS 按屏幕空间展开，单独决策。

验收：parity 基线仅线段像素变化，且变化来自居中修正（逐项说明）；新增 stroke 单测覆盖闭合、
退化段、羽化宽度。

## 5. Phase 2：CameraComponent 增加正交模式（已完成）

编辑器正交 XY profile（工作项 6 的工具面）仍属于 Phase 5：本 phase 只让 CameraComponent
能表达正交投影，不新增第二套相机，也不把 `EViewportMode::Mode2D` 改成 World2D。

### 目标

同一个 CameraComponent 支持 perspective/orthographic，view 仍由 TransformComponent 计算；不造 Camera2D。

### 工作项

1. 在 CameraComponent 中增加 projection mode、正交垂直尺寸和 near/far 的反射/序列化字段。
   仍然只有 Perspective/Orthographic 两态，不添加 Camera2D。
2. 删除“无参数 `getProjection()` 代表最终矩阵”的语义，改为纯函数
   `getProjection(float effectiveAspect)`（或等价的明确输入结构）。`effectiveAspect` 由 View
   producer 从该 View 的 output extent 计算；CameraComponent 不读 Window、Swapchain、RenderService
   或全局 viewport。`_fixedAspectRatio` 只决定是否采用组件保存的 aspect。
3. 正交模式固定一个可读的尺度定义：垂直半尺寸/垂直视野高度；水平范围由
   `verticalExtent * effectiveAspect` 推导。near/far 与 camera 朝向的约定在 P0/P2 测试里锁定。
4. CameraComponent 不再通过 `getFreeView()` / `getOrbitView()` 负责 View。现有
   `getOrbitView()` 会修改 owner Transform，这是禁止的隐式副作用；迁移到明确的
   `SceneCameraQuery`/producer/controller，CameraComponent 只提供 projection data。
5. 同步 camera inspector、Lua binding、scene serialization、RuntimeGameViewProducer、
   EditorViewProducer。所有调用点必须明确传入 output aspect；不保留旧无参接口。
6. Editor 的“正交 XY profile”只修改普通 CameraComponent/编辑器相机与 gizmo 约束，不持有第二套
   camera state；World2D 的坐标约定不能复用 GUI 的 Y-down 逻辑像素。
7. Billboard 的世界边长是 `minWorldScale`，不按相机距离或 FOV 放大。透视下远处看起来更小，
   那是投影，不是把 quad 改大。`screenSizePixels` 不再参与绘制和拾取（字段保留，旧场景能加载）。
   `worldDirection` 写入 push constant 但顶点展开未使用，billboard 永远正对相机；“可侧看的牌”
   属于 Sprite2DComponent。

### 参考与校验

- ECS/Systems/Components/CameraComponent.h/.cpp
- Core/include/Core/Camera/Camera.h（已有正交实现）
- Core/include/Core/Math/Math.h
- Applications/GameRuntime/Render/RuntimeGameViewProducer.cpp
- Applications/GameEditor/EditorViewProducer.cpp
- Applications/GameEditor/Interaction/EditorLayer.Interaction.cpp
- 测试 perspective 旧场景矩阵不变、orthographic aspect 变化正确、相同 Transform 的 view 不变、
  投影计算无 ECS/Window 读操作、`getOrbitView()` 不会修改 Transform。
- xmake b ya-ecs-systems ya-scene-3d ya-game-runtime ya-game-editor ya-testing
- xmake r ya-testing --gtest_filter='*Camera*:*EditorView*:*SceneSerializer*'

## 6. Phase 3：Sprite authored 数据与资源边界

### 目标

让 2D 对象进入现有 Scene hierarchy，可保存/复制/选取；GPU 解析留在 extraction/render 层。

### 目标文件与字段

默认放在现有 authored render component 所在的 Render3D component domain：
Engine/Source/Framework/Render/Render3D/include/ECS/Component/2D/Sprite2DComponent.h，
而不是 ya-ecs-systems。P0 的依赖审计可以把它迁到更中性的 Scene/RenderScene module，
但不得让 gameplay systems 反向携带 renderer/GPU 语义。字段只包含 TextureSlot/asset reference、
local size、UV rect/flip、tint/visible、layer/sort order，以及可选的 authored picking 标识。
组件不保存 FRenderFeatureMask；Game/Gizmo/Debug 是 View policy，不是 Sprite authoring 数据。

禁止：MaterialFactory、GPU descriptor、command buffer、TransformComponent 的位置/旋转/缩放副本、
BillboardComponent 继承、组件内资源 resolve、组件内修改 Render2D global session。

### 工作项与校验

1. 增加 reflection、serialization、必要的 script binding；TextureSlot 只保存 authoring reference。
2. 按 P0 冻结的资源责任，由唯一 pre-extraction producer 处理 resolve、pending、placeholder、
   resourceVersion；组件和 RenderFrameExtractor 不直接访问 AssetManager、MaterialFactory 或创建 GPU descriptor。
3. extraction 只消费已经可读的 resolved binding，生成不可变 WorldSpriteCandidate；pending/failed
   的显示规则（跳过或统一 missing-texture placeholder）只允许有一个实现，并写进测试。
4. 冻结坐标与混合语义：先定义 World2D 平面的 up/forward、sprite 朝向、Transform.z、深度测试/写入、
   sprite-sprite 顺序、与 3D opaque/transparent 遮挡的关系，再定排序键。不得同时声称“z 是世界深度”
   又无条件禁用 depth write，或用 painter order 假装解决与 3D 几何的遮挡。验收覆盖 P0 matrix 中的
   混合场景；UI 左上/Y-down 逻辑像素不得传入 TransformComponent。
5. 在 Scene create/remove/clone/serialize 路径补行为测试；editor companion 继续遵守
   ManagedChildComponent/CompanionSpec，不能把生成图标伪装成 authored sprite。
6. picking 先做 editor 侧 CPU quad hit-test（按 View inverse、sprite bounds、排序回退），不要为了
   一个 2D 选择功能立即扩展 EntityId GPU pass；GPU picking 作为明确后续需求。
7. Scene round-trip/clone/destroy、pending/failed/placeholder、asset hot reload 测试通过，且组件
   不含任何 GPU runtime pointer。
8. rg 检查公开头不 include MaterialFactory、ICommandBuffer、Render2D、Transform2D、Camera2D；
   也不保存 Render3D 的 view feature mask。

## 7. Phase 4：共享 Scene snapshot 与 World2D workload

### 目标

同一个 Scene/revision 只 extraction 一次；每个 View 只拥有 camera 相关 cull/order。隐藏 View 不提交
World2D work。Scene snapshot 共享不意味着所有 View 必须执行同一批 render stages；纯 2D、纯 3D、
混合 View 的工作集合必须在 View declaration / prepared workload 中明确表达，并保持现有 Scene snapshot
去重语义。

### 工作项

1. 扩展 SceneSnapshot，增加只读 world sprite candidates；candidate 中只保留 extraction 后可消费的
   immutable transform/UV/tint/order/resolved-resource binding，资源引用至少活到 command submit/fence 完成。
   场景 sprite 是 retained candidate，不是即时 draw list：不经 `ScreenDrawList` / `WorldDrawList`，
   也不新增通用 World2DList。默认形状是一个 quad + per-instance buffer 的 instanced draw；是否复用
   ya-render-2d 上传机制由 P0 按生命周期与数量决定，不预设。
2. RenderFrameExtractor::extractSceneSnapshot() 一次遍历 Sprite2DComponent；不能按 View 重复遍历 ECS，
   也不能让每个 View 自己 resolve asset。P0 先核对 TextureSlot / AssetManager / ResourceResolveSystem
   的现有写入和版本责任，再选唯一的 pre-extraction resolved-binding producer；不把“所有 sprite 资源
   必须由 ResourceResolveSystem 负责”当未经审计的前提。Scene snapshot 的 dedupe key 仍是
   (Scene, sceneRevision)，异步资源变化在同 tick 的明确阶段反映；不做跨 tick snapshot cache，除非
   resource resultVersion 也进入缓存有效性条件。
3. RenderFrameData 增加 World2DViewData 或等价 view-owned bucket，只保存该 View 的 visible/order
   index，不复制 shared sprite vector。View 的 cull 和 sort 不能回写 SceneSnapshot。
4. 不增加 renderer identity mask。P0 需确定是否需要显式 SceneViewWorkload（例如 World3D、
   World2D）；不能从 candidate 列表为空推断不需要某 stage，因为空的 3D Scene 仍可能需要
   clear、skybox 或 postprocess。若需要该声明，它只表达 View 请求哪些内容，不进入 Scene family
   identity，且不与 Game/Gizmo/Debug 的 FRenderFeatureMask 混用。
5. SceneRenderScheduler 的 Scene snapshot 去重不因 World2D 改变；但只有确实共享 pipeline
   configuration、resource preparation 与 graph scheduling policy 的 View 才放在同一 rendering
   family。不能为了合并 family 而让纯 2D View 带上 3D attachments。
6. 按 P0 选择的 runtime graph 组织 World2D raster：可以是 Forward/Deferred 中显式可选的 stage，
   也可以是轻量 2D-only graph。两种路径都消费 immutable WorldSprite candidate，不依赖
   UIFrameSnapshot/WidgetTree；不能复制 Scene scheduler、frame loop 或应用级 surface/present 编排。
7. RuntimeRenderContext 只负责应用层顺序和 UI compose；RenderDeviceState 继续提供
   prepare/begin/record selected Scene workload/end/seal 机制，不新增一个 World3D/World2D 双入口 coordinator。

### 参考与校验

- Render3D/RenderFrameData.h
- GameRuntime/Render/RenderFrameExtractor.cpp、HostSceneExtract.cpp
- Render3D/Common/SceneRenderScheduler.h
- Render3D/RenderDeviceState.Frame.cpp
- GameRuntime/Render/RuntimeRenderContext.cpp
- SceneViewDesc.h
- 两个 View 同 Scene/revision：同 tick 共用同一个 immutable Scene snapshot，World2D candidate
  extraction 只有一次；两个 View 的 cull/output 独立。identity/count 只能作辅助证据，核心验收应验证
  不同视角的可见结果正确，避免把测试退化成对实现细节的单侧计数断言。
- 隐藏 View：无本 tick View request、无新 target allocation、无该 View 的 sprite pass；其他 View 仍可
  消费同一 Scene snapshot，registered target allocation 按 ViewTargetStore 生命周期保留。
- 纯 2D View 不分配/运行无用 GBuffer、lighting、shadow stages；纯 3D View 不运行 sprite workload；
  mixed View 的两类内容按 P0 冻结的遮挡/颜色规则合成。验收可以支持轻量 2D-only graph，但不得
  新增第二套 Scene scheduler 或应用 loop。
- graph topology 与可见输出共同验证 sprite pass 的 target load/store、depth-test/write、blend、与
  3D opaque/transparent、bloom 及 finalize/tone-map 的顺序。P0 matrix 必须给出各场景的预期遮挡和颜色
  结果；测试不能只断言 pass 被调用/跳过。UI compose 不复用 World2D scene pass。
- xmake r ya-testing --gtest_filter='*SceneRenderScheduler*:*RenderView*:*World2D*:*Snapshot*'

## 8. Phase 5：Runtime 与 Editor 的 2D View producer

### Runtime

修改 RuntimeGameViewProducer：游戏主 View 默认可声明 World3D + World2D；没有 sprite candidates 时
World2D graph pass 不创建。正交 view/projection 仍来自普通 CameraComponent 的纯投影 API；UI
不通过 SceneViewProducer 声明，而由 GameUIHost/UI compose 提交。View producer 负责计算自己的
effective aspect 和 camera view，不读取已发布的上一帧 View output。

### GameEditor

修改 EditorViewProducer、EditorLayer 和 viewport tool：

- 3D authoring 与 2D authoring 是同一个 Scene/View 模型的两个 camera/tool profile，不是两个 scene，
  也不是 UI Designer canvas。
- 2D profile 只设置正交 XY、XY gizmo、sprite/plane picking；不创建 Node2D/Transform2D/Camera2D。
- UI Designer canvas 继续由 EditorUIDesignerSession 的独立 WidgetTree 提供；EViewportMode::Mode2D
  不能同时表达 UI canvas 和 World2D。
- editor grid/gizmo/selection 在 World2D/World3D 输出之后作为 editor overlay compose，不能进入
  authored Sprite snapshot；它们也不能偷偷进入 runtime Game UI snapshot。overlay 由 D2 的
  GameEditor View overlay pass 承载（世界相位 → 屏幕相位），2D profile 只换相机与 gizmo 约束。
- World2D authoring 的隐藏/显示由 View producer 的声明控制；隐藏后不提交该 View 的 scene request、
  cull/order 或 graph pass。若同一 Scene 仍被其他可见 View 请求，共享 Scene extraction 继续服务它们。
  视口从 tab 中移除时，不用在 pipeline 里“录空 pass”补齐。

校验：Hierarchy 中 sprite 与普通 Node3D 并列，可选取/移动/保存/撤销；UI Designer 不接 game input；
World2D、Game UI、gizmo 可独立关闭；切 tab 隐藏 viewport 后不再提交 scene render task；2D hit-test
与 3D ray-pick 不共享错误的 viewport mode 分支。

## 9. Phase 6：Game UI 闭环（沿用 UIDocument，不引入 UMG）

1. SceneWidgetEntry 继续只保存 .yaui document reference + mount intent，不放 UI ECS node。
2. GameUIHost 只做 presentation adapter：输入、logical extent、WidgetTree tick、snapshot；mount
   lifecycle 与 entry traversal 继续由 game-ui-authoring 计划负责。本计划不重复实现 Scene UI
   composition service。
3. runtime UI 与 designer preview 使用不同 WidgetTree 和明确 clock/input policy：runtime 接游戏输入，
   designer 默认不接 game input，只接受 editor manipulation；动画 preview 必须显式开启 preview clock。
4. UIFrameSnapshot 在 GameRuntime graph build 前冻结；World3D/World2D recording 不访问 live
   WidgetTree，也不把 UI snapshot 放进 Render3D 的 FramePacket。
5. UI placement 分成四种，不得用一个 compose helper 混为一谈：
   - Game UI onto View display image：world graph finalize 之后，display compose 之前；
   - Editor viewport chrome/overlay onto editor surface：由 GameEditor 的 surface compose 负责；
   - UI Designer canvas onto GUI-owned offscreen surface：GUI framework/Editor session 负责；
   - standalone GUI surface：GUI host 自己负责。
   这些 target 的 load/store/layout、logical extent 和 input policy 各自明确。
6. UI pipeline 不进 World2D SceneColor pass，不参与 lighting/GBuffer/bloom；Game UI 的具体是否在
   tone map 之后沿用现有 display-image 语义，必须在 P0 记录而不是从函数名推断。
7. 文档浏览、打开、保存、scene mount 继续执行 game-ui-authoring 计划的 Phase 4/5；本计划只规定
   World2D 与 UI compose 的交接边界。

参考：Applications/GameRuntime/GUI/GameUI/GameUIHost.cpp、Applications/GameRuntime/Render/RuntimeRenderContext.cpp、
Applications/GameEditor/EditorUIDesignerSession.cpp、.agent/plan/game-ui-authoring/plan.md。

校验：xmake r ya-testing --gtest_filter='*GameUIHost*:*EditorUIDesigner*:*UIDocument*'；必须有一个真正能
证明 snapshot 在 graph build 前冻结、recording 不访问 WidgetTree 的生命周期/行为测试，不能只数调用次数。

## 10. Phase 7：删除旧路径、性能与资源门禁

删除条件：

- 旧 screen/world 混合 shader interface 与 textureRef flags 删除，不保留含糊兼容入口。shader module
  是否共用以 P0 capability matrix 为准；不得因新 shader 名称出现就默认旧路径已迁完。
- UI/World2D 不再共享 textureRef 高位采样 flag、共享未命名的 global session state 或隐式 layout。
- BillboardComponent 只保留 billboard/editor companion 语义。
- 全局 `Render2D`、pass slot、`FRender2dContext`、`EditorViewportCompose` compose kind 已由 D1/D2 删除
  （P7 只做回归审计）。
- 不新增 IRenderRuntimeServices、中心 UI bus、EditorPanel、第二个 Scene tree 或第二个 app loop。
- EViewportMode::Mode2D 不再同时表示 UI canvas；旧分支迁到明确 profile/preview 后删除。

性能/正确性门禁：

- 同 Scene 多 View extraction 一次，view order/cull 只做必要计算。
- hidden View 不创建当帧 request；registered target 仍由 ViewTargetStore 管理。
- UI 与 World2D 的 flight buffer/cursor 是否分开，以 P0 生命周期矩阵为准；必须保证不同 pass
  的 GPU 数据不会在同一提交中被后续 flush 覆写，不能只靠“调用顺序”假设安全。
- command recording 引用的 texture/view/descriptor/vertex buffer 至少活到 submit/fence 完成。
- pipeline/target replacement 只在 safe point，不在录制中重建。
- World2D graph contract 由 P0 的混合规则决定：target、load/store、depth-test/write、blend、bloom
  和 tone-map 行为必须与可见遮挡规则一致。纯 2D workload 不得无故创建 GBuffer/lighting/shadow；
  但不能为了避免 bloom 就机械固定在某个 pass 名之前/之后。最终语义用 typed graph data 表达，
  不能靠 shader 隐式 bit 或临时 render mode patch。

全量校验：

    xmake ya-shader
    xmake b ya-foundation-core ya-ecs-core ya-ecs-systems ya-scene-core ya-scene-3d \
      ya-render-2d ya-gui-widgets ya-gui-compose ya-render-3d ya-game-runtime ya-game-editor ya-testing
    xmake r ya-testing --gtest_filter='*Camera*:*Sprite2D*:*SceneRenderScheduler*:*World2D*:*GameUI*:*EditorView*'
    python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=90
    python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=120

## 11. Checkpoint 与停止条件

| checkpoint | 单一可验收目标 | 建议提交标题 |
| --- | --- | --- |
| P0 | 完成调用方/owner/时序审计；验证已落地的 Render3D→GUI 边界；冻结混合、target、坐标、资源契约 | [plan/render] freeze 2d and ui contracts |
| P1 | 已完成：screen/world shader 与 vertex contract 分离、textureRef typed 化、保留单一低层 batch owner；见 progress.md parity/test 记录 | [render-2d] separate ui and world draw contracts |
| D1 | draw list 按坐标系拆分；删除全局 Render2D / pass slot / FRender2dContext；Pipelines 与 Recorder 按 §2.4 持有 | [render-2d] split draw lists by coordinate frame |
| D2 | 编辑器 View overlay 由 GameEditor 拥有（世界相位 → 屏幕相位）；GUI Compose 不再持相机/深度/scene color | [editor/viewport] own the view overlay pass |
| D3 | 屏幕 stroke / path（任意三角形 + 几何羽化），GUI 线段与 gizmo 轴线迁移 | [render-2d] add screen-space stroke |
| P2 | CameraComponent 只负责纯 projection，正交模式可序列化并被 runtime/editor 使用；billboard 边长是世界尺寸，不随相机距离缩放 | [scene/camera] add explicit projection input |
| P3 | Sprite2DComponent 可创建、保存、复制、删除，无 GPU 状态 | [ecs/sprite2d] add authored sprite component |
| P4 | 同 Scene 多 View 共享 snapshot；按 P0 选择的 Scene graph/workload 录制 World2D，纯 2D 不承担无用 3D stages | [render/world2d] add shared extraction and sprite pass |
| P5 | runtime 2D view 与 editor 2D authoring profile 可用 | [editor/world2d] add orthographic authoring flow |
| P6 | Game UI runtime/designer/preview 的 tree、clock、input 边界闭环 | [gui/game-ui] close runtime and designer loop |
| P7 | 旧 mixed path 删除，资源生命周期和性能门禁通过 | [render] remove mixed sprite path |

建议顺序：P2 → D1 → D2 → P3 → D3 → P4/P5 → P6/P7。D1/D2 可提前到 P2 之前，二者与 P2 无依赖。
不在范围：分屏产品层；多窗口合并提交（AB4-2d，等撕出视口需要第二扇窗画世界时再做）。

停止条件：若设计要求新增第二个 Scene scheduler/app loop、跨 UI/Scene 的中心 bus、录制期 live 查询，
或恢复 UI/World2D 共用 bit-packed shader 协议，停止当前 phase 并更新架构评审。同样停止：
恢复全局 2D 绘制单例、让一个 draw list 同时收像素与世界坐标、GUI 模块重新接收相机/深度。纯 2D 为避免无用
3D stages 而采用独立 graph/workload，本身不违规；但必须复用既有 Scene declaration、snapshot、
target/submission 生命周期与应用主时序，不能顺手长成另一套 renderer owner。

## 12. 参考结论

- Unity 的 SpriteRenderer + orthographic camera 证明 2D 对象进入同一 Scene 的工作流可行；本项目
  吸收工作流但不复制 Transform2D 类型。
- Godot 的 Node2D/Camera2D 说明 2D authoring 需要正交相机、拾取和 gizmo profile；本项目把这些
  收敛到已有 Node3D/TransformComponent/CameraComponent。
- Unreal 的 UMG 与 world rendering 分离说明 Game UI 应是独立 WidgetTree/snapshot；本项目采用同一
  边界，但不造 UMG 平行控件族。

最终验收不是“像哪个引擎”，而是读者可以从 RuntimeRenderContext::tick/record 一眼读出：
声明 View → 共享 Scene snapshot → 按 workload 选择的 Scene graph（World3D/World2D/mixed）
→ Game UI compose → display compose → present；GUI framework 可以在没有 Scene/ECS 的情况下单独运行。
