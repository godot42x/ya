# GUI 架构收敛进度记录

> 建立日期：2026-08-13
> 作用：记录每轮已经完成的内容、验证证据、遗留风险与下一步接力点。这个文件应该持续追加，不回写成静态总结。

## 2026-08-13 — 计划收口与执行工件补齐

### 本轮完成

- 重写 `plan.md`，把 GUI 主线从“继续堆 feature”收口为：单主循环、Widget/Layout/Slot、事件路径、共享 automation 基底、多窗口留口
- 将应用主链路明确为 `AppKernel -> GUIApp -> GUIWindowHost -> WidgetTree`
- 将 `Slot` 正式定义为 parent-child 边对象，方向参考 UE `GetSlot()`，但明确 parent-owned 生命周期
- 将 automation 正式上收到共享基底，要求 GUI / game / runtime editor 复用同一套事件注入与回归入口
- 在计划中增加 `Phase 0 — Rendering correctness gate`，避免后续 layout/interaction 问题与底层渲染正确性混淆
- 在计划中加入长期执行 harness：要求后续阶段性计划默认配套 `todo.md / progress.md / feature_matrix.json / session_checklist.md`
- 新增 `todo.md`，把当前 plan 展开成可执行事项
- 新增 `progress.md`，作为后续多轮接力记录入口

### 当前结论

- 当前最优先事项不是继续修 Workbench 页面细节，而是先压稳渲染正确性基线（Phase 0）
- 当前事件系统的主要问题不是单个 bug，而是缺少显式 `route path + route phase` 模型
- 当前布局系统的主要问题不是功能不够，而是 `UIContainer` 职责过重，缺少正式 `Layout / Slot` 对象模型

### 当前未完成/风险

- 还未创建 `feature_matrix.json` 与 `session_checklist.md`
- 还未把 `Phase 0` 的最小 render correctness 页面与 overlay 真正落地到代码
- 还未开始 `AppKernel / GUIApp / GUIWindowHost` 的实际代码收口，当前仍停留在计划与方法论层
- 当前 repo 中仍存在多层 App/Host/Surface/Panel 语义并存，尚未完成去重表

### 可供下一轮直接接力的起点

1. 先补 `feature_matrix.json` 和 `session_checklist.md`，把计划工件闭环补齐
2. 然后正式进入 `Phase 0`，先固定最小 workbench 渲染页面与 overlay 入口
3. 在 `Phase 0` 稳定后，再进入 `Phase A`，先做 owner / naming / automation 基底收口

### 本轮验证

- 文档级验证：已检查 `plan.md`、`todo.md`、`progress.md` 在同一目录下存在并互相一致
- 代码级验证：本轮未改 C++ 实现代码，仅修改计划工件

## 2026-08-13 — 执行工件闭环补齐，准备进入 Phase 0

### 本轮完成

- 新增 `feature_matrix.json`，把各 phase 的核心能力拆成可跟踪状态项
- 新增 `session_checklist.md`，明确每轮开工/收尾必须执行的步骤
- 更新 `plan.md`，将 `todo.md` 正式纳入最低执行工件要求
- 更新 `todo.md`，明确当前激活切片为：`Phase 0 / minimal_render_page + render_debug_overlay + single_frame_smoke`

### 当前结论

- 计划工件现在已经闭环，后续推进不再缺执行入口
- 当前下一刀不再是继续补文档，而是进入 `Phase 0` 的第一批代码落地

### 当前未完成/风险

- 还未真正进入 C++ 代码实现层，当前仍停留在计划与执行框架补齐阶段
- `Phase 0` 的 render correctness 页面、overlay、single-frame smoke 仍未落地

### 下一轮直接接力点

1. 固定最小 workbench 渲染页：静态文本 + button + image 占位
2. 建立最小渲染 debug overlay 入口
3. 建立单帧渲染 smoke 验证入口

### 本轮验证

- 文档级验证：已确认 `plan.md`、`todo.md`、`progress.md`、`feature_matrix.json`、`session_checklist.md` 共存且互相一致
- 代码级验证：本轮未改 C++ 实现代码，仅补齐计划工件

## 2026-08-13 — Phase 0 第一刀：Render 基线页 + smoke 起跑点

### 本轮完成

- 在 `Example/GUIWorkbench` 新增 `Render` 基线页，并将其注册为第一个 demo page
- Render 页固定了最小渲染观测组合：静态文本、三块彩色 marker、单个 image 占位、单个 probe button
- 更新 GUIWorkbench 自动化烟测，让 smoke 从 Render 页起跑，并断言首个 render probe click 生效
- 同步更新 `todo.md` 与 `feature_matrix.json`，把 `minimal_render_page` 与 `single_frame_smoke` 标为已落地

### 当前结论

- 现在已经有了一个比 Widgets/Layout 页更小、更适合盯底层渲染问题的基线页
- 后续无论是坐标翻转、文字异常、clip/scissor、resize 闪屏还是 image 绑定污染，都可以先在 Render 页复现/观察

### 当前未完成/风险

- 还未真正实现 framework 级 render debug overlay；当前仍是页面内视觉 marker，而非全局调试叠加层
- 还未重新跑构建/烟测验证本轮改动
- 还未处理历史上已有的 resize 崩溃、字体方向/clip 异常、Render2D flush/RT 污染等底层问题

### 下一轮直接接力点

1. 构建并运行 `GUIWorkbench --smoke-actions`，确认新增 Render 页与自动化路径都可通过
2. 若 smoke 通过，继续补 framework 级 render debug overlay
3. 若 smoke 不通过，优先在 Render 页上定位底层渲染正确性问题，再决定进入 overlay 还是 resize 稳定性修复

### 本轮验证

- 文档级验证：`plan/todo/progress/feature_matrix` 已同步更新
- 代码级验证：本轮已落地 C++ 代码，但尚未执行编译与运行验证

## 2026-08-13 — Phase 0 收尾：smoke PASS + host 侧 debug overlay 注入点

### 本轮完成

- 修正了 GUIWorkbench app 自动化与 shell 内置 Editor 自动化之间的帧序对齐；Render 页插入后，PASS 条件不再错位
- 实测 `xmake run GUIWorkbench --smoke-actions --exit-after-frame=120` 已输出 `GUIWorkbench smoke result: PASS`
- 在 `GUIAppHost` 增加了可选的 host-side debug overlay 开关：`--debug-render-overlay`
- 当前 overlay 先提供最小 render target 观测标记：窗口边界、中心十字线、左上原点块、中心 marker

### 当前结论

- Phase 0 现在已经有稳定 smoke 回归入口，不再依赖人工判断是否“看起来没挂”
- 对于坐标翻转、逻辑原点、present extent、窗口边界这类问题，现在有了 host 侧观察面
- clip bounds / batch boundaries 仍未进入 overlay；当前只是先把 render target 与坐标系 marker 固定下来

### 当前未完成/风险

- overlay 还没有可视化 clip 栈，也没有标记 Render2D flush/batch 分界
- overlay 目前通过修改 snapshot item 列表注入，虽然低风险，但还不是独立的 debug packet 层
- resize 崩溃、字体基线/方向异常、Render2D 多批次污染等底层问题还未开始逐项压实

### 下一轮直接接力点

1. 用 `--debug-render-overlay` 跑 Render 页，确认 overlay 与内容同向、不反转
2. 在此基础上补 clip bounds 可视化
3. 若 resize 仍不稳，优先转入 presentation target / swapchain / flush 生命周期排查

### 本轮验证

- 构建验证：`xmake b GUIWorkbench`
- 运行验证：`xmake run GUIWorkbench --smoke-actions --exit-after-frame=120` 日志已出现 `Workbench automation PASSED` 与 `GUIWorkbench smoke result: PASS`

## 2026-08-13 — Phase 0 继续推进：clip bounds overlay + plan 状态收口

### 本轮完成

- 将 host-side debug overlay 从“仅坐标/边界 marker”扩展为“坐标 + clip bounds”观测面
- overlay 现在会扫描 snapshot 中所有 `bClipped` draw item，并按唯一 clip rect 绘制彩色 outline
- 同步更新 `todo.md`、`feature_matrix.json`，把 `render_debug_overlay` 从 `planned` 收口到 `in_progress`

### 当前结论

- Phase 0 现在已经能同时观察：逻辑原点、窗口边界、中心线，以及 snapshot 解析后的 clip rect
- 后续排查 `scrollsplit` 内容覆盖、文本 clip、resize 后局部闪烁时，不必先猜 layout 还是 render，可先看 snapshot clip 是否已错

### 当前未完成/风险

- overlay 仍未暴露 Render2D batch flush 边界，因此还不能直接证伪“flush / RT 污染”假设
- overlay 仍是 snapshot 注入模式，不是独立 debug packet 管线
- 还未重新跑带 `--debug-render-overlay` 的运行验证来固化本轮证据

### 下一轮直接接力点

1. 运行 `GUIWorkbench --debug-render-overlay`，确认 Render 页 / ScrollSplit 页的 clip outline 与内容方向一致
2. 若仍怀疑 Render2D 污染，再补 batch boundary overlay 或 pass-local flush log
3. 继续逐项压稳 resize / text baseline / validation clean

### 本轮验证

- 代码级验证：已落地 clip bounds overlay 与计划状态更新
- 运行验证：本轮待执行 `xmake b GUIWorkbench` 与 smoke / overlay run

### 补充验证结果

- 构建验证：`xmake b GUIWorkbench` 通过
- overlay 运行验证：`xmake run GUIWorkbench --debug-render-overlay --exit-after-frame=3` 正常退出（exit 0）
- overlay 日志证据：Render 页 snapshot item count 为 50，边界 / 中心 / 原点 marker 正常注入；当前 Render 页 draw item 全部 `clipped=false`，说明 clip overlay 路径已接入，但仍需在 ScrollSplit/裁剪页面取证
- smoke 基线：沿用上一轮已确认的 `GUIWorkbench smoke result: PASS` 作为当前稳定基线；本轮未观察到新的 smoke 回归信号

## 2026-08-13 — Phase 0 继续推进：Render2D flush / scissor 诊断接线

### 本轮完成

- 在 `Render2D.cpp` 接通 session lifecycle 与 clip stack 日志：begin/end、pushClip/popClip 现在可记录 pass slot、extent、clip depth 与 clip rect
- 在 `QuadRender.cpp` 接通 screen/world flush 诊断，并加入 batch cursor 不变量断言：`cursorVertex == batchStartVertex + pendingVertexCount`
- 在 `Example/GUIWorkbench/main.cpp` 增加 `--debug-render2d-log` 与 `--debug-render2d-log-limit`，允许直接对 Workbench 页面采样 flush/scissor 证据
- 将当前激活切片从 `render_debug_overlay(clip) + smoke verification` 收口为 `render2d_flush_scissor_diagnostics`

### 当前结论

- ScrollSplit 的 CPU 侧 snapshot/clip 已基本取证，当前主要嫌疑已经收敛到 Render2D flush/scissor、compose/present 或资源生命周期层
- 后续判断不再依赖肉眼猜测，需要以 `--debug-render2d-log` + snapshot dump + GPU shot 三线交叉验证

### 当前未完成/风险

- 本轮尚未完成 build/run 取证；flush/scissor 诊断只是接线，尚未证明日志与 GPU 画面是否一致
- 若 cursor invariant 断言失败，说明问题在 batch 写入/flush 边界；若 invariant 与 scissor 都正确但 GPU 仍异常，问题将转入 compose/present/target lifecycle

### 下一轮直接接力点

1. `xmake b GUIWorkbench`
2. `xmake run GUIWorkbench --start-page=ScrollSplit --debug-render-overlay --debug-render2d-log --debug-render2d-log-limit=16 --exit-after-frame=3`
3. 再跑 `--gpu-shot` / `--dump-snapshot` 固化证据，并根据结果转入 scissor、batch 或 compose 路径

### 本轮验证

- 代码级验证：已完成 Render2D 日志与断言接线
- 运行验证：待执行构建与 ScrollSplit 取证

## 2026-08-13 — host 输入契约收口 + presentation readback 资源语义修复

### 本轮完成

- 收口 host 输入契约：SdlEventSource 不再依赖 smoke/脚本先发一条 move。新增
  bPointerKnown 状态，在 pollEvents() 先 SDL_PumpEvents()，当 host window
  已持有 mouse focus 且 pointer 未知时，用 SDL_GetMouseState 注入一条
  MouseMoveEvent 完成 pointer bootstrap，再标记为已知。
- 接入 SDL_EVENT_WINDOW_MOUSE_ENTER/LEAVE：进入时主动同步并注入
  MouseMoveEvent，离开时注入越界坐标 (-1000000,-1000000) 并清除 pointer
  状态，保证 hover 回落与首次点击一致性。
- pointer 语义事件（down/up/wheel）统一先同步当前坐标再发语义事件，避免用
  陈旧 lastMouse* 驱动。
- 修复 presentation readback 的 Vulkan validation：
  - SwapchainCreateInfo.bEnableTransferSrc = true（GUI app swapchain image
    现在创建时就带 VK_IMAGE_USAGE_TRANSFER_SRC_BIT）
  - GUIPresentationTarget import 的 usage 由仅 ColorAttachment 改为
    ColorAttachment | TransferSrc，与真实 image usage 一致
  - readback staging buffer 改为按 present extent 尺寸重建（resize 后不再
    复用旧尺寸 buffer）
- 跑通并固化 ScrollSplit start-page + scenario + resize + checkpoint + capture
  整条诊断链。

### 当前结论

- 之前"静止鼠标首击冲突"已在 host 正式路径解决，不再需要测试脚本 workaround。
- 之前 resize/scenario/capture 的 TRANSFER_SRC validation 脏报错是 host 资源
  契约缺失，不是测试写法问题；现已修复。

### 本轮验证

- xmake b GUIWorkbench 通过。
- resize scenario 运行：三份 checkpoint 落盘、GPU shot 写出 960x640 BMP、
  grep VUID 计数为 0（capture 路径 validation 干净）。
- xmake r GUIWorkbench --smoke-actions --exit-after-frame=30 仍 PASS。

### 当前未完成/风险

- 本轮只证伪了 capture/readback 的 TRANSFER_SRC validation；尚未覆盖全应用
  所有页面的 validation 全量门禁。
- resize 稳定性仍只到"scenario 能跑通 + capture 干净"，未证明长时间交互下
  无闪屏/无崩溃（尤其字体渲染与 Render2D 多批次问题仍未归因）。

## 2026-08-13 — Phase 0 收口：flush/scissor 取证与多尺寸 resize stress

### 本轮完成

- 修复 `GUIWorkbench` 对 `Render2D::debug` 的跨 DLL 直接数据访问，CLI 改经
  `Render2D::debugState()` 调整诊断状态，与编辑器诊断面统一走导出函数边界。
- 构建并运行 ScrollSplit 诊断：
  `xmake r GUIWorkbench --start-page=ScrollSplit --debug-render-overlay
  --debug-render2d-log --debug-render2d-log-limit=16 --exit-after-frame=3`
  同时落盘 CPU snapshot 与 GPU shot。
- GPU shot、CPU snapshot 与 session/clip/flush 日志三线一致：ScrollSplit 的
  clip 为 `(16, 134.125, 471.24, 605.875)`；单帧可见 16 个 screen flush，
  每一批均满足 `cursorVertex == startVertex + vertexCount`，且没有 assertion、
  error 或 VUID。
- 新增 `Example/GUIWorkbench/Scenarios/resize_scrollsplit_stress.jsonl`，连续切换
  `960x640 -> 1440x900 -> 800x600 -> 1280x800`，每次 resize 后均等待渲染并写
  checkpoint。
- 用该 scenario 分别覆盖 ScrollSplit 与最小 Render correctness 页；两次运行均
  成功输出五份 checkpoint 和最终 BMP，四次 swapchain recreation 后画面仍完整。
- `xmake b GUIWorkbench` 通过；`xmake r GUIWorkbench --smoke-actions
  --exit-after-frame=30` 通过。

### 当前结论

- 当前 windowed GUI 基线路径中，ScrollSplit 的可见异常不再由 scissor、批次 vertex
  cursor、当前 compose/present 路径或 Render2D 多 flush 覆盖引起；这些假设已经被
  GPU shot 与日志证据证伪。
- Render 与 ScrollSplit 在当前 Vulkan/Windows 环境下均保持左上角原点、可读文字、
  正确 baseline 和 clip 行为，并跨四种窗口尺寸完成资源重建。
- 这不是完整的长期稳定性证明：尚未建立 headless/offscreen 对照，亦未跑持续交互的
  flicker/validation 门禁。

### 下一轮直接接力点

1. 推进 `presentation_path_consistency`：盘点并选择现有可运行的 windowed、
   offscreen、headless 三条入口，先定义同帧 capture/diff 的最小共同证据。
2. 若没有可复用 headless/offscreen host，优先暴露现有 presenter/target 能力；不要
   复制 GUIAppHost 或另起平行 frame loop。
3. 将多尺寸 resize scenario 接入后续自动化门禁，并补持续 resize + pointer/scroll
   交互案例。

## 2026-08-13 — 修复 GUI host scenario frame step 语义

### 根因与修复

- 在多尺寸 scenario 取证时发现 `GuiScenarioStep::frame` 的计数只在通用
  `GuiScenarioExecutor` 中被消费；`GUIAppHost` 私有的 `ScenarioEventSource`
  每次只渲染一个 tick，错误忽略了 `frame:N` 的 N。
- 这会让 GUI scenario 和共享 automation 语义分叉，也会使 resize 后的“等待 N 帧”
  形同虚设。修复放在 GUI host 的共享事件源而非每份 scenario 展开重复 frame 行：
  `remainingFrames` 在后续 poll 中逐 tick 递减，最后一个 frame 才触发 final capture /
  graceful exit。

### 验证

- `xmake b GUIWorkbench` 通过。
- 重新运行 `resize_scrollsplit_stress.jsonl`：五个 checkpoint 均落盘，四次 resize
  后最终 `1280x800` capture 落盘，日志中 VUID/error/assert 计数均为 0。
- `xmake r GUIWorkbench --smoke-actions --exit-after-frame=30` 通过。

### 当前限制

- `frame:N` 现在精确表示 N 个 `AppKernel` tick；swapchain recreate / acquire 暂时跳过
  present 时，该 tick 仍按 kernel 语义推进。若后续需要“至少 N 张已 present 帧”的
  automation 合同，应单独增加 present-ack wait condition，而不是重新解释 `frame`。

## 2026-08-13 — presentation path 调查结论

- windowed 基线路径是 `GUIAppHost -> GUIPresentationTarget(imported swapchain image)
  -> Render2DComposePass(RuntimeUIComposite) -> readback BMP`。
- 现有真正 offscreen compose 在 Product Editor：`EditorToolSurfaceCompositor` 复用
  `Render2DComposePass(EditorToolSurface)`，但它绑定 editor 模块与 editor-owned
  `RenderImage`，不能把它当作 GUI framework 的 headless host。
- 在 `Framework/GUI` + `Example/GUIWorkbench` closure 中未发现可复用的 GUI headless
  host。因此下一步不能复制 `GUIAppHost` 或把 editor compositor 下沉为临时替代；
  应先从二者抽出一个 owner 明确、单帧录制期可持有资源的 presenter/surface 边界，
  再让 windowed/offscreen/headless 接入同一 compose + capture 语义。

## 2026-08-13 — GUIRenderSurface：windowed / offscreen compose target 收口

### 本轮完成

- 新增 `Runtime/Compose/GUIRenderSurface`：
  - `createOffscreen()` 创建 Framework-owned color attachment + sampled + transfer-src
    `RenderImage`，默认收口到 `ShaderReadOnlyOptimal`；
  - `wrapExternal()` 包装 imported swapchain image，收口到 `PresentSrcKHR`；
  - `prepare()` / `record()` 统一走 `Render2DComposePass`，surface 自己钳制最终 layout。
- `GUIPresentationTarget` 现在持有外部 `GUIRenderSurface`，`GUIAppHost` 通过它
  prepare/record windowed snapshot，readback 后也恢复 surface 的 layout，不再在 host
  中硬编码 compose 的 final layout。
- Editor 的 `EditorToolSurfaceCompositor` 改用 Framework-owned offscreen
  `GUIRenderSurface`，仍由 editor 在 detach 前释放，保留现有 GPU teardown 顺序。
- 修正 `Engine/Test/Source/GuiEventDriverTest.cpp`：shared scenario driver 的 drag
  已遵守 pointer bootstrap（initial MouseMove），测试此前仍断言旧的六事件序列。

### 验证

- `xmake b GUIWorkbench` 通过。
- `xmake b ya-editor` 通过。
- `xmake r GUIWorkbench --smoke-actions --exit-after-frame=30` 通过。
- `xmake b ya-gui-closure-test && xmake r ya-gui-closure-test`：91/91 通过。
- `python Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject
  -- --exit-after-frame=30` 正常退出；日志含 624x382 GUIWorkbench snapshot 的 offscreen
  compose 证据，且 VUID/error/assert 计数为 0。

### 当前结论

- Windowed swapchain 与 editor offscreen target 不再各自持有平行的 RenderImage /
  final-layout / compose 调用链；它们已在 GUI closure 的 `GUIRenderSurface` 收口。
- 仍缺无 native window 的 headless snapshot surface；下一步应在 `AppKernel` 上复用
  同一 frame/automation 语义，只生成 immutable snapshot / dump，不创建第二套 run loop。

## 2026-08-13 — GUIHeadlessHost：无窗口 snapshot 路径

### 本轮完成

- 抽出 `IGUIAppDelegate` 到独立的 `GUIAppDelegate.h`，避免 windowed/headless host
  通过彼此的 host header 耦合；delegate 只管理 build/update/event observation，
  host 管理 WidgetTree 与 frame lifetime。
- 新增 `GUIHeadlessHost`：通过 `AppKernel` 驱动 event source、tick、automation exit；
  每 tick 调 delegate update 后构建 immutable `UIFrameSnapshot` 并交给 callback。
  它不初始化 SDL、RHI 或 swapchain。
- 新增 `ya-gui-headless-host-test`，覆盖无窗口三 tick、scripted resize、snapshot
  callback、layout draw item 与 AppKernel `exitAfterFrame` 合同。

### 验证

- `xmake b ya-gui-headless-host-test && xmake r ya-gui-headless-host-test`：1/1 通过。
- `xmake b ya-gui-closure-test && xmake r ya-gui-closure-test`：91/91 通过。

### 当前结论

- GUI 现在有三条 owner 明确的表面路径：
  1. windowed：`GUIAppHost + GUIPresentationTarget + GUIRenderSurface(PresentSrcKHR)`；
  2. offscreen：`GUIRenderSurface(ShaderReadOnlyOptimal)`；
  3. headless：`GUIHeadlessHost + AppKernel + UIFrameSnapshot`。
- 三条路径已经共享 delegate/tree/snapshot 或 surface/compose 合同，但还没有把同一
  Workbench 页面输出成可比较的 snapshot digest + GPU/offscreen golden；下一刀应补
  这个证据，而不是继续增加 host 类型。

## 2026-08-13 — snapshot structural dump / digest

- 新增 `dumpUIFrameSnapshot()` 与 `digestUIFrameSnapshot()`：输出稳定的 item order、
  geometry、color、clip、text、scale，刻意排除 process-local texture/font 指针。
- `GUIAppHost` / `GUIWorkbench` 新增 `--dump-snapshot-json=path`，与
  `--dump-frame` 共用 frame 选择；日志输出 digest，方便把 windowed GPU shot 与
  immutable snapshot 绑定到同一帧。
- ScrollSplit windowed 验证：1280x800、82 items、48 clipped items、digest
  `2131495881988212631`，并同时生成 GPU shot；VUID/error/assert 均为 0。
- `UIFrameSnapshotTest.StructuralDumpAndDigestTrackVisualPacketOnly` 已覆盖不变
  packet 得到相同 digest、几何改动得到不同 digest。

## 2026-08-13 — Phase 0 三路径同帧收口

- `GUIWorkbench --headless` 现在复用 `FWorkbenchApp + GUIHeadlessHost`，通过
  synthetic RuntimeDefault fonts 构建无窗口 snapshot；它不初始化 SDL/Vulkan。
- ScrollSplit 的同一初始页面在 windowed 和 headless 路径生成**完全相同**的 JSON：
  `1280x800`、82 draw items、48 clipped items、structural digest
  `2131495881988212631`、semantic digest `4645728999760259413`。
- 新增 `RuntimeUIOffscreen` compose kind，为同 command buffer 内的 windowed +
  offscreen mirror 分配独立 Render2D pass slot，避免共享 vertex/descriptor resources。
- `GUIAppHost` 支持 `--offscreen-shot` / `--offscreen-shot-frame` /
  `--offscreen-diff`。ScrollSplit 同帧 windowed GPU shot 与 Framework-owned
  offscreen `GUIRenderSurface` BMP 的 SHA-256 相同，host 零容差 diff PASS，VUID/error/
  assertion 均为 0。
- 新增 `resize_scrollsplit_interaction_stress.jsonl`：在四次 resize 间加入 divider
  drag 与 wheel scroll，作为下一步稳定性门禁。

## 2026-08-13 — Phase 0 rendering correctness baseline 关闭

### 最终验证

- `resize_scrollsplit_interaction_stress.jsonl` 已执行：六份 checkpoint、四次
  swapchain recreation、divider width 从初始约 471px 持续变到 531px / 571px，
  wheel 后列表首项从 entry 1 移到 entry 2，最终 1280x800 BMP 可读且完整；
  VUID/error/assert 均为 0。
- GUIWorkbench 8 个 start page（Render / Widgets / Layout / Menus / DragDrop /
  Modal / ScrollSplit / Editor）各运行两帧，全部 exit 0、0 VUID/error/assert。
- `ya-gui-closure-test` 为 92/92 PASS；`ya-gui-headless-host-test` 1/1 PASS；
  GUIWorkbench end-to-end smoke PASS；`ya-editor` build PASS。

### Phase 0 结论

- 最小页面 resize、坐标/文字/clip、multi-flush、windowed/headless/offscreen 同帧
  一致性、GUIWorkbench 页面级 Vulkan validation 已满足当前阶段完成标准。
- 不将这误写为跨平台/全产品长期稳定性证明：MoltenVK 与全产品 validation 仍是后续
  扩大门禁时的工作，不阻塞进入 Phase A owner 收口。

### 下一轮直接接力点

1. 产出 Phase A owner chain 文档：`AppKernel -> GUIApp -> GUIWindowHost ->
   WidgetTree`，并将当前类型映射为保留/改名/过渡/删除。
2. 不在文档之后立即造空接口；先从现有 `GUIAppHost` 拆出真实 window owner
   boundary，保持 AppKernel 仍是唯一 run loop。

## 2026-08-13 — Phase A owner chain / unified loop

### 本轮完成

- 新增 `owner-model.md`：职责图、现状/目标类型映射、state owner、生命周期清单、
  automation placement 和多窗口默认语义均落盘。
- `Product/Host/AppFrameLoop::run()` 不再拥有 native while loop；新增
  `HostSdlEventSource + HostAppKernelDelegate`，由 `AppKernel` 统一执行 SDL event
  source -> product tick -> close policy。高级 scene-stability / screenshot / RenderDoc
  automation 仍保留为 Product Host extension，避免错误下沉到 GUI/core base。
- `MessageBus::publishEvent(const Event&)` 补齐 runtime-typed event bridge，避免
  AppKernel event source 的 base `Event&` 丢失 concrete subscriber 类型。
- standalone GUI 真实 one-window owner 已命名为 `GUIWindowHost`；新增 `GUIApp`
  primary-window assembly 来创建 `AppKernel`。`GUIAppHost` / `FGUIAppHostConfig`
  仅作为兼容 alias，GUIWorkbench 和 GUIFrameworkSmoke 已切到新名称。

### 验证

- `xmake b ya-host` 通过。
- `python Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject --
  --exit-after-frame=8 --log-level=warn` 正常退出。
- `python Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject --
  --exit-after-frame=30 --log-level=warn` 正常退出。注：8-frame editor exit 仍会触发
  既有异步资源 teardown 的 VMA 断言；30-frame run 通过，属于产品初始化 warmup 现有
  稳定性门槛，不归因于 AppKernel loop adapter。
- `xmake b GUIWorkbench`、GUIWorkbench smoke 与 `ya-gui-minimal-host
  --exit-after-frame=8` 均通过。
- `AppKernelTest.RuntimeTypedEventBridgePublishesConcreteEvent` 通过。

## 2026-08-13 — Phase B Layout / Slot kernel closed

### 本轮完成

- 新增 `Runtime/Layout/UILayout`：`UILayout` 是 parent-owned layout algorithm
  合同，提供 `measure/arrange` 与 owner-tree invalidation；`UISlot` 是
  parent-owned parent-child edge，不存入 child 本体。
- 新增 `UIBoxLayout / UIBoxSlot`：box 参数从 `UIContainer` 移出；slot 负责
  `Auto/Fill`、weight、margin、cross alignment、min/max/preferred size 与
  participation。Fill 按 weight 分配主轴余量并停在 max size；Hidden child 的
  layout-space 保留可由 slot 显式关闭。
- `UIContainer` 已退化为 layout host，只持有一个 `UIBoxLayout`；公开访问面为
  `getBoxLayout()`、child `getSlot()` 与 parent `getBoxSlot()`。未恢复旧的
  reflected container box fields，也未为 UIDocument 增加过渡兼容层。
- `WidgetTree` 的 attach/reparent/detach 与 detached-subtree authoring 全部经
  parallel child-edge storage 创建/移除 slot。reparent 销毁旧 edge，并在目标 parent
  创建默认 slot；不得跨越 reparent/detach 缓存 slot 裸指针。
- `WidgetTreeDump` 现在输出 container `layout` 与 child `slot`。Workbench
  ScrollSplit 已以 `UIBoxSlot::Fill` 表达原 stretch-last 语义。
- 新增 layout regression：nested box 的 edge-local state 在 reparent 后不会泄漏到
  destination slot；Hidden participation 与 capped Fill 均有断言。

### 验证

- `xmake r ya-gui-closure-test`：97/97 PASS（包含 15 个 `WidgetLayoutTest`）。
- `xmake b GUIWorkbench && xmake r GUIWorkbench --smoke-actions --exit-after-frame=30`：
  PASS。
- `resize_scrollsplit_interaction_stress.jsonl`：6 个 JSON checkpoint 与最终 BMP；
  exit 0，日志 0 VUID / error / assert。
- 全局旧 UIContainer box-field 引用检查仅保留 `UISplitPane::_padding`；
  它是 Phase E 的 specialized layout state，不是旧 UIContainer box 状态。

### Phase B 结论与下一步

- Box layout 已从 UIContainer 的字段/算法混合体收敛为 layout host + edge-owned slot。
  后续新增通用 box 行为不需要再向 UIContainer 增加布局字段。
- 目前不把 split/scroll 强塞进 box 内核；它们进入 Phase E specialized layout。
  下一切片进入 Phase C：先对象化 pointer/focus path，并分离 hit-test discovery 与
  preview → target → bubble route execution。

## 2026-08-13 — Phase C route-path foundation

### 本轮完成

- `WidgetTree` 新增 tree-owned `WidgetPointerState`、pointer path、focus path 与
  `WidgetRouteTrace`。pointer event 更新持续 logical point；trace 以 widget name
  保存，detach 后仍可安全写 dump。
- route trace 可区分 `HitTest`、`PointerCapture`、`Focus`、`TabTraversal` 与
  `DragSession`；capture/focus 都输出 root -> layer -> target 的显式 path。
- `WidgetTreeDump` 增加 `pointer`、`focusPath`、`lastRoute` 字段，自动化可直接
  检查 target/path，而不必从 hover/focus 裸状态反推。
- `UIElement` 增加 Preview / Target / Bubble 的 route-phase contract：target 保持
  现有 `handleInputEvent()`，preview 默认 passive，bubble 先兼容转发给已有 handler。
  这只建立稳定扩展面，尚未切换 dispatch 的实际 delivery 顺序。
- 新增 `WidgetTreeTest.RouteStateTracksPointerCaptureAndFocusPaths`，覆盖 nested
  pointer hit path、capture path、focus path 与 tree dump 证据。

### 验证

- `xmake r ya-gui-closure-test`：98/98 PASS。
- `xmake b GUIWorkbench && xmake r GUIWorkbench --smoke-actions --exit-after-frame=30`：
  exit 0。

### 下一切片

- 用现有 path trace 替换 `dispatchSubtree()` 的执行模型：preview/tunnel ->
  target -> bubble 必须同时保留 Pass/Stop、nested scroll bubbling、capture 和
  detach-safe route lifetime；在这之前不把 route-phase hook 当作已完成特性。

## 2026-08-13 — Phase C explicit routed delivery

### 本轮完成

- 删除 `dispatchSubtree()` 的隐式 DFS delivery；pointer event 先以
  `collectHitTargetsSubtree()` 收集 topmost candidate，再由 `dispatchRoute()`
  执行 Preview -> Target -> Bubble。
- `Stop` 在任一步骤短路；`Pass` 保留 handled 状态并继续 lower candidate，
  因而透明 overlay 与既有 Pass/Stop 语义不回归。
- route 在回调期间持有 path 的 shared references，并在每一步重查 tree membership；
  popup/modal target 可以在 handler 内 `detach/close()`，后续阶段不会解引用死对象。
- `Focus` route 保持 focus-owner 独占语义；`PointerCapture` 使用相同 route executor
  并设置 `bViaCapture`；popup/modal 通过 `UIPopupOverlay` ancestor 分类到 trace policy。
- 修复 attached parent 的 `addDetachedChild()`：新 child 立即继承所属 `WidgetTree`
  membership。菜单栏在 attached 后添加 entry 的路径现在与一般 builder 一致。
- `WidgetRouteTrace` / dump 现在记录 policy、target/path、每个 phase 的 widget、
  handled、hit filter 和最终 result（同时提供机器可读 enum 与字符串名）。
- 新增 `event-routing.md`，定义 pointer/focus/capture/popup/modal/drag 与
  button/scroll/split/menu 的最小语义表。

### 验证

- `xmake r ya-gui-closure-test`：99/99 PASS。
- 重点覆盖：explicit phase order、Pass overlay、nested scroll bubble、focus route、
  pointer capture、menu hover/switch、popup shield、modal target detach、attached-parent
  late child membership。
- `xmake b GUIWorkbench && xmake r GUIWorkbench --smoke-actions --exit-after-frame=30`：
  exit 0，0 VUID/error/assert。

### Phase C closure

- `--debug-render-overlay` 已把 tree-owned pointer/focus path、hover、capture 和
  pointer marker 转换为 snapshot debug items；它不在 command recording 时读取 live tree。
  Menus 页 GPU capture 已人工检查，坐标/clip overlay 与新增 route overlay 均正常。
- Phase C 的 pointer/focus path、explicit delivery、policy、semantic table、route dump、
  path assertions 和 visual overlay 均满足当前阶段验收。下一阶段转入 Phase D：
  Workbench 页面按 feature gallery / regression app 分层，并补每页 scenario + golden。

## 2026-08-13 — Phase D Workbench regression taxonomy

- 新增 `workbench-regression-matrix.md`：将现有 Workbench 页面明确划为
  Render/debug、basic interaction、layout、tree/property reference，并列出每页的
  当前 smoke/closure/resize 证据与下一条 scenario+golden 工件。
- 明确 ScrollSplit interaction stress 继续作为高价值动态门禁，不退化为单张静态图；
  生成 capture/diff 保持在 ignored diagnostics 目录，example source 只保存 scenario。
- 新增 `menus_popup_interaction.jsonl`：File open -> Edit hover switch -> popup shield
  dismiss；运行产出 `file_menu_open` / `edit_menu_open` / `popup_dismissed` 三份结构
  checkpoint 与初始 BMP golden，exit 0，0 VUID/error/assert。

## 2026-08-13 — Scenario structural assertions + Widgets regression

- GUI scenario JSONL 现在支持 `{"assert": {...}}`。Foundation 仅传递 opaque JSON
  assertion payload；GUI host 以 `dumpWidgetTree()` 进行部分对象匹配，支持 named
  widget selector 与 tree-level route assertion。失败会记录清晰 path 并令 host 返回
  non-zero。
- `WidgetTreeDump` 增补 control-state packet：button、checkbox、slider、combo、
  text field、scroll viewport、split pane、popup overlay。场景不再只能从截图猜交互结果。
- 新增 `widgets_interaction.jsonl`，固定 960x640 后依次验证 Counter release、
  CheckA toggle、Brightness slider capture、combo popup + OpenGL selection、Notes
  focus/text/Enter commit。9 条 host assertion、6 份 checkpoint 与初始 BMP golden 全部通过。
- `menus_popup_interaction.jsonl` 也升级为 4 条 host assertion：popup 类型、hover
  switch 的 Popup route、shield dismiss route。

## 2026-08-13 — Phase D Workbench regression matrix closed

- 完成 8 页 scenario 覆盖：
  `render_probe_interaction`、`widgets_interaction`、`layout_spacing_interaction`、
  `menus_popup_interaction`、`dragdrop_interaction`、`modal_interaction`、
  `resize_scrollsplit_interaction_stress`、`editor_inspector_interaction`。
- Layout spacing slider 之前只更新 demo state，未驱动 live `UIBoxLayout`。修复为通过
  weak layout-host reference 调 `setSpacing()`，scenario 现在断言 live layout spacing
  真正改变。
- GUIWorkbench 配置 `bEscapeQuits=false`：Escape 归 focused popup/modal 的 route
  语义，而不是被 host 提前当作 app quit。Modal scenario 因而完成 open -> focus ->
  Escape dismiss 并写出 final capture。
- 每页均运行 host assertions + checkpoint JSON + baseline BMP capture；第二次通过
  `--scenario-golden` / `--scenario-diff` 进行了 zero-tolerance visual diff，8/8
  exit 0。矩阵详情见 `workbench-regression-matrix.md`。

## 2026-08-13 — Phase E/F specialized layouts and multi-window boundary

- 新增 `UISingleChildLayout`，`UIButton` 不再持有 content padding/child arrangement
  几何逻辑；padding、measure、inset assigned rect 都归 layout。
- 新增 `UISplitLayout`，`UISplitPane` 只保留 divider paint 和 drag transient state；
  orientation/ratio/min extent/divider thickness/padding/first-two-child arrange 全部进入
  layout。ratio setter 在已有 content rect 时即时按 min extent clamp。
- 新增 `UIScrollLayout`，`UIScrollViewport` 只保留 clip + wheel route host 职责；
  axis/offset/step/maxOffset/first-child arrangement 进入 layout，边界 wheel 继续 bubble。
- tree dump 现在统一输出 `box` / `singleChild` / `split` / `scroll` layout packet；
  新增 `ToolControlsTest.SpecializedLayoutsAppearInTreeDump`。
- 新增 `specialized-layouts.md` 与 `multiwindow-semantics.md`，明确 menu/tree/inspector
  仍 composition-first，以及 GUIApp/GUIWindowHost/WidgetTree 的 active window、
  cross-window drag、modal scope、focus path 默认语义。

### 验证

- `xmake r ya-gui-closure-test`：103/103 PASS。
- `GUIWorkbench ScrollSplit resize + drag + wheel scenario`：exit 0，host assertions
  通过，0 VUID/error/assert；与既有 zero-diff baseline 对比 PASS。
