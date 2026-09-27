# Progress

## 2026-09-26 — 计划一致性复审（仅改计划文档）

### 本轮收敛

- 保留已落地的 Render3D→GUI Compose 边界；feature matrix 改为 verified regression guard，
  不再把删除 ViewCompose 或 FramePacket snapshot 当未来任务。
- GUI Compose 与 Scene runtime sprite 使用不同 graphics-pipeline config/state contract；shader module、
  几何、upload 和纹理缓存是否共享再按能力与生命周期证据决定。删除“pipeline 是否分开也待定”的模糊措辞，
  同时不按 primitive 名称制造 shader/batch 层。
- 撤回“World2D 必在 Forward/Deferred”“无 depth write”“bloom 后、tone-map 前”等预设。P0 必须用
  混合场景结果冻结 Transform.z、sprite 排序、3D opaque/transparent 遮挡、blend/color encoding、
  bloom/tone-map 规则；纯 2D 与空 workload 也要定义 clear 和 stage 成本。
- World2D graph 仍可选为 active runtime graph 的可选 stage，或轻量 2D-only graph；不新增第二套
  Scene scheduler/app loop。独立 graph 不再被“第三 renderer”禁令误伤。
- 资源 resolve 责任改为 P0 审计项：先核对 TextureSlot、AssetManager、ResourceResolveSystem 的实际
  owner，再确定唯一 pre-extraction resolved-binding producer；不让每个 View/renderer 各自 resolve。
- 修正 hidden View 语义：不提交该 View 的 request/cull/order/pass，不表示同 Scene 其他可见 View 不做
  共享 extraction，也不提前释放 registered View target。
- 更新 game-ui-authoring 对应术语，避免把 Scene sprite workload 称作 World2D family；补充禁止仅用
  pass 调用次数作为验收的要求。

### 当前状态

- P0 的 Render3D→GUI 边界已有实现与 parity/test 证据；P0 混合视觉契约、资源 resolve owner、纯 2D
  graph 策略仍未冻结。
- P1 的 Render2DList 值化、screen/world shader+vertex split、textureRef typed 化均已有 parity/test
  记录，feature matrix 标为 verified。World2D 仍无 Scene graph 生产调用方；GPU upload/submit 生命周期
  和后续 Scene integration 仍待做，不能把这些前置完成等同于 2D 游戏闭环完成。
- 未改运行时代码，未运行 build/test；本轮检查计划一致性，不构成代码 checkpoint。
- 当前工作区存在其他 shader/Render2D 改动，本轮未覆盖、暂存或提交。
- 下一步：先填 P0-contract-matrix 的混合场景预期结果和证据，再开 P0/P1 实施；如果无法定义某个 MVP
  遮挡场景，写明功能限制，不用模糊 pass 顺序掩盖。

## 2026-09-26 — Architecture review（本轮未改运行时代码）

### 保留的结论

- Game UI compose 与 Scene runtime rendering 继续分开；不引入 Transform2D、Camera2D、Node2D 或
  第二套 Scene tree。
- 同一个 Scene/revision 的 snapshot 在同一 tick 只 extraction 一次；View 只持有自己的 camera、
  cull、order 和 output。
- 2D authored sprite 使用现有 TransformComponent；正交只是 CameraComponent 的 projection mode。
- Game UI runtime/designer 继续使用不同 WidgetTree 和 UIFrameSnapshot 生命周期。

### 本轮发现并已写回 plan.md 的问题

1. 当前 Render3D 仍依赖 GUI compose：RenderFrameInputs 携带 UIFrameSnapshot，ViewCompose.cpp
   include GUI/Compose，ya-render-3d xmake 还 public 依赖 ya-gui-compose。必须在 P0 收回到
   GameRuntime/Editor 的应用侧 compose。
2. World2D 不应成为第三个 ISceneViewFamilyRenderer；它应作为 active Forward/Deferred graph
   中的一个 typed sprite pass。
3. 不再预先承诺四套 UI/World2D shader，也不为分层制造 QuadResourcePool/多个 batch facade；先
   做 capability matrix，只拆数据/状态/生命周期真正不兼容的部分。
4. 不增加 EViewRenderFamilyMask；如确需关闭内容域，只使用小型 SceneViewContents，不表达
   renderer identity，也不加入 family key。
5. World2D MVP 的 graph contract 固定为线性 SceneColor load/store、无 GBuffer、无 depth write，
   放在 bloom graph 之后、finalize/tone-map 之前，避免意外进入 bloom。
6. CameraComponent 最终只负责纯 projection；producer 传 effective aspect，view 由 producer/
   controller 根据 Transform 计算。现有 getOrbitView 的 Transform 写入副作用列为迁移项。
7. Sprite2DComponent 默认沿用 Render3D authored component domain，不放进 gameplay ECS systems；
   ResourceResolveSystem 负责资产 ready/pending/version，extractor 不直接 resolve GPU 资源。
8. 2D picking MVP 采用 editor CPU quad hit-test，不为第一版新增 EntityId GPU pass。
9. Game UI、editor surface、designer offscreen、standalone GUI 是四类 compose target，不能再用
   一个 helper 隐含它们的 layout/load/store/input 语义。
10. Render2D 仍有 Vulkan 判断、Backend TextureLibrary 和 process-global session/cursor；P0 必须
    先冻结 backend seam 与 pass-local state 规则，不能在 UI/World2D 新路径中复制这些分支。
11. 当前 displayRootTask() 只取第一个 bDisplayRoot；多 OS window 的 surface-scoped View output
    尚未在本线解决，不能把本计划误写成多窗口闭环。

### 新增审计工件

- P0-contract-matrix.md：记录当前/目标 owner、producer、consumer、生命周期与待确认的 bloom/
  resourceVersion 契约。

### 当前状态

- P0–P7 仍为 not started；本轮只是把原计划中会导致过度设计或错误依赖的假设改正。
- 尚未创建 Sprite2DComponent、World2D pass、正交 CameraComponent 实现或新 shader。
- 未运行构建/测试；下一步应先完成 P0 调用方表与 Render3D→GUI 边界修正设计。

## 2026-09-25 — 计划建立

### 本轮已核对

- TransformComponent 已经保存 vec3 position/rotation/scale，Node3D/TransformSystem 负责层级和
  world matrix；没有必要新增 Transform2D。
- CameraComponent::getProjection() 当前只有 perspective，但 Core/Camera/FreeCamera 已经存在
  EProjectionType::Orthographic 和 FMath::orthographic，因此正交能力应收敛到 CameraComponent。
- SceneViewDesc、SceneRenderScheduler、ExtractedSceneRender 已经支持同 Scene 多 View 共享 immutable
  snapshot；后续只需补 World2D candidates/view buckets/family mask，不应另造 scene scheduler。
- Render2D 当前通过 FQuadRender/Sprite2D.slang 同时承担 screen/UI 与 world quad，并用 textureRef
  编码采样模式；这是 UI pipeline 与 world runtime pipeline 混杂的主要来源。
- GameUIHost/UIFrameSnapshot/ya-gui-compose 已具备 UI 独立快照和 compose 链路，UI 不应进入
  World3D/World2D extraction。
- EditorGameUIPreview 已经有单独 preview WidgetTree，但尚未具备完整 preview clock/input policy。

### 尚未执行

- 未创建 Sprite2DComponent、World2D pipeline、CameraComponent 正交实现或 shader 文件。
- 未改动旧 game-ui-authoring 计划的运行时代码。
- 未运行构建/测试；本文件只是后续执行的基线。

### 执行纪律

- 每个 phase 只形成一个可运行闭环后提交；不得以移动文件、加 registry、加空接口冒充完成。
- 保留用户工作区已有修改，不 blanket stage；计划与代码、测试在同一 checkpoint 提交。
- 若实现过程中发现需要 Transform2D、Camera2D 或第二个 Scene tree，先停止并回到架构评审，不能临时引入。

---

## 2026-09-26 — P1 前置落地：Render2DList builder（会话所有权值化）

对 Phase 1 §4.5（"Render2D session 所有权改成调用方明确传入，不得依赖 process-global
pending kind 或跨窗口隐式状态"）与 §2.3 review 中 "Render2D 当前有 process-global
session/cursor" 修正项的落地。本轮把 2D 录制改成**先产值再录制**（同 3D graph 的
build/record 分割），顺带完成了 P0 工作项 1/2 的审计目标：

### 已落地

- `Render2DList`（Render2D 模块新值类型）：makeSprite/makeWorldSprite/makeText/
  drawRoundedRect/makeRectFilledMultiColor/makeWorldLine/makeWireBox/makeWireSphere/
  pushClipRect/popClipRect；命令流（kind/firstVertex/count/clip 快照）+ 每 kind 顶点数组 +
  builder 本地纹理表 + clip 栈。构建期不接触 cmdBuf/passSlot/device。
- `Render2D::recordRender2DList(list, ctx)`：唯一 record 步——解析 flight slot（device 环，
  不问 swapchain）、按 per-slot/per-flight 资源提交、局部纹理表重键进全局绑定表、
  按（kind 变化/clip 变化/容量/纹理表满）边界重放，与原立即 flusher 的边界一致。
- 旧立即面删除：`Render2D::begin/end/session/makeXxx/pushClip/popClip/beginBatch/
  flushPending/onUpdate/onRender` 全部移除；FQuadRender 的立即 draw 方法与 CPU 游标、
  FLineRender 的 addLine/addWireBox/addWireSphere 一并移除；`FRender2dSession` 类型删除。
  Emit 纯函数（`FQuadRender::EmitScreenQuad/EmitWorldQuad`）保留，list 与 record 共用。
- 顺带修正：`buildQuadViewportState` 不再读 session（管线 viewport/scissor 是动态状态，
  CI 里只留合法占位）；`PrimitiveMeshCache` 恢复其声明的锁（现存竞态 bug 修复）。

### 证据

- `ya-testing` 1314 passed / 0 failed；新增 `Render2DListTest`（同输入产出一致 list、
  clip 变化关闭旧命令、kind 变化分裂命令）——全部纯 CPU，无渲染设备。
- parity 两图 md5 `c775245ae636f15b41da8485319a2267` 与基线一致（像素逐字节相同）；
  GUIWorkbench `--smoke-actions` PASS；editor smoke exit=0。
- `rg "Render2D::(begin|end|makeSprite|makeWorldSprite|makeText)|pushClipRect"` 在产品代码
  零命中（P0 校验项之一提前达成）。

### 对后续 phase 的影响

- Phase 4 的 World2D workload 可直接构建 Render2DList（builder 无 cmdBuf/slot/device，
  支持并行构建）；record 步是唯一触 GPU 的点。
- Phase 1 §4.3（删 textureRef 高位 bit 隐式协议）尚未做：list 顶点仍复用
  FQuadRender::Vertex 布局（局部 slot + mode 位编码），值化已为 typed 化铺好插入点
  （builder 发射方法改 typed data、record 步消费）。
- 未创建 Sprite2DComponent/World2D pass/正交 CameraComponent/新 shader；未动
  game-ui-authoring 运行时代码。


---

## 2026-09-26 — P0 收尾：Render3D 对 GUI 的依赖归零

对 P0 工作项 6 与 Phase 1 §4.1 前半的落地（架构 review 修正表第一行）。

### 已落地

- `Render3D/Common/ViewCompose.h/.cpp` 删除。`recordCameraViewCompose`（唯一调用者
  RuntimeRenderContext）的函数体收回应用 record 顺序：`RuntimeRenderContext::record`
  增加第三参 `const UIFrameSnapshot* uiSnapshot`，直接调用 `recordRender2DComposePass`
  （RuntimeUIComposite 落在显示根的 image 上，首帧/无 UI 跳过语义保留）。
- `FramePacket::uiFrameSnapshot` 字段与前向声明删除；`TickFrame::uiSnapshot` 保留为
  应用侧 GameUI record packet，`boundFrame()` 不再绑定；snapshot 是 record 的显式
  调用参数，plan 仍是值。
- `makeViewDisplayInsetRect` 移为 `EditorViewProducer.cpp` 文件内布局策略（Render3D
  从不认识"preview"）。
- `Render3D/xmake.lua` 公共 deps 去 `ya-gui-compose`，补实现性依赖 `ya-render-2d`
  （PipelineCoordinator 在 device 生命周期里管理 2D batcher 的 GPU 资源——Render2D
  是 Framework/Render 邻居，不是 GUI framework）。
- `buildQuadViewportState` 的 session 读取改为固定占位（管线 viewport/scissor 是动态
  状态）；`RuntimeRenderContextTest` 的 record 概念断言更新为三参形状。

### 证据

- `rg "GUI/Compose|UIFrameSnapshot|ya-gui-compose" Engine/Source/Framework/Render/Render3D`
  → **零命中**（P0 校验口径）。
- `ya-testing` 1314 passed / 0 failed；parity 两图 md5 `c775245ae636f15b41da8485319a2267`
  与基线一致（移动的是调用位置，desc 构造等价，像素逐字节相同）；
  GUIWorkbench `--smoke-actions` PASS；editor smoke exit=0。

### 尚未执行

- capability matrix、textureRef typed 化（Phase 1 §4.2/4.3）、shader 拆分决策；
  World2D workload 与 Sprite2DComponent（Phase 3/4）；GameUI record packet 的
  结构化封装（等 Phase 4 一起定，本期 snapshot 走显式参数）。

---

## 2026-09-26 — P1 完成：shader/顶点布局按归属拆分 + textureRef typed 化

Phase 1 §4.3（强制项：删除 textureRef 高位 bit 隐式协议）与 §4.2（capability matrix
作为拆分依据）落地。引擎先例校准（用户拍板）：Godot 的 canvas 统一（一切 2D 同管线）
与我们的两个硬约束冲突（World2D 需与 World3D 深度共存；Render3D 不得依赖 GUI），采用
**UE 形状**——Slate 式 GUI compose shader 与场景 2D shader 分开。

### 已落地

- `Sprite2D.slang` 拆两个文件：`Sprite2DScreen.slang`（GUI compose 四类 target：
  Coverage/Sdf/Opaque 采样 + rounded SDF + multicolor）与 `Sprite2DWorld.slang`
  （场景 billboard 展开 + UV Y-flip，无 corner SDF 分支、无屏幕分支）。
- 顶点布局拆分：`FQuadRender::Vertex` 拆为 `ScreenVertex`（屏空间，无 world 死载荷
  32B）与 `WorldVertex`（billboard，无 corner 死载荷）；screen/world 两条管线各自
  独立的属性注册与 slang stage files；`buildQuadWorldPipelineCI` 补上
  `buildQuadWorldVertexAttributes`（漏接曾导致 VUID-07904 管线创建失败）。
- **textureRef typed 化**：`kTextureIndexMask/kTextureModeShift/encode()` 删除，顶点
  改为显式 `textureSlot` + `sampleMode` 两个字段；shader 侧两个
  `nointerpolation uint` 直读（mode 值经 SAMPLE_MODE_* 编译 define 传入，枚举单一
  来源）；record 步纹理重键只写 textureSlot，sampleMode 逐字节原样拷贝。
- `Render2D::recordRender2DList` 的 quad copy 拆成 screen/world 两条显式路径
  （typed 顶点类型不同），`emitted` 计数驱动容量分段；capacity flush 语义保留
  （区域满 → flush 当前区域 → 同命令在新区域继续）。

### 过程中抓到的两个真 bug（parity 抓到）

1. **批边界丢失**：record 步最初只按 kind 变化 flush，同 kind 不同 clip 的连续命令
   被合并进一个 region draw，整批用了最后一个 scissor（面板被按钮的裁剪框裁掉，
   35995 像素差异）。修成"clip 变化也是批边界"。
2. **quad 跳号**：copy 循环里 srcIndex 同时用了推进中的 src 指针和 q*4，每迭代跨 8
   顶点，最后一条命令读到数组尾部之外（90 帧段错误，frame 10 不现形因为字形少）。
   修成 emitted 计数单驱动。修后 parity 恢复基线。

### 证据

- `ya-testing` 1314 passed / 0 failed；parity 两图 md5
  `c775245ae636f15b41da8485319a2267` 与基线一致；GUIWorkbench `--smoke-actions`
  PASS；editor smoke exit=0。
- `rg "kTextureIndexMask|kTextureModeShift|textureRef"` 产品代码零命中（TextureSlot
  序列化字段同名是另一域，不受影响）。

### 尚未执行

- capability matrix 结论：**不新增 shader/pipeline 拆分**（screen 一条服务四类
  target；世界内容走场景侧管线）；world quad 管线零调用方（Phase 4 World2D 定义
  其最终形状）；batch owner 保持一个（§4.4）。
- World2D workload、Sprite2DComponent、正交 Camera（Phase 3/4）。

### 2026-09-26 补记（P0 矩阵补充）

- **世界 billboard 的场景侧路径已存在**：BillboardComponent 经 DeferredRenderPipeline 的
  ViewOverlayStage（BillboardFrameUBO）绘制——世界空间、深度测试走场景管线，不经过
  Render2D。Render2DList 删除的 makeWorldSprite/WorldQuad（屏像素尺寸 billboard、无深度
  测试）从来不是世界 2D 内容的正确载体；Unity 式"世界中可旋转、被遮挡、固定尺寸的 2D
  quad"的归属就是场景管线（Phase 4 World2D 同理），与 §9.5 四类 target 划分一致。
- Render2DList 的 makeSprite（vec3/mat4 两个重载）均为屏空间（mat4 是目标像素空间的
  任意变换，用于旋转的 UI 线/gizmo 轴线）；Render2D 里不存在世界空间的绘制入口。
  （2026-09-27 更正：后半句不成立，makeWorldLine/makeWireBox/makeWireSphere 是世界入口，见下一节。）

---

## 2026-09-27 — P2：CameraComponent 投影与正交

### 完成

- `ECameraProjection`（Perspective / Orthographic）+ `_orthoHalfHeight`（垂直半高）进入反射、序列化和 Lua。
- `getProjection(float outputAspect)` 是唯一投影入口。`_fixedAspectRatio` 为真时用组件保存的 aspect，否则用调用方传入的 View 输出 aspect。无参 `getProjection()` 删除。
- `getFreeView` / `getOrbitView` / `getViewProjection` / `getOrbitViewProjection` 删除。`getOrbitView` 写 Transform 的路径随之消失。view 由 `cameraViewFromOwner` 从 owner 世界姿态计算，`SceneCameraQuery::cameraView` 是应用侧入口，不写 Transform。
- Runtime / Editor producer 在声明 View 时传入 output aspect。`syncRuntimeCameraAspect` 以及两个 controller 把窗口 extent 写进组件的代码删除。
- Billboard 世界尺寸改为从该 View 的投影矩阵读取：透视乘 `tan(fovY/2)`，正交与距离无关（`BillboardScale.h`）。
- `worldDirection` 仍未参与朝向（billboard 永远正对相机）；“可侧看的牌”留给 Sprite2DComponent。

### 未做

- 编辑器正交 XY 工具 profile（plan §5 工作项 6 的编辑器面）留在 P5，避免把 `Mode2D` 同时变成 UI canvas 和 World2D。

### 验证

- `xmake b ya-testing`
- `xmake r ya-testing --gtest_filter='*Camera*:*EditorView*:*SceneSerializer*'`：36 passed。

### 下一步

- D1：按坐标系拆 draw list，删除全局 `Render2D`。

## 2026-09-27 — 2D draw 语义与持有者 review（仅改计划文档）

### 结论（写入 plan.md §0/§1/§2.4/§4A、P0-contract-matrix.md、feature_matrix.json）

- 分层：上传机制共用；shader 按图元语义拆分（已完成）；draw list 按坐标系拆分为
  `ScreenDrawList` / `WorldDrawList`；时序按 pass owner 拆分（场景 sprite → View overlay 世界相位 →
  屏幕相位 → 屏幕 compose）。判据：命令能否不知道相机就被正确画出。
- 删除全局 `Render2D`：`ScreenDrawPipelines` / `WorldDrawPipelines` 归持有 `IRender` 的一方，
  `ScreenDrawRecorder` / `WorldDrawRecorder` 归目标 owner，取代 pass slot。GUIApp 只创建屏幕层，
  不链接 Render3D；ya::App 的共享层放 App 设备状态，不放 `PipelineCoordinator`。
- GUI 模块只见屏幕类型；编辑器 View overlay 从 GUI Compose 的 `EditorViewportCompose` 移到 GameEditor。
- 不建通用 World2DList：世界空间即时绘制（`WorldDrawList`）、场景 sprite（snapshot candidate）、Billboard（ViewOverlayStage）
  三者生命周期与画面位置不同。
- 屏幕空间补 stroke / path（任意三角形 + 几何羽化），不用 GPU line 拓扑。
- Billboard：像素尺寸公式隐含 90° FOV、正交需改公式、`worldDirection` 未被使用，随 P2 修正/登记。
- 命名：不用 `Device` 后缀与 `F` 前缀。世界侧初稿 `WorldAnnotation*` 绑定了用途，改为只表达坐标系的
  `WorldDraw{List,Pipelines,Recorder}`，与 `ScreenDraw{List,Pipelines,Recorder}` 对称（用户确认）。

### 更正

- 早先称切换 Forward/Deferred 会 destroy `Render2D`，不成立：`PipelineCoordinator` 只在 `shutdown()`
  destroy。问题是 init owner 由先到者决定、设备级资源挂在 Render3D 管线协调器下、`composePassSlot`
  藏有 static slot 池。
- Phase 1“无剩余必做项”不成立：shader 拆分完成，但 draw list / 录制上下文 / compose owner 仍混装，
  由新增 D1–D3 承接，不重新打开 P1。

### 范围调整

- 分屏：不在本计划范围。
- 多窗口合并提交（AB4-2d）：延后到撕出视口需要第二扇窗画世界时；`FPresentSync` 已可拼同步对，
  剩余是应用侧 per-surface display plan 与额外窗口 tick 顺序。

### 状态

- 未改运行时代码，未运行 build/test；不构成代码 checkpoint。
- 待用户拍板：D2 的 View overlay 放 tone map 前还是后（建议后）。
- 下一步：P2（CameraComponent 正交）或 D1，二者无依赖。
