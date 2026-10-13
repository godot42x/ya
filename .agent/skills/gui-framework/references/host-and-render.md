## Render2D pass slot

- `Render2D` 不认识 game/editor pass；调用方经 `Render2D::acquirePassSlot()` 获取不透明
  `Render2DPassSlot`（每进程静态递增，上限 `FQuadRender::kMaxPassSlots = 8`），资源按 slot
  懒分配（GUI app 只用自己需要的 slot）。
- 映射位置：`Render2DComposePass::composePassSlot(kind)`（compose kind 各占一个 slot）、
  `RenderOverlay::viewportOverlayPassSlot()`。
- 管线 prep 必须在录制前：depthless（depthFormat==Undefined → uiPipeline）与 depth 变体
  （screenPipeline）按目标附件格式缓存。**运行时 UI 复合管线必须在首帧 world 渲染前 prep**
  （display image 在帧图执行期才创建；见 memory：first-frame prep 时序坑）。
- 单帧多批次 flush 共享一块 host-visible 顶点缓冲：每批次写不同区域 + `vertexOffset` 定位，
  容量 `MaxVertexCount × kFrameFlushSlots`，超限 assert（见 memory：multi-flush 覆盖坑）。
  kind 切换也算一次该 backend 的 flush，连续同类仍合批。
- Session 是 state-change batcher：`pendingKind ∈ {None, ScreenQuad, WorldQuad, Line}`。
  `makeSprite` / `makeText` / `drawRoundedRect` / `makeRectFilledMultiColor` → ScreenQuad，`makeWorldSprite` → WorldQuad，
  `makeWorldLine` / wire → Line。kind 一变先 `flushPending()` 再切换，GPU draw 顺序 = emit
  顺序。禁止在 `end()` 里按 world→screen→line 固定倒空。clip 改栈 flush 的是 pending
  kind（三条 backend），不是只 flush screen quad。
- GUI snapshot 的 Line 仍 tessellate 成 sprite，不走 `FLineRender`。`FLineRender` 只用于
  对场景 depth 的 debug 线。2D 半透明叠放靠 painter’s algorithm，不要用 `pos.z` / depth
  解决 chrome 遮盖。
- clip 栈改动必须"先 flush 再改栈"；scissor 防御性 clamp 到窗口边界。



## GUI render surface

- `GUIRenderSurface`（`Runtime/Compose`）是 GUI compose target 的唯一资源边界：
  `createOffscreen()` 创建 Framework-owned `RenderTexture`，`wrapExternal()` 包装
  imported swapchain image；二者都经同一 `prepare()` / `record()` 调用
  `Render2DComposePass`。
- surface 只拥有/保留目标 image、format 与最终 layout；**不** pump event、acquire
  swapchain image、present 或访问 live WidgetTree。window/present 仍属于 host，
  tree/snapshot 仍属于 WidgetTree。
- 最终 layout 是 surface 的不变量：offscreen 默认 `ShaderReadOnlyOptimal`，
  swapchain surface 为 `PresentSrcKHR`（`isDisplayComposeTarget()`）。调用方不能通过 compose desc 把二者留在
  错误 layout。view compose 写 Camera/offscreen RT；display compose 才用 PresentSrcKHR。
- 替换/销毁 surface 必须发生在 frame boundary，且旧 command buffer 的 submit 已完成；
  command recording 仅消费不可变 snapshot 与当前 surface。
- `RuntimeUIOffscreen` 是和 `RuntimeUIComposite` 分离的 compose kind / pass slot：
  可在同一 command buffer 中把**同一 snapshot**录制到 windowed 和 offscreen target，
  不复用 vertex/descriptor frame resources。`GUIAppHost` 的
  `--gpu-shot` + `--offscreen-shot` + `--offscreen-diff` 是零容差 parity 门禁。
  Present 路径只持有 `IRenderSurfaceContext*`：用 `ISwapchain` 与
  `buildPresentationImages`，不要 `as<VulkanSwapChain>()`。
  Primary 与 extra 窗创建走同一套 API：`FGUIWindowHostConfig::renderAPI`（默认 Vulkan）；
  extra 在已有 `IRender` 时用 `render->getAPI()`。acquire 之后若 rebuild 了
  presentation targets，必须校验 `imageIndex` 仍落在 targets/cmdbufs 内，失败则
  empty-submit 本帧；不要对着过期 index 再 acquire 一次。
- `replayUIFrameSnapshot` 把 snapshot 画进**已经 begin 的 raster pass**（不
  `beginRendering` / 不转 layout）。WidgetTree chrome 走这条路径：presentation
  pass 已经打开，WidgetTree 覆盖整个 swapchain，3D viewport RT 只作为 `UIImage`
  采样。管线 prep 用 **swapchain format**，slot 用 `EditorToolSurface`。不要把
  `GUIRenderSurface::record()` 塞进 presentation（它会自己 begin pass）。



## Host（ya-gui-host）

- 顶层命名：`GUIApp` 是 standalone GUI 的装配层（当前一个 primary
  `GUIWindowHost`）；`GUIWindowHost` 是主窗一窗口一 tree / SDL window / presenter /
  pointer context 的真实 owner。extra 窗走 `GUIWindowManager`（实现
  `IGUIWindowCoordinator`）：每个 `GUIWindowSession` 拥有 NativeWindow +
  WidgetTree + snapshot + 可选 `IRenderSurfaceContext`。新代码只使用 `GUIApp` /
  `GUIWindowHost` / `IGUIWindowCoordinator`，不得新增或恢复
  `GUIAppHost` 兼容别名，也不得平行再造一套 window manager。
- Window chrome 走 `GUIWindowChrome` capability API（`EWindowChromeMode` =
  Native / Hybrid / ClientDrawn）。macOS 默认 Hybrid（full-size content + traffic
  lights / safe-area）。消费者用 `queryWindowChromeLayout` / `FWindowChromeLayout`，
  不要用生 `queryWindowChromeInsets`：SDL safe-area 常为 0，Hybrid 仍要 floor
  traffic-light 最小宽（78）和 title 高（28）。标题带里 **page TabBar 整条
  layout rect** 必须 `updateWindowChromeTitleClientHits` 为 Client；只有
  trailing drag gutter 是 Drag（macOS 上 Drag 映射为 `ENativeWindowHitResult::Normal`，
  由 Cocoa monitor 做 `performWindowDragWithEvent`）。不要把 tab 行空区当成
  窗口 caption，否则拖 tab 会整窗跟着走。macOS Hybrid 的透明 titlebar **不会**
  把 Drag 交给 AppKit 双击 zoom；`GUIWindowChrome` 在 Hybrid/ClientDrawn 上装
  Cocoa local monitor，对 **gutter Drag** 的双击执行 `zoom:`（尊重
  `AppleActionOnDoubleClick`）。Windows 走 HTCAPTION，不要再在宿主里 toggle。
  双击最大化只来自顶部 tab bar 的空条（`UITabBar::_onStripDoubleClick`）或
  gutter Drag（chrome API / Cocoa monitor）；tab 按钮和 dock leaf tab bar
  不得 zoom。菜单在 title 带下方，不要塞进 titleContent。
  `GUIWindowChrome.cpp` / `GUIWindowPlacement.cpp` 只谈 `INativeWindow` 与
  `Os::display*`；`SDL_*` 留在 `SDLNativeWindow` / `Core/Os`，AppKit 留在
  `GUIWindowChromeCocoa.mm`。`applyWindowChrome` 属于 GUI Host，resize/move
  后必须重 apply 才能更新 hit-test。Dock、EditorSurface、tab spawner 只读
  insets / layout，不得 include NSWindow / Win32 non-client API。完全 borderless
  必须显式请求。
- 生命周期：init → run（SDL event → WidgetTree dispatch → snapshot → compose → present）→
  shutdown。resize 只在帧边界重建 presentation 资源。
- `GUIHeadlessHost` 是同一 AppKernel/WidgetTree/delegate 合同的无窗口变体：只产生
  immutable `UIFrameSnapshot`（可由 callback 检查/落盘），不创建 SDL window、RHI、
  swapchain 或第二套 run loop。它用于 automation、结构断言与 windowed/offscreen
  交叉取证。
- 诊断：`--dump-snapshot=path --dump-frame=N`（CPU 侧 BMP 光栅化快照）、
  `--dump-snapshot-json=path --dump-frame=N`（snapshot 几何/clip/text JSON + digest）、
  `--gpu-shot=path --gpu-shot-frame=N`（GPU readback BMP）。内置纹理 resolver
  （`builtin/white|black|multipixel|checkerboard`）供 image 控件在无资产系统时使用。
- Glyph flush：`flushPendingGlyphs` 仍在 snapshot 之后、command recording 之前（Core Rule 6）。缺失字形的 layout/paint 由下一帧 `WidgetTree` 消费 `FontManager::resourceRevision()` 驱动，host 不要再 `invalidateSubtree`。Texture 就绪靠 bump `UIFrameBuildContext.generation`。
- **teardown 铁律**：任何持有 GPU 资源的成员（readback buffer、shader storage、widget tree、
  command buffers、presentation targets）必须在 `delete render`（VMA 销毁）前释放
  （见 memory：VMA teardown 顺序坑）。


