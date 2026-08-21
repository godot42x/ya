# Phase 0 - Style capability audit

> 建立日期：2026-08-21
> 作用：记录当前 style 相关入口与硬编码热点的真实状态，供后续 typed style / theme context 直接引用。本文件不是最终机制，只是收口前的审计快照。

## 结论摘要

第一阶段不做 CSS/QSS/DSL。当前需要先回答一件事：控件的外观数据现在长在哪、由谁写、改动一次要动多少处。

本轮审计确认：当前外观数据分散在两类地方，两类都不该作为终局机制：

1. framework 控件自身持有裸颜色字段（_normalColor/_hoveredColor/_selectedColor/...），并通过 addTab / addItem 等工厂方法把父容器字段复制给子按钮；
2. app 层通过临时 token header（FWorkbenchStyle / defaultWorkbenchStyle）和遍历 children 覆写控件字段，作为主要主题方式。

## 审计样本来源

- GUI/Widgets/Style.h：UIStyleSet / FWidgetStyle / Reactive<FWidgetStyle>（样式原语）
- UIText：已支持 bindStyle / resolvedStyle（唯一真正消费 style binding 的控件）
- UIButton / UITabBar / UITabButton / UIMenuBar / UIMenuBarItem / UIPanel / UIDockSpace / UIDockFloatingWindow：各自持有裸外观字段
- FWorkbenchStyle + WorkbenchSurface / WorkbenchDemoPages：app 层临时 token + 遍历覆写

## 控件 -> 外观数据现状

| 控件 | 当前外观兜底 | 是否走 style binding | 反模式 |
|---|---|---|---|
| UIText | _color / _fontSize | 是（bindStyle/resolvedStyle） | 无 |
| UIPanel | setColor | 否 | app 层直接 setColor（硬编码） |
| UIButton | _normalColor/_hoveredColor/_pressedColor/_focusedColor | 否 | app 层构造时覆写字段 |
| UITabButton | _textColor/_normalColor/_hoveredColor/_selectedColor/_accentColor/_padding | 否 | 裸字段；addTab 从 UITabBar 复制 |
| UITabBar | _tab*Color + _bottomRuleColor（过渡改动） | 否 | addTab 复制给子按钮 |
| UIMenuBarItem | _textColor/_normalColor/_hoveredColor | 否 | 裸字段；addItem 从 UIMenuBar 复制 |
| UISplitPane | divider 颜色（paint 硬编码） | 否 | paint 内 magic color |
| UIDockSpace | canvas 底色（paint 硬编码） | 否 | paint 内 magic color |
| UIDockFloatingWindow | body/border + edge handle 色（paint 硬编码） | 否 | paint 内 magic color；FResizeHandle 是 UIElement 子类直接 paint |

## 反模式清单（下一阶段要消除的）

### AM-1: framework 控件裸颜色字段扩张

- 位置：UITabButton / UIMenuBarItem / UIButton 头文件；UITabBar / UIMenuBar 的工厂方法
- 现象：每类控件暴露一组 _xxxColor / _xxxPadding 字段；父容器 addTab/addItem 时逐字段复制给子控件
- 问题：
  - 外观数据与控件实现耦合；
  - 状态（hover/selected 等）对应的颜色仍由控件逻辑分支决定，没有统一 style 表；
  - 想换主题只能改字段默认值或挨个覆写。
- 目标：改由 typed style（FTabStyle / FMenuBarItemStyle / FButtonStyle）resolve，控件只维护状态。

### AM-2: app 层临时 token header + 遍历 children 覆写

- 位置：FWorkbenchStyle / defaultWorkbenchStyle；WorkbenchSurface 构造后挨个 `_tab*Color = kStyle.xxx`、`_item*Color = kStyle.xxx`
- 现象：把颜色集中到一个 struct，但使用方式仍是手工搬到字段上
- 问题：
  - `FWorkbenchStyle` 放在 Tooling/Workbench 私有目录，未进公开 include，无法被 framework 解析；
  - 仍需要 app 遍历或逐个赋值，没有真正 resolve 链；
  - 与 AM-1 叠加后，改一处主题至少要动一组控件字段。
- 目标：WorkbenchTheme / UITheme 提供 style key 映射，控件自行 resolve；app 不再手工覆写控件字段。

### AM-3: paint 内 magic color

- 位置：UISplitPane / UIDockSpace / UIDockFloatingWindow（含 FResizeHandle）
- 现象：颜色直接写在 paintSelf / builder.addSprite 调用里
- 问题：这类外观既有“控件语义色”也有“主题色”，统一后应从对应 typed style resolve。
- 目标：引入对应的 typed style 字段，paint 读取而非内联常量。

## 与现有 UIStyleSet 的关系

- UIStyleSet + Reactive<FWidgetStyle> 是有效基础，证明样式变化可触发依赖重绘；
- 但 FWidgetStyle 只含 fillColor/textColor/fontSize/padding，不足以承载真实 shell 控件；
- 第一阶段在其之上扩展 typed styles + theme context，不推翻 reactive 失效链。

## 决策：stash 中的过渡改动弃用

- 此前曾做过一批 style token 收拢（UIMenuBar/_item*、UITabBar/_tab*、WorkbenchSurface 用 FWorkbenchStyle）。
- 该批改动与计划方向冲突：它给 framework 控件加了裸字段（扩大 AM-1），又用私有 token header 手工搬运（保留 AM-2）。
- 结论：该批 stash（stash@{0}，含 dock 提交前的 staged dock 副本）不恢复为工作代码；仅作为本 audit 的样本。dock 本体已单独提交（9eccc54d）。

## Phase 0 完成标准

- [x] 审计核心 shell 控件现有外观字段
- [x] 列出哪些已有 style binding、哪些没有
- [x] 明确不把临时 token header 当终局机制
- [x] 建立控件 -> 目标 typed style（Phase 1）映射：
  - UIText -> FTextStyle
  - UIPanel -> FPanelStyle
  - UIButton -> FButtonStyle
  - UIMenuBarItem/UIMenuBar -> FMenuBarItemStyle
  - UITabButton/UITabBar -> FTabStyle
  - UISplitPane -> FSplitPaneStyle（divider 色）
  - UIDockSpace -> FDockSpaceStyle（canvas/预览色）
  - UIDockFloatingWindow -> FFloatingWindowStyle（body/border/edge/标题色）

> ⚠ Phase 1 落地遗漏（2026-08-21 评审发现）：上表承诺的 **FSplitPaneStyle 与 FScrollBarStyle 未在 Style.h 落地**；SplitPane 的 divider 色被错误塞进 `FDockSpaceStyle::splitDividerColor`。UISplitPane 是通用控件（非 dock 场景也在用），不应从 dock style 取色，且其三个 divider 状态（`_dividerColor/_dividerHoveredColor/_dividerDraggingColor`）被压成单一静态色。Phase 2 前必须补独立 `FSplitPaneStyle`（divider 三态），从 FDockSpaceStyle 移除 splitDividerColor。

