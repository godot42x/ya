## UX vs closure 测试

- `ya-gui-closure-test` 覆盖 dump / dirty / route / snapshot contract，**不是**手感门禁。
  不要把「N dump tests passed」写成 UX 完成。
- 手感验收走 Gallery `--scenario` 的状态组合（见下方交互契约 1–11）和 editor 手测
  （选区、选色、dock 关 tab、Hierarchy 右键、Designer 树 DnD）。路径存在 ≠ 手感等价。
- closure dump/route/dirty **测不出**：`isHoverable`、presenter 选中循环、首次 expand
  的 Layout Reactive、Modal 输入独占、page `leave` 拆 Popup、ColorEdit SV 是否是
  ImGui 两层一维顶点色（S 再 V-alpha；不是四角 2D quad，也不是 HSV 格子）。Gallery scenario 必须点 hover/select/expand/resize，不能只 assert 控件存在。
- 内核体验长线见 `.agent/plan/gui-kernel-ux-parity/`。parity 表
  `.agent/plan/gui-framework-editor-readiness/imgui-widgettree-parity.md`
  里 ✅ 只表示 retained 有一条能完成核心工作流的路径。



## 控件交互契约（编辑态 / 弹出态规则）

这些是 2026-08-18 Gallery 交互验收轮沉淀的硬规则，写新控件时逐条自查：

1. **编辑模式不吞底层交互**：进入编辑态（_bEditing）后，控件原有的业务交互
   （SpinBox 的 +/- 步进、拖拽、打开菜单）必须仍可达——编辑态 press 分支要先
   commit/cancel 编辑再执行原交互，绝不能 `return false` 把 press 吞掉。
2. **弹出控件的 dismiss 必须释放交互残留**：菜单/弹出被真实关闭（外部点击/Esc/
   选挑）时，dismiss 回调要一次清完——filter 清空 + 主动 `setFocus(nullptr)`
   （否则控件继续画 '(type to filter)' 等占位态，用户要再点一次才恢复）。
3. **可见内容集变化必须同时标 Layout + Paint**：Reactive 的 Layout 粒度通知只保证重排；
   若重排后排列 rect 不变（固定高度树/表），增量 paint 缓存会继续画旧内容。
   `setExpanded/toggleExpanded` 必须 `markLayoutDirty()` + `markPaintDirty()`。
   首次 expand 前 `isExpanded` 必须创建 Layout 粒度 Reactive，否则 Auto 父级高度不涨。
   任何影响可见行/可见项的状态变化都按此处理。
4. **弹出刷新 ≠ 真实关闭**：「关旧开新」的刷新路径会触发旧菜单 dismiss 回调；
   回调里的清理逻辑（清 filter 等）必须用刷新标志（_bRefreshingMenu）隔离，
   只在真实关闭时执行。
5. **过滤/搜索的自动展开是一次性的**：过滤变化时自动展开匹配链（记录
   `_lastFilterApplied` 防重复），之后手动折叠/展开必须仍然生效——过滤
   不能持续强制展开（TreeView「filter 激活时箭头失效」即此坑）。清空
   过滤永不收拢任何东西。
6. **demo 的约束性行为要有可见文案**：选择性 accept（drop target 谓词）、禁用
   条件等「看起来像 bug」的设计，必须在控件 label / 页面说明里写明
   （如 'Zone B: only payload.2'）。
7. **Hover chrome 必须 `isHoverable()`，且 hover owner 的 `hitTestSelf` 含指针**：
   WidgetTree 只对 hoverable 节点发 enter/leave。DragFloat/SpinBox/ColorEdit 色板与通道
   不声明时，hover 填色永远不出现。`hoverOwnerAlongPath` 还会要求 `hitTestSelf(point)`：
   Expander 只把 header 当 hover 区，body 里的 label 不能点亮 header（或外层
   CollapsingHeader）。Button 仍会从 text child 冒泡，因为 button 的 hitTestSelf
   是整块 layout rect。
8. **Modal 只独占输入**：`_bModal` 让鼠标/键盘无法穿透到该 overlay 之外（直到
   OK/Cancel/Esc），外点消费但不关窗。框架不画遮罩、不提供 dim 开关。要挡住/
  模糊底下，在 dialog chrome 下叠一张 fill 的 CanvasPanel/Image（`HitTestInvisible`
  以免抢 hit），和 UE / Qt / Win32 modal window 一样。
9. **字符串匹配默认 ignore-case**：SearchCombo / TreeView filter 走 `StringMatch`，
   默认 IgnoreCase；要大小写敏感再显式 Sensitive。
10. **Presenter 必须同步 SelectableRow**：行上的 `_bSelected` 不是数据源。
    `_onSelect` 必须循环 `setSelected` 全部行，否则点了看起来没选中。
11. **离页必须拆 Popup 附着**：Workbench `FPage::leave` / `setPageLeave` 拆掉该页
    挂到 Popup 层的 host（Dock floating）。Content 层换页时 Popup 层会活下来。



## 人肉测试前的自动化验收

按 `Example/GUIWorkbench/Scenarios/` 的 31 个 jsonl 回放做第一道闸（跑法：
`xmake run GUIWorkbench --start-page <Page> --scenario <abs path> --scenario-dump-dir <dir>`，
exit 0 = 全 checkpoint 过）。页名必须是分组 rail 上的 Gallery 页（`Inputs`/`Tree`/`Brush`/`Dialog`…
不要再写已删除的 `Gallery`/`Layout`/`Modal`，除非有意走别名）。tooltip dwell 用
`--scenario-render`（`gallery_p1.jsonl`）。写验收场景时的覆盖要求：

1. **状态 × 交互组合矩阵**：有编辑态/弹出态的控件，必须覆盖「进入状态 → 各交互」
   的关键项（编辑态 × {键入, Backspace, Enter, Esc, +/- 点击, 外部点击}；菜单开 ×
   {过滤输入, 选挑, Esc, 外部点击}）。单条 happy path 会漏掉状态组合 bug
   （SpinBox 编辑态吞 +/- 即如此）。
2. **焦点生命周期断言**：弹出类控件关闭后断言 `focusPath` 不含该控件（或为空）——
   只断言 popup 结构开合不够（SearchCombo 两次点击 bug 即漏在焦点上）。
3. **数据副作用断言**：断言要锁到值级（filter 字段、visibleRows 计数），不能只锁
   控件存在/结构（filter 被刷新清除的 bug 就溜过了只查 popup 结构的断言）。
4. **双布局变体**：TreeView/Table 的行集变化测试，autoSize 和固定高度两种布局都要
   覆盖——前者 rect 变化掩盖了「Layout≠Paint」漏画，后者才暴露。
5. **渲染级验证走真机**：scenario 模式不渲染帧（buildSnapshot 不跑），G2 校验帧
   （漏标脏告警）和像素验证必须用真机。真机走 `control` 入口，别自己拼启动命令：
   `python3 Script/ya.py control start --project <p>`（有则接入、无则起一个且带墙钟上限）
   → `control call` / `control mcp` → `control stop`（详见 `ya-build` skill）。行为断言归
   scenario，渲染断言归真机，两条线分工。
6. **环境随机崩溃重试**：GUI 反复启动偶发 init 崩溃（0xC0000005，swapchain 创建
   阶段，与场景内容无关）。自动化脚本对场景运行加重试（每场景最多 4 次，间隔
   1.5s），不要在单次失败上误判回归。

