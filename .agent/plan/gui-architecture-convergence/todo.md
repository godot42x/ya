# GUI 架构收敛 TODO

> 更新时间：2026-08-14
> 作用：把 `plan.md` 里的阶段目标展开成当前可执行事项。这个文件优先服务日常推进，要求持续收口，不保留已经失效的行动项。

## 当前优先级

当前默认只允许同时推进一个 phase 的一个最小切片。若工作区中已经存在未完成切片，先收口再开新项。

当前激活切片：`phase-a physical tree + app subtree split + rename/move design`

当前审计工件：`owner-model.md` + `plan.md` 目录归位表（macOS/MoltenVK gate 保持外部阻塞，不再占用当前主切片）

## P0 — Phase 0 渲染正确性基线

### 进行中

- [x] 固定一个最小 workbench 页面：静态文本 + 单个 button + 单个 image 占位，作为 render correctness 页面
- [x] 补最小渲染 debug overlay：坐标轴 / render target 标记 / clip bounds 已与 ScrollSplit flush/scissor / GPU shot 联合验证
- [x] 建立单帧渲染 smoke 入口，支持 agent 无手操验证一帧结果
- [x] 固定 ScrollSplit snapshot 取证：CPU 侧标题/副标题/divider/左右 pane clip 坐标已落盘，可作为 Render2D 排障基线
- [x] 修复 `--start-page=Editor` 诊断入口顺序问题，确保内建 Editor 页可直接起跑
- [x] 接通并验证 Render2D session/clip/flush 诊断日志：ScrollSplit GPU shot、snapshot 与 flush/scissor 日志一致
- [x] host pointer bootstrap：静止鼠标首击一致性由 SdlEventSource 正式路径保证（mouse focus + SDL_GetMouseState + enter/leave）
- [x] scenario resize host path：start-page + resize + checkpoint + capture 已跑通
- [x] 修复 presentation readback image usage：swapchain bEnableTransferSrc + import usage 加 TransferSrc，resize scenario 0 VUID
- [x] 收口 GUI compose target：`GUIRenderSurface` 同时覆盖 standalone swapchain import 与 editor tool offscreen image
- [x] 修复 GUI scenario `frame:N`：GUIAppHost 与共享 GuiScenarioExecutor 均按 N 个 tick 解释 frame step

### 待做

- [x] 压稳 resize：Render 与 ScrollSplit 均通过 960x640、1440x900、800x600、1280x800；ScrollSplit resize + divider drag + wheel stress 产出六份 checkpoint 和最终 capture，0 VUID/error/assert
- [x] 压稳坐标：ScrollSplit windowed / headless structural JSON 完全一致，且 windowed/offscreen BMP 零差异；逻辑坐标保持左上角原点
- [x] 压稳文本：ScrollSplit windowed / headless 82-item snapshot JSON 完全一致，windowed/offscreen BMP 零差异；文字方向、baseline、clip/scissor 当前基线正确
- [x] 压稳 Render2D 多批次 flush：ScrollSplit 单帧 16 个 screen flush 的 cursor/scissor 日志与 GPU shot 一致，无断言或污染
- [x] 用 GPU shot + flush/scissor 日志判定问题归属：当前 ScrollSplit 样本已排除 scissor / batch cursor / 当前 present 路径污染
- [x] 建立 windowed/headless/offscreen 三路径同帧一致性检查：ScrollSplit windowed/headless snapshot JSON 完全相同（82 items / 48 clipped / structural+semantic digest 相同），同帧 windowed/offscreen BMP SHA-256 相同且 host zero-tolerance diff PASS
- [x] 确认当前 Windows Vulkan validation 零错误：8 页 scenario/golden matrix、resize interaction stress、capture/readback、minimal host、runtime/editor smoke 均为 0 VUID/error/assert
- [~] 运行 macOS / MoltenVK validation gate：当前 Windows runner 无 macOS/MoltenVK runtime；`Script/gui_convergence_macos_validation.py` 已补齐 closure/headless/minimal/page-matrix/snapshot parity 覆盖，待 macOS runner 实际执行并把证据回填到 `progress.md`

### 完成标准

- [x] 最小页面在 resize 后稳定：Render 四尺寸 stress 和 ScrollSplit 四尺寸 resize + divider drag + wheel stress 均通过
- [x] 至少一张 overlay 截图或 dump 证明坐标/clip/batch 正确（ScrollSplit GPU shot + snapshot + flush/scissor log）
- [x] 至少一条自动化 smoke 可重复运行

## P1 — Phase A 主链路与 owner 收口

### 进行中

- [x] 产出 `AppKernel -> GUIApp -> GUIWindowHost -> WidgetTree` 职责图（`owner-model.md`）
- [x] 盘点现有 `App / Host / GUIAppHost / Surface / Panel` 类型，整理成保留/改名/过渡/删除四类表
- [x] 明确 app/window/tree 各自拥有的状态：focus、hover、capture、modal、popup、drag overlay
- [x] 明确 control plane 基底的最终挂载层级，以及 GUI/game/runtime editor 的扩展点
- [x] 补一页 owner checklist：谁创建、谁销毁、谁 restore、谁 dump
- [x] 把动态 visual hierarchy mutation 正式写入计划：attach/detach/reparent/reorder 属于 retain UI 运行时合同，而不是 GUI app 静态拼装例外
- [x] 产出目录归位表：Foundation/Core、Foundation/Core/Application、Foundation/RHI、Framework/AppRuntime、Framework/GUI/App、Framework/AppServices、Product/Host、Product/Editor、Example/GUIWorkbench 映射到目标结构
- [x] 写出目录收口 charter：明确共享能力轴与应用形态轴，并把 Foundation / Framework / Product 降级为历史现状（`directory-charter.md`）
- [x] 决定 App 不能默认带窗口语义：只保留 `App/Kernel`、`App/Control` 两段，支持 CLI / DS / server / render-service 等无窗口形态
- [x] 决定 Framework/AppRuntime 的唯一去向：`GUI/Host`；窗口/bootstrap/native event source 收回 GUI 宿主链
- [x] 决定 Framework/GUI/App 的唯一去向：`GUI/Host`；首轮保留 `GUIAppHost` compatibility alias
- [x] 把 `Game / Editor` 定性为应用形态，而不是共享能力大桶；共享 `Scene / Physics / Scripting / RenderRuntime / Reflection` 后续必须按能力轴归位
- [x] 把 `NativeWindow` 的未来边界定性为：`INativeWindow`（窗口对象）+ `IPresentSurfaceSource`（RHI present bridge），RHI 不再依赖完整 window 语义
- [~] 决定 Product/Host 的唯一去向：必须离开顶层 Product；先拆共享能力，再把剩余壳归到具体 app-form shell；确切 leaf 名待 include/target audit 拍板
- [~] 定性 `Framework/AppServices`：不属于共享 App 主链；下一轮按真实职责拆回 `Render/Runtime`、`Scene`、`Physics`、`Scripting` 或具体 app shell
- [ ] 补目录 -> target -> include 根映射表，作为 Batch 1 闭包审计工件，并显式区分 `App/Kernel`、`App/Control`、`GUI/Host`
- [ ] 补“共享能力轴 -> 目录 / target”与“应用形态轴 -> owner / shell”双视图映射表，避免后续 move/rename 又回到单轴思考
- [ ] 盘点 `Foundation/RHI/NativeWindow*` 的现有 API，形成三分表：留在 `INativeWindow` / 下沉到 `IPresentSurfaceSource` / 改由 presenter or host 持有
- [ ] 先做一轮不改行为的目录迁移设计：只移动文件/改 include/改 target 名称，不混入逻辑重写

### 下一刀顺序

- [ ] 先补目录 -> target -> include 根映射表：把 `Foundation/Core`、`Foundation/Core/Application`、`Foundation/RHI`、`Framework/AppRuntime`、`Framework/GUI/App`、`Framework/AppServices`、`Product/Host`、`Product/Editor` 映射到实际 target / include 根 / 未来目录
- [ ] 先补共享能力轴 / 应用形态轴映射：明确 `GameRuntime`、`GameEditor`、`GuiWorkbench` 各自只组合哪些能力，不再把共享系统默认归到 `Game`
- [ ] 先补 `NativeWindow` API 三分表：窗口身份、窗口管理、present bridge 各自谁拥有，谁允许被 RHI 看见
- [ ] 先盘点 xmake target 依赖线：确认哪些 target 只是目录命名噪声，哪些已经形成真实复用边界
- [ ] 先做第一轮 rename/move 设计，不动行为：
  - [ ] `Foundation/Core/Application` -> `App/Kernel` + `App/Control`
  - [ ] `Framework/AppRuntime` -> `GUI/Host`
  - [ ] `Framework/GUI/App` -> `GUI/Host`
  - [ ] `Product/Host` -> 具体 app-form shell（`GameRuntime/*` 或保留 `Editor/*` 外壳内的 branch-local shell；最终名字由 audit 决定）
- [ ] 再做第一轮 no-behavior 迁移：文件移动、include 修正、xmake target/name 收口
- [ ] 迁移后立刻验证 GUIWorkbench 链接闭包，确认不再被 `Product/Host` 语义污染

### 完成标准

- [x] 文档能直接回答“哪个是真正主循环、哪个拥有窗口、哪个拥有 tree”
- [x] 同名/近名类型已给出明确去向
- [x] control plane 不再被描述成多个平行入口
- [ ] 新开发者能从目录直接读出共同依赖线与组合关系：`App/Kernel (+ App/Control)` 之上是 `Render / GUI / Scene / Physics / Scripting ...` 等共享能力，再往上才是 `GameRuntime / GameEditor / GuiWorkbench` 等 app form；headless path 可在无窗口能力链截停
- [ ] 新开发者能从目录直接看出“共享能力轴”和“应用形态轴”分别是什么，不会再把 `Game / Editor` 误判成共享 Physics / Scripting / Scene 的归宿
- [ ] GUI example 的链接闭包不再被 Product/Host / Game 分支语义污染
- [ ] 至少完成一轮目录/命名迁移，且行为验证与迁移前等价

## P2 — Phase B Layout / Slot 内核

### 已完成

- [x] 定义 `UILayout` 基类：measure/arrange/invalidation 合同
- [x] 定义 `UISlot` 基类：parent-owned edge object 合同
- [x] 定义 `UIBoxLayout / UIBoxSlot` 的最小 public API
- [x] 规定 child 获取 slot、parent 获取 layout 的访问面
- [x] 规定 reparent / detach / destroy 时 slot 生命周期
- [x] 规定 slot 改动对应的 invalidation 粒度
- [x] 选择一个最简单 vertical box 页面，迁到新 layout/slot
- [x] 选择一个 nested box 页面，验证横竖组合与 reparent

### 完成标准

- [x] 至少一个页面完全不依赖 UIContainer 旧 box 字段
- [x] slot 不在 detach/reparent 后悬空
- [x] layout/slot dump 能看到 box 布局结果

## P3 — Phase C 事件路径与状态模型

### 当前切片：path state 与 route trace

- [x] 定义 pointer state / pointer path / focus path 数据结构
- [x] 拆分 hit test 求 target/path 与 route phase 执行：topmost candidate discovery 与 `dispatchRoute()` 分离
- [x] 定义 preview/tunnel -> target -> bubble 三阶段合同：`UIElement` 的 Preview/Target/Bubble hook 已由真实执行器调用
- [x] 把 capture / modal / popup / drag session 归类为 route policy：HitTest/Capture/Focus/Tab/Drag/Popup/Modal 均有 trace policy
- [x] 收敛“当前鼠标位置”为 tree/window 级持续状态，不再作为业务层接口参数反复传递
- [x] 为 menu/button/scroll/split/dragdrop 制定最小事件语义表（`event-routing.md`）
- [x] 增加 route dump、hover/focus overlay、自动化路径断言：dump 和 unit test 覆盖 path/phase/handled；`--debug-render-overlay` 显示 pointer/focus path、hover/capture 和 pointer marker

### 完成标准

- [x] 一次 mouse move / press / release 可以打印明确 path（`lastRoute.path/steps`）
- [x] menubar 横向 hover 切换有结构证据（`menus_popup_interaction.jsonl` assertion + checkpoint）
- [x] button 状态回落有自动化断言（`widgets_interaction.jsonl` Counter release assertion）

## P4 — Phase D Workbench 与测试迁移

### 当前切片：feature gallery regression matrix

- [x] 明确 Workbench 页面分层：基础交互页、布局页、树/属性页、overlay/debug 页（`workbench-regression-matrix.md`）
- [x] 先迁 menu / popup / button 三类页面：Menus popup 与 Widgets interaction scenarios 均有 host-side assertion、checkpoint 与初始 golden
- [x] 再迁 scroll / split 页面：resize + divider drag + wheel stress 已升级为 host-side layout assertions 与 zero-diff baseline
- [x] 最后迁 tree / inspector 页面：Editor scenario 覆盖 selection -> visibility -> rename -> preview/row binding
- [x] 每个 feature page 至少补一条 scenario + 一张 golden：8/8 page matrix 已执行 zero-tolerance BMP diff

### 完成标准

- [x] Workbench 成为 feature gallery + regression app
- [x] demo 页不再回流到 Framework 代码中
- [x] 高频体验缺陷具备结构断言与视觉基线

## P5 — Phase E/F specialized layout 与多窗口留口

### 已完成

- [x] 收 split 到 `UISplitLayout`
- [x] 收 scroll 到 `UIScrollLayout`
- [x] 明确单 child 内容布局抽象（`UISingleChildLayout`）
- [x] 先按“组合控件优先、专用 layout 兜底”评估 menu/tree/inspector（`specialized-layouts.md`）
- [x] 文档化多窗口 owner 边界（`multiwindow-semantics.md`）
- [x] 文档化 cross-window dragdrop 的 source/target/commit/cancel 语义
- [x] 文档化 per-window vs whole-app modal 语义
- [x] 文档化 active window / activation order / focus path 切换规则

### 完成标准

- [x] 不改 layout 内核就能解释未来多窗口与 docking
- [x] dragdrop / modal / focus 默认语义已经写清

## 暂不推进

- [ ] `.yaui / UIDocument` 作为当前主 authoring 入口
- [ ] 完整 docking 系统
- [ ] editor shell 全量替换
- [ ] 主题系统大扩张
- [ ] RHI 全面重写
