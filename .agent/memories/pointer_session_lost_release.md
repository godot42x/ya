# GUI pointer session：丢失的 release 要回收，不能是 assert

> 2026-09-19，编辑器调 dockspace / splitter 时频发 `WidgetTree.cpp:1499` 断言崩溃时定位。
> 一句话：**press 缓存只有 release 才会清，而 release 会丢——所以回收必须归框架，不能归控件。**

## 现象

拖动或点击 splitter / dock 分栏几轮后崩溃：

```
[ERROR] WidgetTree.cpp:1499 WidgetTree: MouseButtonPressed (EMouse::Left) while that button is
        still down (capture=<none>); leftover pointer session steals the first click
```

栈顶是 `WidgetTree::beginPointerDispatch` → `dispatchEvent`。`YA_CORE_ASSERT` 展开是
`YA_CORE_ERROR` + `PLATFORM_BREAK()`，**debug/release 都 abort**，所以现象是进程直接死。

## 根因

1. `_pointerButtonsDown` 是 per-tree 的按下缓存，只有 `MouseButtonReleased` 清；没有别的回收路径。
2. release 真的会丢，且都不是控件能负责的情况：key focus 丢失（切到别的 app、dock tab 撕成
   另一个 OS window）、指针在没有 `SDL_CaptureMouse` 的情况下离开窗口后释放、自动化注入
   press 却没有配对 release。分栏拖动正是长时间按住、经常结束在窗口外的操作。
3. 平台侧也没有兜底：`GUIAppHost`（primary window）完全不处理 `WindowFocusLost`；
   `GUIWindowManager::dispatchToSession` 的 extra window 分支在 dragging/capture 时**显式提前
   return**（“capture/drag 可以跨窗”），于是这个 session 永远留着自己。
4. `e1c93a0e` 把「第一次点击被吃掉」升级成断言崩溃。意图是对的（不要静默吞点击），但断言放在
   frame loop 里、对所有构建生效、且状态本来就用另一个更好的 owner 可以修复，于是“点不动”
   变成了“直接崩”。
5. 同一家族还有：`UISplitPane::handleInputEvent` 的 capture / `_bDraggingDivider` 分叉断言、
   capture 握在已 detach 控件上的断言、`setPointerCapture` 无 press 断言。

## 契约（现在的实现）

press 打开 session，session 只能以 **release** 或 **cancel** 结束；框架是 session 的 owner，
控件不需要在每条路径上记得 release。

- release 路由结束后 `repairPointerSession` 回收仍被握住的 capture（warn + 计数）。
- `WidgetTree::cancelPointerSession(cause)`：丢掉 drag candidate、`cancelDrag()`（observer 拿到
  `EDragFinishResult::Cancelled`）、给 capture 控件一次 `clearTransientInputState()`、清 button
  mask、清 hover/pointer path。
- 同一键的第二次 `MouseButtonPressed` 就是「上一次 release 丢了」的证据：树自动 cancel（warn +
  计数）后**照常服务这次 press**。
- `WidgetTree::reconcilePointerButtons(osMask, cause)`：平台是物理状态的事实来源。
  `FOsMouseQuery::buttonMask`（编码 `1u << EMouse::T`）由 host 在 `WindowFocusLost` /
  `WindowMouseLeave` 上报；mask=0 → cancel，仍按住 → 保留 session（跨窗拖拽靠这条活着）。
- 没有 press 的 `setPointerCapture` 直接拒绝（那种 capture 没有任何 release 能结束它）。
- 诊断而不是崩溃：`getPointerSessionRecoveries()` / perf `gui.tree.pointer_recoveries`。abort 只
  留给「程序错误且无法安全修复」的地方。

接线点（每个都持有树和平台状态）：`GUIAppHost::onEvent`（primary window）、
`GUIWindowManager::dispatchToSession`（extra window）、`AppEventRouter` →
`EInputCancelReason::WindowFocusLost` / 新增的 `PointerLeftWindow` →
`EditorInputNode::reconcilePointerSessionsWithPlatform`（editor 全部 window tree + GameUIHost
tree）。`PointerLeftWindow` 只走 pointer，不 cancel 按键（指针离开 viewport 不能吃掉按住的键）。

## 验证

- 新增 6 个 `WidgetTreeTest`：丢 release 后下一次 press 仍被服务、平台 state reconcile、capture
  不能活过 press、无 press 的 capture 被拒、detach 后 repair 不崩、cancel 结束 drag session 并给
  observer `Cancelled`。
- `WidgetTreeTest.*` 86/86（排除既有会 abort 的 `SystemLayersCannotBeDetached`）；
  `ToolControlsTest` / `DockNodeTest` / `InputRouterTest` / `GuiEventDriverTest` /
  `BindingContractTest` / `DeclarativeContractTest` 214/216（2 个 split 比例 clamp 失败是既有的）。
- 注意：`WidgetTreeTest.SystemLayersCannotBeDetached` 自己会 abort（`detach` 系统层走断言），
  所以整套 `WidgetTreeTest.*` 永远 exit 133，只能按 filter 跑。

## 边界

- 不要用“在 divider 外再按一次来放 capture”当修复，也不要用 `_bVolatile` / 每帧写 presenter
  字段绕开失效链。
- release 是平台的投递结果，不是控件的义务。任何“必须记得配对”的 pair API 最终都会漏，正确
  做法是让框架回收 + 平台状态对账 + 计数可见。
