# GUI Style System 收敛计划：从硬编码控件色到 Theme/Style 上下文

> 建立日期：2026-08-21
> 状态：活跃计划
> 目标：把当前 GUIWorkbench / Dock / Menu / Tab / FloatingWindow 的硬编码视觉参数，收敛为一套可复用、可切换、可逐步扩展到 Editor 与 Game UI 的 style system。

## 0. 结论摘要

当前仓库并不是完全没有样式机制，而是只有一层很薄的原语：

- UIStyleSet
- Reactive<FWidgetStyle>
- UIText::bindStyle()

这套原语可以证明样式变化触发依赖控件重绘的方向成立，但还远远不是一个可支撑 retain-mode GUI shell 的 style system。当前主要问题不是颜色不好看，而是：

1. 大量控件仍直接持有 _normalColor/_hoveredColor/_selectedColor/... 之类字段；
2. Workbench / Dock / Floating / MenuBar / TabBar 没有统一 token 来源；
3. 没有 tree/window/subtree 级 theme context；
4. 没有统一的 style resolve 链；
5. 没有 typed widget styles，FWidgetStyle 无法承载真实 GUI shell；
6. 没有明确区分 framework 负责样式机制和 app 负责主题内容。

本计划的正式判断：

- GUI framework 必须内建 style system；
- app / editor / game 提供各自 theme 内容；
- 第一阶段不做 CSS/QSS/DSL 解释器；
- 第一阶段先建立 typed style + theme context + resolve 规则，让控件退出硬编码。

一句话主线：

Framework/GUI 提供样式运行时；GUIWorkbench / Editor / Game 只提供主题资产与映射。

## 1. 先回答边界问题

### 1.1 style system 属于 framework 还是 app？

机制属于 framework：style 类型系统、theme context、resolve 链、状态态映射、subtree/window 级覆盖。

主题内容属于 app：Workbench dark theme、Runtime editor theme、Game default HUD theme、未来的 light/high-contrast/theme pack。

因此 UITheme / UIThemeContext / typed style structs / resolve rule 属于 Framework/GUI；WorkbenchTheme / EditorTheme / GameTheme 属于应用层或示例层。

### 1.2 game UI 需不需要 style system？

需要，但诉求比 tool GUI 轻。Tool GUI 需要 menubar/tab/dock/floating/inspector/popup 的统一 shell 风格、状态态系统化表现、多窗口/subtree/popup 的 theme 继承与覆盖。Game UI 也需要统一字体字号文本色、menu/HUD/dialog/button 一致性、平台/模式/skin/mod 切换。

所以不是 GUI 需要而 Game UI 不需要，而是机制共用、主题复杂度不同、style key 空间不同。

### 1.3 StyleSet 放在应用层可以吗？

不行，至少不能只放应用层。如果 style set 只存在于 app，framework 控件无法统一 resolve style，subtree/window override 无法成为正式能力，editor/game/workbench 各自会平行发展第二套样式注入约定。

正确做法是 framework 提供统一 style runtime，app 只往 runtime 填具体 theme 数据。

## 2. 当前现状审计

### 2.1 已有基础

- GUI/Widgets/Style.h：UIStyleSet / FWidgetStyle / Reactive<FWidgetStyle>
- UIText 已经支持绑定 style，并在 paint/layout 时读取绑定值

这说明 reactive 驱动的样式失效链可行，样式变化 -> 控件重绘 方向已有最低证据。

### 2.2 主要缺口

1. FWidgetStyle 过于通用，只有 fillColor/textColor/fontSize/padding，只够 Text 或极简单按钮，无法表达 Dock/Floating/MenuBar/Tab 的真实需要；
2. 样式入口不统一，Button/TabBar/MenuBar/Dock/Floating 各自持有私有颜色字段，app 只能构造后挨个覆盖；
3. 缺少 owner 级 theme context，WidgetTree/window/subtree 不能自然切 theme；
4. 缺少状态态模型，normal/hovered/pressed/selected/active/focused/disabled 没有统一 resolve；
5. Workbench 的样式收口现在仍是临时 token header，属于阶段性整理手段，不是终局机制。

## 3. 终局对象模型

### 3.1 三层模型

A. design token 层：表达主题原料，不直接给控件使用。颜色如 color.bg.window/color.bg.panel/color.border.subtle/color.text.primary/color.text.muted/color.accent；尺寸间距如 spacing.xs/sm/md/lg、radius.sm/md、border.thin/normal；字体如 font.body/font.mono/font.size.sm/md/lg。第一阶段可先用 C++ struct 承载。

B. typed widget style 层：第一阶段重点。每类控件有自己的 style struct：FTextStyle、FPanelStyle、FButtonStyle、FTabStyle、FMenuBarItemStyle、FSplitPaneStyle、FScrollBarStyle、FDockSpaceStyle、FFloatingWindowStyle。不允许继续共用 FWidgetStyle 假装通用；不允许新的 shell 控件继续直接暴露一整套颜色字段；控件只读取与自己有关的 style 属性。

C. theme context / style lookup 层：UITheme 持有 token 与 named typed styles；UIThemeContext 挂在 WidgetTree/GUIWindowHost/subtree override 上；UIStyleKey 是命名查找键，如 button.toolbar、tab.workbench、dock.floating。

### 3.2 resolve 链

统一顺序：widget explicit override -> widget style key lookup -> subtree theme override -> tree/window theme default -> framework fallback。

禁止 resolve 不到就临时写 magic color，禁止应用层通过遍历 children 二次覆写控件字段作为主要主题方式。

### 3.3 状态态模型

统一状态态：normal/hovered/pressed/selected/focused/active/disabled。widget 只维护当前状态，style system 决定状态对应的绘制属性，业务逻辑不再把 hovered=某颜色写死在 paint 里。

## 4. 最小闭环设计

第一阶段只做 retain-mode GUI shell 所需的最小闭环，不做大而全主题系统。

第一批接入控件优先级：UIText -> UIPanel -> UIButton -> UIMenuBarItem/UIMenuBar -> UITabButton/UITabBar -> UISplitPane -> UIDockSpace -> UIDockFloatingWindow。这批就是 Workbench/Dock/Floating 最明显的 shell 构成，接入后 Workbench 外观能从硬编码转成主题驱动。

第一个主题宿主：GUIWorkbench。它是 feature gallery + regression app，能覆盖 menu/tab/dock/floating/editor shell，可作为 editor 主题系统前置验证场。

第一阶段明确不做：CSS/QSS 解析器、selector engine、XML/DSL style file、editor/game 双主题同时落地、图像 brush/9-patch/radii/shadow 全量体系、运行时样式编辑器。

## 5. 分阶段落地顺序

## Phase 0 - 审计与主入口清理

审计所有核心 shell 控件现有颜色/边距字段，列出哪些已有 style binding 哪些没有，把 Workbench 当前临时 token header 标记为过渡输入。完成标准：有 style capability matrix 和控件 -> typed style 映射表。

## Phase 1 - typed style structs 与 framework fallback

新增第一批 typed style structs，每种 style 定义 framework fallback 默认值，定义状态态结构或命名约定。完成标准：Button/Tab/MenuBarItem/Panel/FloatingWindow 都存在自己的 style struct，新控件不再以加颜色字段为默认扩展方式。

## Phase 2 - Theme / ThemeContext / resolve 链

新增 UITheme、UIThemeContext，WidgetTree 或等价 owner 能设置默认 theme，控件支持 style key/explicit override/fallback resolve。完成标准：同一控件在不同 tree/window 下可 resolve 不同样式，不需要 app 遍历 children 手动改色。

## Phase 3 - 第一批控件去硬编码

UIText/UIButton/UIMenuBarItem/UIMenuBar/UITabBar/UITabButton/UIDockFloatingWindow/UIDockSpace 从对应 typed style resolve。完成标准：Workbench 最外层 shell 不再遍历 child 二次覆写，Dock/Floating/Tab/Menu 视觉常量不再散落 cpp。

## Phase 4 - GUIWorkbench 主题接入

新增 WorkbenchTheme 或等价实例，FWorkbenchSurface/demo pages 通过 theme key 组织样式，Dock/Editor/gallery 共用同一主题。完成标准：修改一个 token 或一组 named style 可全局改变 Workbench 壳层外观。

## Phase 5 - Game / Editor 扩展预留

定义 game/editor theme key 命名约定，明确哪些 typed style 需扩展到 HUD/menu/dialog，明确多窗口 theme context owner 语义。完成标准：style runtime 可被 editor/game 复用，不需要重构 core。

## 6. 目录与 owner 建议

- Framework/GUI/Runtime/Widgets/Style.* 继续保留基础 reactive/style 能力；
- Framework/GUI/Runtime/Widgets/Theme.* 放 UITheme/UIThemeContext/typed styles；
- Framework/GUI/Tooling/Workbench/... 放 WorkbenchTheme 等 app/theme 内容。

硬约束：framework public include 只放通用 style runtime；WorkbenchTheme 不反向污染 runtime/widgets core；不允许把 editor/game 具体命名塞进 framework typed style 层。

## 7. 验证计划

编译：xmake b GUIWorkbench、xmake b ya-gui-widgets、xmake b ya-gui-tooling。

行为：menu hover-switch、tab select、dock tear-off/redock、floating resize scenario 仍通过。

视觉：产出 Workbench 全窗口、Dock 页、Editor 页截图基线，检查 menu/tab/floating/dock/status bar 是否共享一致层级，hover/selected/active/focused 是否一致。

架构：新增 shell 控件不得继续默认扩展 _normalColor/_hoveredColor 等；Workbench 不以遍历 children 覆写控件字段作为主要主题机制；Theme/Style runtime 不直接依赖 Workbench/Game/Editor 语义。

## 8. 当前默认决策

1. 第一阶段不做 CSS/QSS；2. 采用可编程 theme + typed styles + style keys；3. style runtime 属于 framework；4. theme 内容属于 app；5. GUIWorkbench 是第一接入宿主；6. game UI 复用同一 runtime 但主题复杂度更低；7. 临时 WorkbenchStyle 只是过渡输入。

## 9. 退出条件

Workbench shell 主要控件都从 theme/style resolve 而非硬编码；tree/window/subtree 级 theme context 已存在；typed style 已替代核心 shell 控件裸颜色字段扩张趋势；app 与 framework 职责边界清楚；下一步扩 editor/game 无需重写 style core。

