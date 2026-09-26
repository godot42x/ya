# P0 Contract Matrix（2026-09-26 初版）

这不是实现清单，而是执行 P1 之前必须冻结的 owner/producer/consumer/lifetime 表。
表中“当前”描述仓库现状；“目标”描述本计划要求的边界。任何没有 owner 的字段、pass、
callback 或 global state 不得进入后续 phase。

| 事实 / 数据 | 当前 producer | 当前 consumer | 目标 owner | 生命周期 / 安全点 |
| --- | --- | --- | --- | --- |
| SceneViewDesc / View outputRect | RuntimeGameViewProducer、EditorViewProducer | Scheduler、RenderFrameExtractor、active renderer | 各 View producer | 当前 tick 声明；target 在 ViewTargetStore safe point 准备 |
| Scene snapshot | HostSceneExtract → RenderFrameExtractor | 每个 View 的 RenderFrameData / active renderer | GameRuntime extraction | 同 Scene+revision 一 tick 共享；不可变至 submit/fence |
| World sprite candidate（待实现） | ResourceResolveSystem 后的 extraction | Forward/Deferred 的 World2D pass | RenderFrameExtractor + active renderer | 不在 recording 期 resolve；Texture/Image/descriptor 至少活到 submit |
| Camera projection | CameraComponent 当前无参 getProjection | Runtime/Editor producer | CameraComponent 纯 projection function；producer 传 effective aspect | 不读 Window/Swapchain；View declaration 前生成 |
| Camera view | CameraComponent 当前 getFreeView/getOrbitView | Runtime/Editor producer | SceneCameraQuery / producer/controller | 只读 Transform；禁止 projection getter 修改 Transform |
| World2D target | 尚无 | 尚无 | active Forward/Deferred graph | MVP 复用线性 SceneColor：load/store；无 GBuffer、无 depth write；bloom 后、finalize 前 |
| Game UI snapshot | GameUIHost / EditorGameUIPreview | 当前 Render3D ViewCompose + GUI compose | GameRuntime / GameEditor compose owner | graph build 前冻结；recording 不访问 WidgetTree |
| UI target | Runtime View display image、editor surface、GUI offscreen 混合 | Render2DComposePass | 各应用/GUI host 按 target scope 持有 | target 明确 load/store/layout；不由 Render3D 猜 UI 语义 |
| Render2D session / batch cursor | 当前 Render2D global/session + pass slot | UI、editor overlay、world helpers | 明确的 pass owner | flight/submit 范围内保活；禁止后续 flush 覆写在提交中的数据 |
| Swapchain / presentation | Host acquire/present + SurfacePresentation | display compose | presentation layer | 不属于 View/Scene/World2D；仅 display compose 触碰 |
| View content selection | 当前 FRenderFeatureMask 只表达 Game/Gizmo/Debug | extraction buckets | 如有需要新增小型 SceneViewContents | 不表达 renderer identity，不进入 family key |
| 2D picking | 当前未定 | Editor selection | GameEditor CPU quad hit-test（MVP） | 使用当前 View camera/inverse；不得新建 GPU pass 作为第一步 |

## 已确认的依赖修正

1. ya-render-3d 当前仍依赖 ya-gui-compose，RenderFrameInputs 还携带 UIFrameSnapshot，
   ViewCompose.cpp 直接调用 GUI compose。这是 P0 的边界修复项，不是 World2D feature 的长期形状。
2. World2DRenderPipeline 不作为第三个 ISceneViewFamilyRenderer；World2D 是 active
   Forward/Deferred graph 内的 typed pass。
3. SceneViewFamilyKey 继续按 (Scene, sceneRevision, policy) 分组；不把每个 render family
   变成新的 scheduler/registry。
4. Game UI、editor viewport compose、designer canvas、standalone GUI surface 是四类 target，
   不共享一个“把所有 2D 画上去”的入口。

## P0 还必须查清的两件事

- Forward/Deferred 的 bloom graph 输入和 finalize 输入在加入 World2D 后如何保持清晰的 graph
  依赖；默认不能让 World2D 意外进入 bloom。
- TextureSlot/resourceVersion 的 ready/pending/failed 行为如何在 extraction 前收敛成一个
  immutable candidate，不允许每个 renderer 自己处理资源 resolve。

## 2D Capability Matrix（2026-09-26，P0 收口）

调用方普查方法：沿 `Render2DList` 全部方法的调用链（UI snapshot replay / extraContent）逐点
归档，按 plan §9.5 的四类 target 分组。**结论先行：UI 与 overlay 的屏空间能力集兼容——一条
screen shader 服务四类 target；世界内容（billboard/线）归场景侧管线；不新增 shader/pipeline
拆分（engine 先例：UE Slate 与世界内容分开——我们已采用；Godot canvas 统一，但其世界 2D 无
深度语义且管线归 GUI 所有，与"World2D 需与 World3D 深度共存""Render3D 不依赖 GUI"两个硬
约束冲突）。**

### 调用方 × 能力（普查实测）

| 能力 | UI replay（chrome/gameUI/offscreen） | CanvasPreview | ViewportCompose（编辑器 3D） | ToolSurface+inspector |
| --- | --- | --- | --- | --- |
| makeSprite(pos/transform) | ✓ | ✓（网格/选择） | ✓（HUD/场景 blit opaque） | ✓（inspector） |
| makeText（SDF>48px / Coverage≤48px，chooseModeForSize） | ✓ | ✓ | ✓ | ✓ |
| drawRoundedRect（SDF corner） | ✓ | – | – | – |
| makeRectFilledMultiColor | ✓ | – | – | – |
| makeWorldLine / makeWireBox / makeWireSphere | – | – | ✓（网格/视锥/物理/AABB） | – |
| makeWorldSprite | –（**全仓零生产调用方**，Phase 4 World2D 前瞻） | – | – | – |
| push/popClipRect | ✓ | ✓（经 snapshot） | – | ✓（经 snapshot） |
| depth 附件 | 无（uiPipeline 深度变体） | 无 | 有（**仅 Line 管线** depth-test：LessOrEqual/Always） | 无 |

### 四类 target 的 load/depth 差异

| kind | load/clear | clear 值 | depthTarget |
| --- | --- | --- | --- |
| RuntimeUIComposite | Load | 不生效 | 无 |
| RuntimeUIOffscreen | Clear | (0.05,0.06,0.07,1) | 无 |
| EditorCanvasPreview | Clear | (0.055,0.06,0.07,1) | 无 |
| EditorViewportCompose | Clear | (0.07,0.075,0.09,1) | **唯一可带深度**（bAttachDepth），depth loadOp=Load |
| EditorToolSurface | 无 rendering（回放进已开 presentation pass） | – | 无 |

### 管线契约结论

- uiPipeline 与 screenPipeline 的全部差异 = depth attachment 格式（数据驱动变体，保留）；
  quad（screen/world）两套管线深度测试恒关；**唯一启用深度测试的是 Line 管线**（世界
  overlay 对 scene depth 做测试）——UE 的 PDI 形状。
- textureRef 的 30-bit 槽 + 2-bit 采样模式是纯每顶点载荷，三种消费分支全在 FS
  （Coverage discard / Sdf smoothstep / Opaque force-a=1）；re-key 只动槽位。
- **world quad 管线（vertWorldMain）零生产调用方**：登记为 Phase 4 World2D 的前瞻路径
  （届时由 Forward/Deferred graph pass 使用或删除）；overlay 现用的世界内容全部是
  FLineRender 线（独立 shader + depth 变体，已与 quad 分离）。
- 结论：**不新增 shader/pipeline 拆分**（一条 screen shader 服务四类 target；世界内容
  走场景侧）；P1 §4.3 的 textureRef 位协议删除按 typed 化执行。
