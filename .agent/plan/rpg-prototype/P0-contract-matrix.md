# P0 Contract Matrix（2026-09-26 初版）

这不是实现清单，而是执行 P1 之前必须冻结的 owner/producer/consumer/lifetime 表。
表中“当前”描述仓库现状；“目标”描述本计划要求的边界。任何没有 owner 的字段、pass、
callback 或 global state 不得进入后续 phase。

| 事实 / 数据 | 当前 producer | 当前 consumer | 目标 owner | 生命周期 / 安全点 |
| --- | --- | --- | --- | --- |
| SceneViewDesc / View outputRect | RuntimeGameViewProducer、EditorViewProducer | Scheduler、RenderFrameExtractor、active renderer | 各 View producer | 当前 tick 声明；target 在 ViewTargetStore safe point 准备 |
| Scene snapshot | HostSceneExtract → RenderFrameExtractor | 每个 View 的 RenderFrameData / active renderer | GameRuntime extraction | 同 Scene+revision 一 tick 共享；不可变至 submit/fence |
| World sprite candidate | `RenderFrameExtractor::extractSprites`（每 Scene+revision 一次；组件只存 authored reference，resolved binding 在抽取期由 `slotToTextureBinding` 取得） | active Deferred graph 的 `appendSprite2D`（读 View 的 bucket，不再遍历 ECS） | RenderFrameExtractor + active Scene graph | candidate 持有 `TextureBinding`（强引用），保证 texture/image/descriptor 活到 submit/fence；不在 recording 期 resolve |
| Camera projection | CameraComponent 当前无参 getProjection | Runtime/Editor producer | CameraComponent 纯 projection function；producer 传 effective aspect | 不读 Window/Swapchain；View declaration 前生成 |
| Camera view | CameraComponent 当前 getFreeView/getOrbitView | Runtime/Editor producer | SceneCameraQuery / producer/controller | 只读 Transform；禁止 projection getter 修改 Transform |
| World2D target | View 的 SceneColor（load/store）+ GBuffer depth（load/store） | `appendSprite2D`：skybox 之后、bloom 之前 | active Deferred graph | 现状：opaque 写深度、translucent 只测；无 sprite 的 View 不建 pass。**2026-10-03 冻结为只测不写（见文末 C1）**；纯 2D View 的 graph 策略待步骤 6（C5） |
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

（2026-10-03 已按文末「2026-10-03 冻结」回答，其中纯 2D graph 与 Render2D 后端 seam 两项未选，见 C5 与步骤 5。下面保留原问题。）

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

## 2026-10-03 冻结（评审步骤 3）

只产出契约，不改代码。下面每一条都写明「现状 → 冻结后」，实现落在步骤 4 / 5 / 6。
之前的 owner / producer 表（本文件开头）就是 `pipeline_contract_audit` 的结论，不再另写一份。

### C1 精灵之间的前后关系：只有画家顺序

比较过 Unity 2D（Sorting Layer → Order → 距离，透明队列，不写深度）、Godot 4（z_index → 树序，
节点级 y-sort，2D 无深度缓冲）、Bevy（按 z 排，不写深度）、Unreal Paper2D（半透明按优先级加距离，
Masked 写深度）。2D 引擎主流是画家顺序，只有从 3D 长出来的 Paper2D 靠深度。

| | 现状 | 冻结后 |
| --- | --- | --- |
| 不透明精灵 | 无混合，`a < 0.01` 丢弃，写深度 | 与半透明同一条管线，混合开启，`a < 0.01` 只作为提前丢弃 |
| 半透明精灵 | 测深度、不写深度，另走一遍 | 同上，不再分两遍 |
| 深度附件 | `Load / Store`，测试 `LessOrEqual` | 只读测试（`LessOrEqual`），不写。graph 里声明为只读深度 |
| 精灵之间 | 不透明先、半透明后，再按 layer、sortOrder，最后到相机的距离（远到近）；不透明还要靠深度 | 只看排序键（C2），`Transform.z` 与精灵之间的顺序无关 |
| `Transform.z` 的作用 | 精灵间深度、对 3D 不透明的遮挡 | 只用于被 3D 不透明几何遮挡 |

后果（已接受）：
- 精灵不再写深度，编辑器 billboard 图标（灯、相机）会画在精灵之上。
- 精灵不再遮挡 3D 前向透明物体（粒子等）；2D 玩法线用不到。
- 鼠标拾取与绘制共用同一个比较函数（C2），拾取取绘制顺序里最后的一个。
- `Actor.zFor` 与 `z = 基准 − y × ε` 约定随步骤 5 删除，`2d-gameplay` skill 同步改（5a 已完成）。

### C2 排序键：layer → y → order

比较函数只有一个，放在 `Scene2D`（步骤 4），提取期排序与 2D 拾取都调它。

```
key = (layer, ySortRank, yKey, order, tiebreak)   // 全部升序，后画的在上面
```

- `layer`：`Sprite2DComponent::layer` / `TilemapComponent::layer`（int，已有）。
- `ySortRank`：未开 y-sort 的对象为 0，开了的为 1。同一 `layer` 里不 y-sort 的先画，y-sort 的后画；
  所以「y 项恒为 0」不会和 y-sort 对象混成任意顺序。
- `yKey`：开 y-sort 的对象取 `-sortY`（世界 y 越大越早画，越靠屏幕下方越晚画）；不开的恒为 0。
  `sortY` 是对象排序点的世界 y，取 texel 吸附之后的值，同一行的对象落在同一个 `yKey`，由 `order` 决定。
  排序点在 pivot 落地（步骤 4）之前是精灵底边中点，之后是 pivot。
- `order`：`Sprite2DComponent::sortOrder`；tilemap 取子层下标 `layerIndex`（现状）。
- `tiebreak`：实体 id，其次 tile 的提取顺序（行优先），保证同键时结果稳定、不依赖 `std::sort` 的不稳定性。
- y-sort 是逐对象开关：`Sprite2DComponent::bYSort`、`TilemapLayer::bYSort`，tilemap 子层默认关。
  字段名与序列化形状在步骤 4 定，这里只冻结语义。
- 未开 y-sort 的 tile 层不会因为 y 在同层里穿插：`yKey` 恒为 0，行优先提取顺序即 tiebreak。

**C2 增补（5a，2026-10-04）**：`TilemapLayer::layerOffset`（int，默认 0 不序列化）。tile 绘制 layer = `TilemapComponent::layer + layerOffset`，order = `layerIndex`。
原因：`layer` 是组件级，而 `ySortRank` 让未开 y-sort 的先画，Overlay 子层（rank 0）否则会被角色（rank 1）盖住；Overlay 设 `layerOffset = 1`。
`zOffset` 只留给 3D 不透明遮挡。同 order、同 layer、都不 y-sort 时由实体 id 决定先后——Town 的门（id 1024）因此需要 `sortOrder = 1`，否则被 TilemapGround（id 2000）盖住；这是数据层面的显式表达，不是排序规则的例外。

### C3 实例格式与分批

| 项 | 冻结 |
| --- | --- |
| 实例数据 | `worldCenter`(3) + `axisX`(3) + `axisY`(3) + `uvRect`(4，翻转已折进 uv) + `tint`(4) + 纹理槽下标；与现有 push constant 内容一致，只是从逐 draw 变成逐实例 |
| 排序键不进 GPU | 排序在 CPU，输出绘制顺序；GPU 只看顺序 |
| 分批 | 按排好的顺序，连续同管线、同纹理表的一段合成一批。**禁止为了合批把顺序打乱**（不得按纹理重排） |
| 切批条件 | 纹理表（`kTextureTableSize = 16`）装不下下一个纹理，或管线状态变化 |
| 共享核心 | 与 `ScreenDrawList` 共享 quad 实例核心、批游标和纹理表；前端各自处理相机矩阵 / 像素裁剪 |
| 纹理表 | 按资产槽身份作键，直接映射，不再每帧重建加线性查找（步骤 5） |

### C4 资源责任

- candidate 持有 `TextureBinding` 强引用，抽取期解析，录制期不 resolve（与矩阵开头一致，不变）。
- 实例缓冲按 flight 保活，销毁走 `DeferredDeletionQueue`；同一提交内不覆写已经上传的数据；被引用的
  buffer / texture 活到 submit 与所需 fence（`render2d_upload_submit_lifetime`，步骤 5 的验收条件）。
- tilemap 静态实例缓冲只在编辑时重建，重建信号走编辑漏斗（步骤 5）。

### C5 graph 位置与纯 2D

- Sprite pass 的位置不变：天空盒之后、Bloom 之前，写 `viewColor`，所以精灵不受光照，经过 Bloom 与 tone-map，
  与 3D 场景共用同一套颜色处理。
- 内容门：View 没有精灵就不加这个 pass（已有，`appendSprite2D`）。
- 纯 2D View 怎么不分配 / 不执行 GBuffer、光照、天空盒、Bloom：**本次不选**。候选是「View 声明 workload，
  同一张 Deferred graph 跳过不需要的 stage」与「单独一张 2D-only graph」，等 `render-view-family` P3 的
  ViewFamily compiler 再定（步骤 6）。约束不变：不新增第二套 Scene scheduler，纯 2D 不支付 3D attachment。
- 空 sprite 列表且场景需要 clear：由前面的 pass（GBuffer / 光照 / 天空盒）负责，精灵 pass 不参与；
  纯 2D 的 clear 归属随上一条一起定。

### 混合场景预期（冻结）与验收

「已验证证据」一列现在没有数据：本步骤只冻结契约，不写代码，所以没有可见输出可以校验。
每一行是步骤 5 合批核心的验收项，到时用自动化截图 / 像素断言补。

| 场景 | 冻结的预期可见结果 | 验收（步骤 5） |
| --- | --- | --- |
| sprite 在 3D opaque 前/后 | sprite 用真实 `Transform.z` 对 3D 不透明深度做测试：在几何后面的部分被遮住，在前面的完整可见。精灵不写深度 | 一个立方体穿过精灵平面的截图，像素断言交线两侧 |
| 两个 sprite 重叠 | 只由 C2 的键决定，与 `Transform.z` 无关；同键按 tiebreak 稳定 | 同位置、不同 z 的两个精灵，调换 z 画面不变；调换 order 画面变 |
| 同层 y-sort | 开 y-sort 的对象，y 小（更靠屏幕下方）的盖住 y 大的 | 两个角色上下相邻重叠，上下交换后遮挡随之交换 |
| sprite 与 3D transparent 相交/重叠 | **不支持顺序交织**：3D 前向透明物体总在所有精灵之后画，只测 3D 不透明深度（精灵不写深度，所以精灵后面的透明物体会盖在精灵上）。MVP 的明确限制 | 文档化的限制，不做像素断言 |
| 半透明 sprite + bloom/tone-map | 半透明精灵 `SrcAlpha / OneMinusSrcAlpha` 混合进场景颜色，高亮精灵进入 Bloom，与 3D 共用 tone-map | 半透明精灵叠 3D 地面，比较与现状的逐像素差异，差异只来自「不透明不再硬裁」 |
| 空 sprite 列表、Scene 仍需 clear | 没有精灵 pass，输出就是前面 pass 的结果 | 无精灵场景截图与现状逐像素一致 |
