# Scene 2D World 与 Game UI 闭环计划

## 0. 计划状态与架构结论

- 状态：planned。本轮只建立计划，不改运行时代码。
- Game UI compose 与 Scene runtime rendering 必须分开：WidgetTree/UIFrameSnapshot 只进入 GUI
  compose；authored sprites 属于 Scene rendering。不要让 GUI framework 变成 World2D renderer，
  也不要为 UI 复制 RHI device、RenderGraph 或应用主循环。
- World2D 是现有 Scene runtime pipeline 中的一种 draw workload，不是第二个
  ISceneViewFamilyRenderer，不新增 active pipeline、family registry 或第二条 frame loop。
- Render3D 不依赖 ya-gui-compose。现状里 FramePacket 暴露 UIFrameSnapshot，且 Render3D 的
  ViewCompose 调 GUI compose；先在现有 GameRuntime 编排点收回这条依赖。
- 不新增 Transform2D、Camera2D、Node2D 或第二套 Scene 树。
- authored 2D 对象使用现有 Node3D + TransformComponent；XY 是平面，vec3.z 是前后/排序层次。
- 相机仍使用 CameraComponent + owner 的 TransformComponent；正交只是 projection mode。
  具体投影矩阵由 view owner 提供有效 aspect，CameraComponent 不读取 Window/View 全局状态。
- Game UI 仍是 UIDocument + WidgetTree + UIFrameSnapshot；不挂回 ECS hierarchy。

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
      → ya-gui-compose / recordCameraViewCompose

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
| Render2D | 当前低层屏幕/世界 2D draw 机制；具体复用边界经 P0 调用方审计后决定 | Scene/ECS 遍历、UI document |
| Render3D | View target、RenderGraph、当前 Forward/Deferred family recording；集成 Scene sprite workload | GUI compose、读取 live WidgetTree |
| GUI Widgets | WidgetTree、UIDocument、UIFrameSnapshot、输入/时间策略 | Scene/ECS、Render3D |
| GUI Compose | UIFrameSnapshot 到 UI pipeline 的 replay | Scene/ECS、World2D extraction |
| GameRuntime | 一帧的 declaration、extraction 和 World3D→World2D→UI 顺序 | 把产品顺序隐藏在 Framework |
| GameEditor | 2D/3D authoring camera profile、UI preview、editor overlays | 改写 GUI pipeline |

## 2. 当前真实链路与目标插入点

当前代码可证实的录制顺序是：

    prepare → active Forward/Deferred family graph → Game UI view compose
      → IFrameRecordExtensions::recordViewCompose → display compose → present

SceneViewFamilyPlan 按 Scene/revision/policy 分组；RenderDeviceState::recordViewFamilies
把每个 family 交给单个 active ISceneViewFamilyRenderer。Forward/Deferred 各自构建完整
family graph 并发布 View output。现在没有 World2D recording step，因此不能把目标顺序写成
已经成立的「World3D → World2D → UI」。

目标是在现有 active Forward/Deferred family graph 内加入 sprite workload；不增加第三个
top-level renderer。Game UI 仍在 world graph 之后写入 View 的 display image；display compose
最后才写 surface/swapchain。World2D 的 graph 插入点必须在 P0 冻结它与 3D opaque、3D
transparent、depth 和 postprocess 的视觉关系之后，不能借用 GUI 的 view-compose 扩展点。

### 2.1 三条 pipeline 的语义

1. Scene runtime：现有 Forward 或 Deferred renderer 是单个 View family 的顶层 executor。
   World2D sprite workload 集成到这一执行链，不新增第二个 renderer/family selector。
2. GUI Compose：只消费不可变 UIFrameSnapshot；screen-space、Y-down、clip、文字和圆角属于
   GUI compose。它不遍历 Scene/ECS，也不参加 Scene snapshot extraction。
3. 最小 shader/pipeline 数量由 P0 的调用方与状态矩阵决定。不得预先规定 GUIPrimitive、
   GUIImage、GUIText 三套 pipeline，也不以拆分为目标制造 QuadResourcePool、多个 batch owner
   或策略层；只拆已经证明有不兼容数据/状态或资源生命周期的路径。

共用 RHI/RenderGraph/command buffer 等底层能力不等于共用 shader 状态、draw item、排序或
生命周期。反过来，概念分开也不自动要求复制一套完整 quad batching 实现。

### 2.2 空间与生命周期

- World2D 使用 Scene 世界坐标；UI 使用 Render2D 左上角/Y-down 逻辑像素，二者不混用。
  P0 必须冻结 World2D 的 up/forward、sprite 朝向、z/depth/sort 关系；不能把 GUI 的 Y-down
  坐标直接扩散进 TransformComponent。
- View 的 outputRect 是 View 自己声明的 offscreen rect；窗口尺寸只属于 presentation。
- View 不存在于本 tick 时不提交 task；已注册 target 的销毁仍由 ViewTargetStore::unregisterView
  管理，不能用“连续 N 帧没看到”猜 GC。
- Scene snapshot 由 Scene+revision 共享；camera、cull、排序、view output 只属于 View。
- Scene revision 不代表异步资产状态版本。sprite 的 authored asset reference 留在组件；每 tick
  的抽取在同一 Scene 遍历中读取当前 resource version/state，输出不可变、保活到 submit/fence
  完成的 render candidate。Pending/failed/placeholder 和热重载行为必须在 P3/P4 定义并测试。

## 2.3 Architecture Review：2026-09-26

本次 review 发现原计划有几处会把复杂度重新引回来的假设，已按以下结论修正：

| 发现 | 原计划风险 | 修正后的执行口径 |
| --- | --- | --- |
| Render3D 当前 include GUI/Compose、FramePacket 携带 UIFrameSnapshot | Framework 方向反转；纯 runtime renderer 被 GUI 类型锁死 | P0 先把 UI compose 调用收回 GameRuntime/Editor；Render3D 只发布 View output |
| 把 World2D 写成第三个 World2DRenderPipeline | active pipeline/family selector 变成三套，recording 主链再次分叉 | World2D 是 Forward/Deferred graph 的一个 typed pass/workload |
| 在 SceneViewDesc 增加 family mask | View 逐渐变成 renderer policy flags 垃圾桶 | 不加 family mask；确有需要时只加小型 SceneViewContents，不表达 renderer identity |
| 预先规定 GUIPrimitive/GUIImage/GUIText/WorldSprite 四套 shader | 通过“拆文件”制造 PSO、descriptor、batch 和缓存复杂度 | 先做 capability matrix，只拆真正不兼容的 shader/pipeline |
| World2D 直接写“World3D 之后、postprocess/bloom 之前” | 当前 bloom graph 从 SceneColor 读取，实际会把 sprite 带入 bloom | MVP pass 放在 bloom graph 之后、finalize/tone-map 之前；需在 graph topology 中验证 |
| CameraComponent 读取 outputRect 或继续提供隐式最终 projection/view | 组件依赖窗口/View，或 getOrbitView 修改 Transform | producer 传 effective aspect；CameraComponent 只生成 projection，view 由 producer/controller 生成 |
| Sprite2DComponent 放在 ECS/Systems | gameplay systems 被迫依赖 renderer/asset 语义 | 默认沿用现有 Render3D authored component domain，P0 再确认是否抽到中性 Scene/RenderScene |
| 只写“texture asset reference” | 执行时容易在 component/extractor 内偷偷 resolve GPU 资源 | ResourceResolveSystem 负责 resolve/version；snapshot 只消费 resolved binding，pending 行为单一化 |
| 只提到 entity-id/picking | 2D editor 可能先实现 GPU picking，增加 pass 和同步 | MVP 先用 CPU quad hit-test；GPU picking 单独决策 |
| Game UI 与 editor UI 的 target 没分开 | compose helper 继续吞掉 View/surface/display 三种语义 | 明确 View display、editor surface、designer offscreen、standalone surface 四类 target |
| 新计划重复 game-ui-authoring 的 mount/designer phase | 两条计划会同时改 GameUIHost/Scene UI composition | game-ui-authoring 负责 UIDocument/mount/designer；本计划只定义交接契约 |
| Render2D 仍在 QuadRender.cpp 里直接判断 Vulkan，并依赖 Backend TextureLibrary | UI/World2D 拆分后把后端耦合复制到多个 batch；OpenGL 路径更难维护 | P0 把 viewport convention、sampler lookup、backend resource access 收敛到 RHI/resource seam；World2D 不得再复制 Vulkan 分支 |
| Render2D 当前有 process-global session/cursor | 多窗口或并行录制时，后一次 begin/flush 可能覆盖前一次状态 | P0 明确 pass-local mutable state；global 只保留 immutable/device-owned resources，或明确禁止并行录制并加断言 |
| SceneRenderPlan::displayRootTask() 取第一个 bDisplayRoot | 多 OS window 下一个全局 display root 不能表达多个 surface 的输出归属 | 本计划不再扩展 root bool；多窗口先由 surface-scoped View output/presentation 计划解决，2D feature 依赖其契约 |

这些修正不是可选优化，而是后续 checkpoint 的前置条件。若实施中再次出现“新增一个总 pipeline、
一个中心 registry、一个跨域 service 或一个 shader bit 就能接上”的建议，应先退回本节重新审查。

## 3. Phase 0：审计并冻结契约

### 目标

先把 UI screen、world billboard、editor overlay、UI canvas preview、未来 World2D 分开命名；
没有完成本 phase 不新增 Sprite2DComponent 或新 shader。这个 phase 还必须修正当前
Framework 依赖方向：Render3D 不能以 `UIFrameSnapshot` / `GUI/Compose` 作为公开或实现依赖。

### 工作项与参考

1. 审计 Render2D::begin/end、FRender2dContext、FRender2dSession、FQuadRender::PassPipelines。
   参考：Render/Render2D/include/Render2D/Render2D.h、QuadRender.h、Render2D.cpp、QuadRender.cpp。
2. 列出 makeSprite、makeWorldSprite、makeText、makeRectFilledMultiColor、drawRoundedRect、
   makeWorldLine 的全部调用方，按 UI/World2D/EditorOverlay 归类。
3. 审计 Render2DPassSlot owner；确认 slot 只表达资源隔离，不表达 runtime/editor/UI 语义。
4. 审计 SceneSnapshot 多 View 共享边界，记录 extraction 与排序是否重复。
5. 审计 EditorViewportCompositor、EditorGameUIPreview、recordCameraViewCompose 的顺序，
   明确 UI Designer canvas 与 World2D authoring 不是同一模式。
6. 审计 `Render3D/Common/ViewCompose.*`、`Render3D/Common/RenderFrameInputs.h`、
   `Render3D/xmake.lua`：把 Game UI compose 从 Render3D 收回 GameRuntime/GUI integration；
   `makeViewDisplayInsetRect` 这种 editor layout helper 移出 Render3D。目标是 `ya-render-3d`
   不 include `GUI/Compose`、不暴露 `UIFrameSnapshot`。
7. 审计 Forward/Deferred graph 的真实阶段：opaque、skybox、transparent、entity-id、
   bloom、finalize。记录 World2D 的唯一插入点和 target layout，不先创建 `World2DRenderPipeline`。
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

P0 还必须标明 Render2D 是否允许同一提交中并行/嵌套 begin；若不允许，代码必须在入口断言，
而不是让 global session 的限制成为未记录的约定。

## 4. Phase 1：先收回 GUI/Render3D 边界，再按证据拆 2D draw path

### 目标

先消除 Render3D 对 GUI compose 的反向依赖，再消除 UI 和运行时场景共同使用 Sprite2D.slang、
world/screen 字段和 `textureRef` 高位 flag 的情况，同时保持现有 GUI 像素基线。

### 目标文件与实现

1. 第一件事不是拆四个 shader，而是移除 `Render3D/Common/ViewCompose` 对 `GUI/Compose` 的依赖。
   GameRuntime 在自己的 record 顺序中显式调用 GUI compose；Render3D 只发布 View output。
   `FramePacket` 不再携带 `UIFrameSnapshot`，GUI snapshot 归应用侧的 GameUI record packet。
2. 建立 capability matrix：primitive、image、text/SDF、world sprite、opaque/alpha、clip、
   transform、depth、blend、resource binding。只有当两类 draw 在这些维度上不兼容时才拆 shader
   或 pipeline；禁止把 `GUIPrimitive/GUIImage/GUIText/WorldSprite` 四个名字当作预先批准的设计。
3. 无论拆不拆 shader，都必须删除 `textureRef` 高位 bit 同时表达 SDF、opaque、world/screen
   的隐式协议。采样模式、alpha mode、坐标空间和资源绑定改成 draw/pipeline 的 typed data。
4. 先保持一个可读的低层 batch owner；只有 UI 与 World2D 需要不同的 cursor、descriptor、
   resource-retain 或 flush 生命周期时，才拆成两个实现。不得为了“看起来分层”新增
   `QuadResourcePool + GUIQuadBatch + WorldSpriteBatch` 三层空壳。
5. Render2D session 的所有权改成调用方明确传入的 pass/session；不得依赖 process-global
   pending kind 或跨窗口隐式状态。
6. 目标顺序由 GameRuntime 的 record 函数明确写出，但 World2D 的实际 graph pass 必须由
   active Forward/Deferred pipeline append；不能通过 GUI compose 或 `recordViewCompose` 偷塞。

### 参考与校验

- 参考：Engine/Shader/Slang/Sprite2D.slang、Render/Render2D/QuadRender.h/.cpp、
  GUI/Runtime/Compose/Render2DComposePass.cpp、GameRuntime/Render/RuntimeRenderContext.cpp、
  Render3D/Common/ViewCompose.cpp、Render3D/Common/RenderFrameInputs.h、
  Engine/Shader/Shader.xmake.lua。
- xmake ya-shader
- xmake b ya-render-2d ya-gui-compose ya-render-3d ya-game-runtime ya-game-editor
- xmake r ya-gui-closure-test
- 运行 Script/automation/gui/run_workbench_gpu_parity.py 和 HelloMaterial 90 帧冒烟。
- rg 检查 Render3D 无 GUI/Compose 或 UIFrameSnapshot 依赖；UI/world draw 的坐标空间、采样模式、
  alpha mode 不再由高位 bit 隐式编码。
- 未经 capability matrix 证明，不接受“创建四套 shader/pipeline”作为 checkpoint 完成条件。
- UI Workbench、EditorSurface、Game UI compose 截图与迁移前一致；只允许 label 变化。

## 5. Phase 2：CameraComponent 增加正交模式

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
2. ResourceResolveSystem/资产链负责 resolve、pending、placeholder、resourceVersion；组件和
   RenderFrameExtractor 不直接访问 AssetManager、MaterialFactory 或创建 GPU descriptor。
3. extraction 只消费已经可读的 resolved binding，生成不可变 WorldSpriteCandidate；pending/failed
   的显示规则（跳过或统一 missing-texture placeholder）只允许有一个实现，并写进测试。
4. 明确坐标和排序：World2D 默认 XY 平面、Y-up、camera 沿 -Z 看，transform 的 z 是世界深度输入；
   MVP 使用 (layer, sortOrder, worldZ, entityId) 的稳定 painter order，禁用深度写入。不要把 UI 的
   左上/Y-down 逻辑像素语义传入 TransformComponent。
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
World2D work。World2D 不创建独立的 SceneViewFamilyRenderer，也不改变现有 family key 的含义。

### 工作项

1. 扩展 SceneSnapshot，增加只读 world sprite candidates；candidate 中只保留 extraction 后可消费的
   immutable transform/UV/tint/order/resource binding，资源引用至少活到 command submit/fence 完成。
2. RenderFrameExtractor::extractSceneSnapshot() 一次遍历 Sprite2DComponent；不能按 View 重复遍历 ECS，
   也不能让每个 View 自己 resolve asset。Scene snapshot 的 dedupe key 仍是 (Scene, sceneRevision)；
   不做跨 tick 的 snapshot cache，除非额外把资源 resultVersion 纳入 key。
3. RenderFrameData 增加 World2DViewData 或等价 view-owned bucket，只保存该 View 的 visible/order
   index，不复制 shared sprite vector。View 的 cull 和 sort 不能回写 SceneSnapshot。
4. 不增加 EViewRenderFamilyMask。若确实需要关闭某个内容域，增加小而明确的
   SceneViewContents（World3D/World2D 两个内容选择），它不是 renderer identity、不是 family key、
   也不和 Game/Gizmo/Debug 的 FRenderFeatureMask 混用。若空 candidates 已经足够，优先不加字段。
5. SceneRenderScheduler/family key 继续按 (Scene, revision, policy) 分组。一个 family 内的每个 View
   都由当前 Forward/Deferred pipeline 消费自己的 3D 与 World2D view data；不能让一个 family 再拆成
   两个 active renderer。
6. 在 Render3D 的公共或私有 graph helper 中新增 World2D sprite pass builder，由 Forward 和 Deferred
   在各自的唯一 graph 编排点调用。不要新增 World2DRenderPipeline、第二个 active pipeline 或
   World2D family registry。该 pass 只依赖 RenderFrameData/WorldSprite candidate 和低层 2D draw
   mechanism，不依赖 UIFrameSnapshot/WidgetTree。
7. RuntimeRenderContext 只负责应用层顺序和 UI compose；RenderDeviceState 继续只提供
   prepare/begin/record active family/end/seal 机制，不新增一个 World3D/World2D 双入口 coordinator。

### 参考与校验

- Render3D/RenderFrameData.h
- GameRuntime/Render/RenderFrameExtractor.cpp、HostSceneExtract.cpp
- Render3D/Common/SceneRenderScheduler.h
- Render3D/RenderDeviceState.Frame.cpp
- GameRuntime/Render/RuntimeRenderContext.cpp
- SceneViewDesc.h
- 两个 View 同 Scene/revision：shared snapshot 地址相同，sprite extraction 计数为 1；两个 View 的
  World2D order/cull 独立。
- 隐藏 View：无本 tick View request、无新 target、无 sprite pass。
- 纯 2D/纯 3D/Mixed View 的 active Forward/Deferred graph 均符合预期，没有第三种 top-level renderer。
- graph topology 明确验证：World2D 是 SceneColor 上的 unlit forward raster pass，load existing color、
  store color；MVP 在 bloom graph 之后、finalize/tone-map 之前；UI compose 不复用此 pass。
- MVP World2D 不写 GBuffer、不投 shadow、不参加 lighting；是否进入 bloom 必须通过 graph 顺序明确
  （默认不进入 bloom extract），不能靠“它是 unlit”猜测。
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
  authored Sprite snapshot；它们也不能偷偷进入 runtime Game UI snapshot。
- World2D authoring 的隐藏/显示由 View producer 的声明控制；没有可见 View 就没有 ECS extraction
  consumer 和 graph pass。视口从 tab 中移除时，不用在 pipeline 里“录空 pass”补齐。

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

- Sprite2D.slang 不再同时服务 UI/World2D；迁移后删除或改名，不保留含糊兼容入口。具体 shader
  数量以 P0 capability matrix 的最小结果为准。
- UI/World2D 不再共享 textureRef 高位采样 flag、共享未命名的 global session state 或隐式 layout。
- BillboardComponent 只保留 billboard/editor companion 语义。
- 不新增 IRenderRuntimeServices、中心 UI bus、EditorPanel、第二个 Scene tree 或第二个 app loop。
- EViewportMode::Mode2D 不再同时表示 UI canvas；旧分支迁到明确 profile/preview 后删除。

性能/正确性门禁：

- 同 Scene 多 View extraction 一次，view order/cull 只做必要计算。
- hidden View 不创建当帧 request；registered target 仍由 ViewTargetStore 管理。
- UI 与 World2D 的 flight buffer/cursor 是否分开，以 P0 生命周期矩阵为准；必须保证不同 pass
  的 GPU 数据不会在同一提交中被后续 flush 覆写，不能只靠“调用顺序”假设安全。
- command recording 引用的 texture/view/descriptor/vertex buffer 至少活到 submit/fence 完成。
- pipeline/target replacement 只在 safe point，不在录制中重建。
- World2D pass 的 graph contract 固定为：输入/输出同一线性 SceneColor，load existing color，store
  color，无 GBuffer attachment、无 depth write；默认追加在 bloom graph 之后、finalize/tone-map
  之前，以避免 sprite 被默认 bloom，同时保持与场景同一 tone-map。任何改变都必须新增明确的
  render mode，不在 shader 里加隐式 bit。

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
| P0 | 完成调用方/owner/时序审计，收回 Render3D→GUI 依赖，冻结 target/坐标/资源契约 | [plan/render] freeze 2d and ui contracts |
| P1 | 在不预设 shader 数量的前提下移除 mixed flag，并完成最小 draw-path 分离 | [render-2d] separate ui and world draw contracts |
| P2 | CameraComponent 只负责纯 projection，正交模式可序列化并被 runtime/editor 使用 | [scene/camera] add explicit projection input |
| P3 | Sprite2DComponent 可创建、保存、复制、删除，无 GPU 状态 | [ecs/sprite2d] add authored sprite component |
| P4 | 同 Scene 多 View 共享 snapshot，World2D 作为 active Forward/Deferred graph workload 录制 | [render/world2d] add shared extraction and sprite pass |
| P5 | runtime 2D view 与 editor 2D authoring profile 可用 | [editor/world2d] add orthographic authoring flow |
| P6 | Game UI runtime/designer/preview 的 tree、clock、input 边界闭环 | [gui/game-ui] close runtime and designer loop |
| P7 | 旧 mixed path 删除，资源生命周期和性能门禁通过 | [render] remove mixed sprite path |

停止条件：发现需要 Transform2D/Camera2D、第二个 Scene scheduler、第三个 active renderer、中心
bus、录制期 live 查询，或 UI/World2D 需要共享新的 bit-packed shader flag 时，停止当前 phase，先
更新架构评审，不用补丁继续推进。

## 12. 参考结论

- Unity 的 SpriteRenderer + orthographic camera 证明 2D 对象进入同一 Scene 的工作流可行；本项目
  吸收工作流但不复制 Transform2D 类型。
- Godot 的 Node2D/Camera2D 说明 2D authoring 需要正交相机、拾取和 gizmo profile；本项目把这些
  收敛到已有 Node3D/TransformComponent/CameraComponent。
- Unreal 的 UMG 与 world rendering 分离说明 Game UI 应是独立 WidgetTree/snapshot；本项目采用同一
  边界，但不造 UMG 平行控件族。

最终验收不是“像哪个引擎”，而是读者可以从 RuntimeRenderContext::tick/record 一眼读出：
声明 View → 共享 Scene snapshot → active Forward/Deferred graph（World3D + World2D workload）
→ Game UI compose → display compose → present；GUI framework 可以在没有 Scene/ECS 的情况下单独运行。
