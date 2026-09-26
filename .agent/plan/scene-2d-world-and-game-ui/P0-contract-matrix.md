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
