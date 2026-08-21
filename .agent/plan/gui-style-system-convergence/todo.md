# GUI Style System 收敛 TODO

> 更新时间：2026-08-21

## 当前主线

- [ ] Phase 0 — style capability audit
- [ ] Phase 1 — typed style structs
- [ ] Phase 2 — theme context / resolve 链
- [ ] Phase 3 — 核心 shell 控件去硬编码
- [ ] Phase 4 — GUIWorkbench theme 接入
- [ ] Phase 5 — Editor/Game 扩展留口

## Phase 0 — capability audit

- [ ] 审计 UIText / UIPanel / UIButton / UIMenuBarItem / UITabBar / UISplitPane / UIDockSpace / UIDockFloatingWindow 当前硬编码字段
- [ ] 审计 UIStyleSet / FWidgetStyle 当前真实使用点
- [ ] 列出控件 -> 目标 typed style 映射表
- [ ] 标注哪些过渡 token 改动只应保留在 app/demo 层

## Phase 1 — typed style structs

- [ ] 设计 FTextStyle
- [ ] 设计 FPanelStyle
- [ ] 设计 FButtonStyle
- [ ] 设计 FMenuBarItemStyle
- [ ] 设计 FTabStyle
- [ ] 设计 FDockSpaceStyle
- [ ] 设计 FFloatingWindowStyle
- [ ] 定义 framework fallback 默认值

## Phase 2 — theme runtime

- [ ] 设计 UITheme
- [ ] 设计 UIThemeContext
- [ ] 设计 style key 命名规则
- [ ] 确定 WidgetTree / GUIWindowHost 的 theme owner 边界
- [ ] 实现 resolve 顺序：explicit override -> style key -> subtree override -> tree/window theme -> framework fallback

## Phase 3 — 第一批控件接入

- [ ] UIText -> FTextStyle
- [ ] UIPanel -> FPanelStyle
- [ ] UIButton -> FButtonStyle
- [ ] UIMenuBarItem/UIMenuBar -> FMenuBarItemStyle
- [ ] UITabButton/UITabBar -> FTabStyle
- [ ] UIDockSpace -> FDockSpaceStyle
- [ ] UIDockFloatingWindow -> FFloatingWindowStyle

## Phase 4 — Workbench theme

- [ ] 定义 WorkbenchTheme
- [ ] FWorkbenchSurface 改为使用 theme key，而不是手工 child 覆写
- [ ] Dock demo / Editor demo / 通用 gallery 页统一接入 theme
- [ ] 产出截图与回归基线

## Phase 5 — 复用与扩展

- [ ] 约定 editor theme key 命名空间
- [ ] 约定 game HUD/menu/dialog theme key 命名空间
- [ ] 写清多窗口时 theme context 的 owner 与继承语义
- [ ] 判断第二阶段是否需要 selector / 外部文件 / DSL

