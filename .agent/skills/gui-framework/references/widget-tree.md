## WidgetTree 模型

- 层：`Content / Popup / Tooltip / DragIme`（项目内容不能覆盖系统层 zOrder）。
- 单视觉父契约：`attach`（新成员）/ `reparent`（显式迁移）/ `detach`（递归、清 transient state）。
- 输入：`dispatchEvent` 用显式 route：topmost candidate discovery 后执行 Preview
  (root -> parent) -> Target -> Bubble (parent -> root)。`Stop` 短路，`Pass` 继续 lower
  candidate；capture/focus/popup/modal/drag 都是 tree 级 route policy。`WidgetTree` 持有
  persistent pointer state、pointer path、focus path 和 route trace；`WidgetTreeDump`
  输出 `pointer`、`focusPath`、`lastRoute`（policy/path/phase/handled/result）。route callback
  可 detach 自身，executor 会持有 path 并重查 membership。
  **Pointer session 所有权（平台是物理状态的事实来源，框架不允许被它拖垮）：** press
  打开 session，session 只能以 release 或 cancel 结束。release 路由结束后框架自己回收
  仍被握住的 capture（`repairPointerSession`），控件不需要在每条路径上记得释放；没有
  press 的 `setPointerCapture` 直接拒绝。平台可能永远不投递 release（key focus 丢失、
  指针离开窗口、pane 被撕成另一个窗口、注入 press）：`WindowFocusLost` /
  `WindowMouseLeave` 时 host 用**事件自带的 `getHeldButtons()`**（`1u << EMouse::T` 编码，
  事件发生那一刻由 `SDL_GetGlobalMouseState` 采样进事件）调
  `WidgetTree::reconcilePointerButtons`——不要在处理处现场查物理鼠标
  （`OsEventPump::queryGlobalMouse().buttonMask`）：处理时按钮可能已再被按下/松开，
  reconcile 会拿错事实，也让回放与测试不确定。同键第二次 press 时树自己
  `cancelPointerSession`（计数见 `getPointerSessionRecoveries()` /
  `gui.tree.pointer_recoveries`），并给 capture 控件一次 `clearTransientInputState()`；
  cancel 也必须结束 drag session（observer 收到 `EDragFinishResult::Cancelled`）。
  禁止把这类丢失 release 当成 `YA_CORE_ASSERT` 崩溃、静默吞第一次点击，或用“divider 外
  再按一次放 capture”当修复。另一条仍然成立的根因：`UIDockSpace` 在 capture 落在 dock
  子树里时 `syncProjection(Structure)`。标题 Client 洞必须在 `buildSnapshot`（layout）
  之后发布。
  drag&drop 的 source-local 状态（`beginDrag/updateDrag/endDrag/cancelDrag`、payload、ghost、observer）由树管理，唯一入口是
  `beginDrag(source, UIDragDropOperationRef)`。跨窗的 source/hover window 身份由 host `GUIDragRouter` 唯一持有。基类带通用 `payload` slot；领域拖拽
  继承加字段（`FDockPanelDragDropOp` / `FTreeReorderDragDropOp`）。目标用
  `as<T>()` / `isType()`。拖放源/目标是两个 final 行为种类 `UIDragSourceBehavior` /
  `UIDropTargetBehavior`（回调配置 + `onOwnerDetached`），控件在 install 函数里配置后 `addBehavior`，
  不派生它们；树只调自由函数 `detectDrag/acceptsDrop/previewsDrop/dropOnto/highlightDrop/hoverDrop`
  （内部 `findBehavior`），`UIElement` 没有拖放虚函数。
- 行为模型（`UIBehavior.h`）：**能力封闭、种类开放**。能力 = GUI 的派发点（`EUIBehaviorCapability`：
  Tick / Input），每个一个接口；拖、放、tween 是种类不是能力。控件自身事件是控件上的
  `MulticastDelegate`（如 `UIButton::onClicked`），不是能力；GUI 没有「动作字符串」与冒泡 sink，
  脚本直接监听控件（Lua `btn.onClicked:add(self, fn)`，见 `ECS/Systems/LuaEvent.h`：委托只持弱令牌，
  Lua 回调归 owner 脚本实例、随其 `self` 结束；owner 必须是活脚本的 `self`）。行为用
  `UIBehaviorWith<Self, IUITickable, ...>`（`Self` final）在编译期声明能力与种类键 `type_index_v<Self>`，
  `addBehavior` 时进入 `UIElement` 的按能力索引，
  派发只读 `behaviorsOf<I>()`（同能力按挂上顺序），无 `dynamic_cast`。接口构造私有，不经
  `UIBehaviorWith` 声明就继承接口是编译错误。新行为种类（GameRuntime 的 Lua 脚本、编辑器行为）不改
  GUI 源码；新增**能力**才改 GUI。`UIBehavior` 基类只有 owner + 种类键 + `onAttached/onDetached`，
  禁止再往基类加能力虚函数。
  **一个控件每种行为至多一个**：`addBehavior` 按种类键拒绝同类第二个（返回 false + ERROR），
  所以 `findBehavior<T>()`（扫连续种类键，返回 `T*`，无 RTTI）结果唯一。需要多份工作的行为自己在内部持有多份
  （`UIAnimatorBehavior` 持有多个 `UITween`；一个控件至多一个 `script.lua`）。
  文档节点上的 `behaviors` 只往返 `type` / `data`，GUI 不解释。`activateBehaviorSpecs` 从调用方给出的根开始遍历，并把这个根记成 `documentRoot`，子节点不会变成自己的根。哪一种 spec 只在这个根上生效，由宿主决定；GUI 没有 logic 字段，也不知道 Lua。
  文本焦点：`UITextField` 消费 `KeyTyped`（IME 提交）、按码点 Backspace/Delete，选区
  （anchor/caret、Shift+方向、拖选、primary+A），以及
  primary+C/X/V（Cmd macOS / Ctrl 别处）经 `WidgetTree` clipboard（作用在选区上；
  无选区时拷切整缓冲）。默认内存缓冲；windowed host（含 extra 窗）用
  `bindSdlClipboard` 接同一块 OS clipboard。DPI 由 `setDpiScale` 与 `uiScale` 正交折叠。文字不在几何缩放之后重采样：`planTextRaster` 把 zoom、树 dpi、render-transform scale 收成整像素栅格尺寸，bitmap 的 `textScale` 为 1（见 font-rendering skill）。游戏 UI 的 framebuffer scale 是所在 View 的 `pixelDensity`（设备像素 / 逻辑点），不是宿主 `renderScale`；参考分辨率 fit 在逻辑尺寸上算，是连续函数，没有 min UI scale。低于 9px 栅格下限的文字会溢出 slot，下限放在游戏窗口最小尺寸和视口 split 的 `minExtent` 上。
  焦点/悬停时 `getCursor()` 为 `ECursorType::IBeam`。`UIDragFloat` / `UISpinBox`
  的 `_bEditing` 复用同一套 `FTextEditState`，不要再写第三套迷你编辑器。
  IME / 文本输入：`WidgetTree::wantsTextInput()` 沿 **focus path** 问每个节点的
  `UIElement::wantsTextInput()`。`UITextField` 为 true；`UIDragFloat` / `UISpinBox`
  仅 `_bEditing`；ColorEdit picker 仅 hex 编辑。Editor chrome 经 session 转发 tree，
  禁止 `dynamic_cast<EditorInspectorTab*>`。
- 快照：`buildSnapshot`（layout dirty 时才 layout + paint）→ 不可变 `UIFrameSnapshot`；
  录制只消费快照。命令录制期绝不读 live tree。业务代码不得在 paint/layout
  回调中直接修改 tree 结构；tooltip/drag 等 framework maintenance 只在显式 pass boundary 执行。
  渐变走 `UIFrameBuilder::addRectFilledMultiColor`（Y-down 四角：TL/TR/BR/BL）→
  `Render2D::makeRectFilledMultiColor`。GPU 一个 quad 是两个三角形，二维四角场会出对角缝；
  一维渐变（对边同色）才正确。HSV 方是两层 1D（S 再 V-alpha），不要用 sprite 格子或控件离屏 RT。
  编辑器规模基线在 `EditorScaleBaselineTest`：Hierarchy 只 paint 视口窗口、Content 目录
  `computeKeyedVisibleWindow` 与 catalog 规模无关、Inspector 列第二次干净 snapshot `rebuiltWidgets==0`。
  长时结构 soak 在 `EditorLongRunSoakTest`：同 subtree attach/detach、destroy/recreate、theme 切换、
  deferred texture generation。GPU/offscreen 像素门禁：`Script/automation/gui/run_workbench_gpu_parity.py`
  （headless lastRoute + snapshot digest + windowed `--gpu-shot`/`--offscreen-diff` 零容差）。
- 帧生命周期（**树上一张 tick 登记表，行为级登记**）：`WidgetTree` 持有 `FWidgetTickRegistry`
  （`WidgetTickRegistry.h`），`WidgetTree::tick` 只遍历登记项，不再整树递归、不再每帧轮询；
  没人要帧时开销与节点数无关。登记项是 `(owner, tickable)`：控件**自己的类**用 `enableTick()` +
  override `tick()`（一项，tickable 为空；editor tab / section 是这一类）；**别人的部件**用
  `addBehavior(IUITickable 行为)`，行为用 `setTickEnabled(bool)` 开关自己那一项（tween 播放期、
  对话框自刷新、Lua `self:setTickEnabled`）。两者**互相独立**：行为要帧不会让 owner 的 `tick()` 跑，
  override `tick()` 也不需要转发 `UIElement::tick`（基类 no-op），所以子类无法藏掉行为那一项
  （历史坑见 `../../../memories/widget_tick_gate_hidden_by_override.md`）。
  登记随成员关系：节点挂上树时按当前开关登记，脱离树时移除，开关只在已挂树时增删表项。
  只 tick `isVisibleInTree()` 的 owner（按 `getVisibilityRevision()` 缓存，帧内可见性变化在下一项前刷新）；
  顺序只承诺「树结构变化后按前序重排」（同一 owner 先自身、再按行为添加顺序），不承诺更多。
  派发中增删安全：移除即刻失效（之后才压缩），新增项下一帧才跑，重入 `tick` 警告并返回。
  `wantsTick()` 仍在，但只是诊断查询（自身开关 || 任一行为开着），不是门。


