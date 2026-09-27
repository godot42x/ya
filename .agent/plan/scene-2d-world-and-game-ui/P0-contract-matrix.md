# P0 Contract Matrix（2026-09-26 初版）

这不是实现清单，而是执行 P1 之前必须冻结的 owner/producer/consumer/lifetime 表。
表中“当前”描述仓库现状；“目标”描述本计划要求的边界。任何没有 owner 的字段、pass、
callback 或 global state 不得进入后续 phase。

| 事实 / 数据 | 当前 producer | 当前 consumer | 目标 owner | 生命周期 / 安全点 |
| --- | --- | --- | --- | --- |
| SceneViewDesc / View outputRect | RuntimeGameViewProducer、EditorViewProducer | Scheduler、RenderFrameExtractor、active renderer | 各 View producer | 当前 tick 声明；target 在 ViewTargetStore safe point 准备 |
| Scene snapshot | HostSceneExtract → RenderFrameExtractor | 每个 View 的 RenderFrameData / active renderer | GameRuntime extraction | 同 Scene+revision 一 tick 共享；不可变至 submit/fence |
| World sprite candidate（待实现） | P0 审计后选定的 pre-extraction resolved binding producer | Scene runtime sprite draw path（graph 策略由 P0 决定） | RenderFrameExtractor + selected Scene workload | 不在 recording 期 resolve；texture/image/descriptor 至少活到 submit/fence |
| Camera projection | CameraComponent 当前无参 getProjection | Runtime/Editor producer | CameraComponent 纯 projection function；producer 传 effective aspect | 不读 Window/Swapchain；View declaration 前生成 |
| Camera view | CameraComponent 当前 getFreeView/getOrbitView | Runtime/Editor producer | SceneCameraQuery / producer/controller | 只读 Transform；禁止 projection getter 修改 Transform |
| World2D target | 尚无 | 尚无 | P0 选定的 Scene workload/graph | P0 冻结 target、load/store、depth、blend、与 3D opaque/transparent、bloom/finalize 的视觉顺序；纯 2D 不得承担无用 3D stages |
| Game UI snapshot | GameUIHost / EditorGameUIPreview | GameRuntime / GameEditor 显式调用 GUI compose | GameRuntime / GameEditor compose owner | graph build 前冻结；recording 不访问 WidgetTree；Render3D 不持有 snapshot |
| UI target | Runtime 当前写 View display image；Editor/standalone 由各自 host 选择 | Render2DComposePass | 应用/GUI host 提供明确的目标图像、extent、encoding | compose helper 不猜产品语义；不为不同产品场景复制 renderer |
| Render2D list / batch cursor | `Render2DList` value builder（像素 + 世界混装）+ 静态 `Render2D::recordRender2DList` | GUI compose、editor overlay；World2D 尚无生产者 | D1：`ScreenDrawList` / `WorldDrawList` 分类型；`ScreenDrawRecorder` / `WorldDrawRecorder` 由目标 owner 持有（见 plan §2.4） | flight/submit 范围内保活；禁止后续 flush 覆写在提交中的数据；录制器销毁走 DeferredDeletionQueue |
| 2D 共享 pipeline / PSO 缓存 | 静态 `Render2D::init`：`GUIAppHost`（swapchain 格式）与 `PipelineCoordinator`（3D 管线格式）谁先到谁生效；`PipelineCoordinator::shutdown` destroy | 所有 2D 录制 | D1：`ScreenDrawPipelines` 归持有 `IRender` 的一方（GUIApp：`GUIAppHost`；ya::App：App 设备状态）；`WorldDrawPipelines` 只在 ya::App 创建 | 设备 init 之后建、设备销毁之前毁；PSO 变体按格式懒建，只在录制外 |
| Pass slot | `Render2D::acquirePassSlot`；GUI session 持 present/offscreen slot；`composePassSlot` 函数级 static 池 | Render2D 内部资源隔离 | D1 删除：隔离由录制器实例天然提供 | — |
| 编辑器 View overlay | GameEditor `EditorViewOverlay`：tone-map 后的 display image 上先 `WorldDrawList`（测 View 深度不写）再 gizmo/HUD | 编辑器 3D 视口 widget | GameEditor。GUI compose 不持相机、scene color、深度 | 每帧写 authoring View 的 display image；相机预览 View 不画 overlay |
| Render2D backend resource access | 当前 `QuadRender` 存在 Vulkan/backend texture access（需复核工作区实现） | screen/world pipeline record | P0 冻结 RHI capability 与后端资源访问 seam；Render2D 不直接拥有 Vulkan-only policy | 核对 Vulkan/OpenGL 实现、texture/sampler/pipeline/buffer owner；只抽真实需要的公共机制，不加空 facade |
| Swapchain / presentation | Host acquire/present + SurfacePresentation | display compose | presentation layer | 不属于 View/Scene/World2D；仅 display compose 触碰 |
| View content selection | 当前 FRenderFeatureMask 只表达 Game/Gizmo/Debug | extraction buckets | P0 决定是否有必要增加明确的 SceneViewWorkload | 不能用空 candidates 推断不需要 stages；若加，只表达内容请求，不表达 renderer identity，不进入 Scene family key |
| 2D picking | 当前未定 | Editor selection | GameEditor CPU quad hit-test（MVP） | 使用当前 View camera/inverse；不得新建 GPU pass 作为第一步 |

## 已确认的边界与待验证决策

1. 当前 ya-render-3d、RenderFrameInputs 已无 GUI/Compose 与 UIFrameSnapshot 依赖；
   RuntimeRenderContext 显式 compose Game UI。此边界已实现，P0 保留依赖扫描作为回归守卫。
2. GUI compose graphics path 与 Scene runtime sprite path 必须语义分离。共享 RHI/RenderGraph
   不代表共享 mixed shader flags、坐标、排序或 global recording state。
3. Scene snapshot dedupe 继续按 Scene/revision；rendering workload/family 可以因 stage 和资源
   需求不同，不能为了合并 family 迫使纯 2D View 分配 3D resources。
4. GUI 与 Scene sprite 必须分别拥有 graphics pipeline contract；shader module、quad geometry、upload
   和纹理缓存是否共享逐项由 capability/lifetime 证据决定。compose 接收明确的 snapshot 与 target
   数据，destination policy 留在应用/GUI host；不按产品名称建立四套同构 renderer。

## P0 冻结前还必须查清的决策

- 先画一张“混合场景预期结果表”，并通过可见输出校验：
  - sprite 在 3D opaque 几何前/后时，是否按 Transform.z 做真实深度遮挡；
  - 两个 sprite 重叠时 layer、sortOrder、worldZ 的唯一优先级与稳定 tie-break；
  - sprite 与 3D transparent 的相对顺序，明确当前 MVP 是有限支持还是不支持；
  - 半透明 sprite 的 blend/color encoding；高亮 sprite 是否进入 bloom，以及 sprite 与 3D 是否共用
    tone-map；
  - 空场景/空 sprite workload 的 clear 与 output 行为。
  不能只为“避开 bloom”固定 pass 位置，也不能只靠 graph pass 调用次数验收。
- 纯 2D View 如何不分配/执行无用的 GBuffer、lighting、shadow stages；比较可选 runtime stage 与
  轻量 2D-only graph，选择能保住单一 app orchestration 且 GPU 成本最低的实现。
- Render2D 当前 backend-specific texture/pipeline/buffer access 的真实调用边界，以及 Vulkan/OpenGL
  是否都支持目标能力；若要补 RHI seam，只补足现有后端实现所需的最小接口，不把 backend policy
  留在业务 draw contract，也不为未使用后端造层。
- TextureSlot/resourceVersion 的 ready/pending/failed 行为，以及 AssetManager、ResourceResolveSystem
  和 RenderFrameExtractor 各自现有职责；选定唯一 pre-extraction producer 收敛为 immutable candidate，
  不允许每个 view/renderer 自己 resolve。
- SceneViewWorkload 是否需要显式表达纯 2D/纯 3D/mixed；candidate 为空不是可靠语义。

## 2D Capability Matrix（2026-09-26，现状调查 + P1 落地记录）

调用方普查方法：沿 Render2D 对外绘制方法的实际调用链，分别标记 GUI snapshot replay、
GameEditor overlay 与 Scene runtime 绘制。下表只记录当前消费者，不把“几何都像 quad”当作
pipeline 可合并的证据。GUI 与 Scene sprite 必须有分离的 typed draw contract / graphics state；
当前 P1 已拆 screen/world shader 与顶点布局，并完成 textureRef typed 化。低层 batch/upload owner 是否
进一步拆分仍按资源生命周期证据决定；World2D 尚无 Scene graph 生产调用方。

### 调用方 × 能力（普查实测）

| 能力 | UI replay（chrome/gameUI/offscreen） | CanvasPreview | ViewportCompose（编辑器 3D） | ToolSurface+inspector |
| --- | --- | --- | --- | --- |
| makeSprite(pos/transform) | ✓ | ✓（网格/选择） | ✓（HUD/场景 blit opaque） | ✓（inspector） |
| makeText（SDF>48px / Coverage≤48px，chooseModeForSize） | ✓ | ✓ | ✓ | ✓ |
| drawRoundedRect（SDF corner） | ✓ | – | – | – |
| makeRectFilledMultiColor | ✓ | – | – | – |
| makeWorldLine / makeWireBox / makeWireSphere | – | – | ✓（网格/视锥/物理/AABB） | – |
| makeWorldSprite | –（已于 `ff7817bc` 删除） | – | – | – |
| push/popClipRect | ✓ | ✓（经 snapshot） | – | ✓（经 snapshot） |
| depth 附件 | 无（uiPipeline 深度变体） | 无 | 有（**仅 Line 管线** depth-test：LessOrEqual/Always） | 无 |

### 当前 destination 的 load/depth 差异

| kind | load/clear | clear 值 |
| --- | --- | --- |
| RuntimeUIComposite | Load | 不生效 |
| RuntimeUIOffscreen | Clear | (0.05,0.06,0.07,1) |
| EditorCanvasPreview | Clear | (0.055,0.06,0.07,1) |
| EditorToolSurface | 无 rendering（回放进已开 presentation pass） | – |

View overlay 不在这张表里：它是 GameEditor 自己的 pass，颜色 Load（不清除已 tone-map 的 display），深度 Load。

### 现有能力结论（不是目标架构冻结）

- 当前 uiPipeline 与 screenPipeline 的主要已知差异是 depth attachment 格式（数据驱动变体）；
  quad 的 screen/world 路径当前都没有深度测试，**唯一启用深度测试的是 Line 管线**（世界 overlay
  对 scene depth 做测试）。这描述现状，不足以证明 UI compose 与 runtime sprite 应共用 pipeline。
- textureRef 混合 texture index 与 SDF/opaque flags 的协议必须删除；采样、alpha 与材质语义改为
  typed draw/pipeline state。不能只重排位域或以新的 packed integer 替换。
- world quad 管线（vertWorldMain）当前零生产调用方；FLineRender 用于带 scene depth policy 的
  editor/world overlay。此现状说明不能直接复用 world quad 旧实现，不说明需要新造完整 batch 系统。
- 目标边界：GUI Compose 与 Scene runtime sprite 各自有独立 graphics-pipeline owner/state contract；
  现有 screen/world pipeline config、shader 和顶点布局已拆分；低层 resource/batch owner 不必仅因分层
  再复制。World quad 当前没有 Scene 生产调用方，因而不能据此宣称 World2D graph 集成已完成。P1 的
  textureRef flags 删除已有验证记录；不按 primitive 名称堆 shader/batch，也不预设多层 facade。

### 2026-09-27 更正与补充

- `Line` batch 只有世界语义：`Sprite2DLine` 乘调用方的 `viewProjection`，GUI 路径传单位矩阵时坐标被当成
  裁剪空间。GUI 的线是 `UIFrameDrawItem::EKind::Line` 转成的旋转细 quad（`makeSprite(mat4)`），且偏向法线
  一侧（D3 修正）。屏幕空间此前没有真正的 line / path 能力。
- 世界入口 `makeWorldLine` / `makeWireBox` / `makeWireSphere` 的唯一调用方是
  `EditorViewportOverlayRecord.cpp`；GUI replay、CanvasPreview、ToolSurface 均无调用。progress.md 中
  “Render2D 里不存在世界空间的绘制入口”一句不成立。
- 编辑器 overlay 当前录制顺序为：网格线 → gizmo 屏幕 quad → HUD → 视锥线 → 物理线 → 包围盒线。屏幕 quad
  深度测试/写入均关闭，后画的世界线会覆盖 gizmo handle。D2 以固定相位（世界 → 屏幕）解决。
- 分层结论、三类世界 2D 的区分、两条分叉的持有者表见 plan.md §2.4；执行见 §4A D1–D3。

### 混合场景验收记录（P0 必填）

| 场景 | 预期可见结果 | 已验证证据 |
| --- | --- | --- |
| sprite 在 3D opaque 前/后 | 待冻结；说明 Transform.z 与 depth test/write 的关系 | 待补混合输出截图/像素断言 |
| 两个 sprite 重叠 | 待冻结 layer/sortOrder/worldZ/tie-break | 待补可见顺序验证 |
| sprite 与 3D transparent 相交/重叠 | 待冻结 MVP 支持边界 | 待补视觉验证或明确限制 |
| 半透明 sprite + bloom/tone-map | 待冻结 blend/color encoding/bloom/tone-map 规则 | 待补颜色输出验证 |
| 空 sprite 列表、Scene 仍需 clear | 待冻结 2D-only 与 mixed graph 的合法空 workload 行为 | 待补可见输出验证 |
