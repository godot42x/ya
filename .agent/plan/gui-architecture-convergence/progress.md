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
