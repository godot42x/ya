---
name: gui-gallery-design
description: 用 desktop GUI 的成熟信息架构与视觉层级规则，驱动 YA GUIWorkbench / feature gallery / dock demo 的体验收口。
---

## 适用场景

- 调整 `Example/GUIWorkbench/` 的信息架构、shell、visual hierarchy
- 为 retain-mode 工具 UI 设计 sidebar / content / panel / dock 的最小规则
- 在不引入 CSS/QSS/DSL 的前提下，为 style key / theme 内容提供约束

## 目标

不是追求某个特定品牌视觉，而是让 Workbench 更像一个真正的工具型 GUI app：

- shell 清楚
- 导航分层清楚
- 主内容区是主角
- dock/floating 的操作反馈自然
- gallery 页面是“能力展示”，不是“控件堆叠”

## 规则 1：Shell 信息架构

默认采用四层：

```text
Top menu / app actions
Left navigation sidebar
Main content surface
Bottom status / hints
```

约束：

- Sidebar 是一级导航，不是内容区里的一个 panel。
- Sidebar 必须可调宽；默认 220~260 px，最小不低于 192 px。
- 主内容区与 sidebar 之间必须由 split / gutter / elevation 明确分界。
- 不把 feature 列表平铺到顶部 tab strip；feature gallery 优先走左侧导航。

## 规则 2：Surface 层级

至少区分以下视觉层：

```text
panel.window        app 最深背景
panel.sidebar       左侧导航背景
panel.sidebar.card  左侧导航承载卡片 / 分组容器
panel.surface       右侧主内容承载面
panel               普通工具 panel
panel.canvas        画布 / viewport / preview 区
```

约束：

- 不同层级主要靠 surface、间距、gutter、elevation 区分，不靠大量边框。
- Sidebar card 应明显高于 sidebar backdrop。
- Content surface 应明显高于 app background。
- Canvas 是最深的“工作面”，而不是和普通 panel 同层。

## 规则 3：Spacing / Typography

采用 4 px spacing ramp：

- 4 / 8 / 12 / 16 / 24 / 32

建议：

- 控件内 padding：8~16
- section 间距：12~16
- 页面外边距：12~16
- 分组块之间：16~24

文字：

- section label / eyebrow：10~11 px
- 普通 UI：13~14 px
- 避免在狭窄 dock tab 里放超长标题；必要时缩写或允许裁切。

## 规则 4：Sidebar 导航

Sidebar 不是一整列等权按钮。

推荐结构：

```text
Core
  Render / Widgets / Layout
Interaction
  Menus / DragDrop / Modal / ScrollSplit
Advanced
  Gallery / Interactions / Dock / Theme
Reference
  Editor
```

约束：

- 导航项可点击区要整行一致。
- selected 态优先用 fill + accent，不只是一条线。
- hover / selected / keyboard focus 必须可区分。
- 若暂时无法做真正分组，至少在视觉上把导航承载到一个 card 中。

## 规则 5：Dock UX

Dock 的目标不是“能拖动”而已，而是形成自然的工具工作区体验。

默认原则：

- 中央内容最大（如 Scene / Viewport）
- 工具面板在边缘（Hierarchy / Inspector / Console / Assets）
- merge preview 不要整块遮蔽目标 pane
- split preview 应表现为 insertion gutter / edge target
- floating window 必须有明确 title strip、body、边缘 resize affordance

约束：

- merge：以 outline + tab/header 强提示为主，fill 轻量
- split：以 edge strip 为主，不用整屏蓝板
- self-hover / self-drop 不显示有效 dock 提示
- drag title / drag tab / re-dock 的手势必须一致

## 规则 6：Gallery 页面模板

每个 feature page 都按统一结构组织：

```text
Page title
One-line purpose
Interactive demo area
Controls / knobs / state readback
Expected behavior hint
```

约束：

- page 要说明“这是展示什么能力”，不要只堆控件。
- 可交互区域与说明区分层明确。
- demo log / status hint 应简短，避免底部一长串说明挤压主内容。

## 规则 7：实现边界

- style mechanism 属于 framework
- theme content / visual language 属于 app
- 不为一个 demo 页面引入特化控件分支
- 优先通过 theme key、shell 结构、spacing、layout 修正体验
- 在没有 selector/CSS 系统前，避免把视觉状态硬编码回业务控件字段

## 落地顺序

1. 先收 shell：menu / sidebar / content / status
2. 再收 surface hierarchy：sidebar / card / content / canvas
3. 再收 dock default layout 与 preview affordance
4. 最后收 gallery page 的分组、文案、节奏

