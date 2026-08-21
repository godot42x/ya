# GUI Style System 收敛 TODO

> 更新时间：2026-08-21

## 当前主线

- [x] Phase 0 — style capability audit
- [ ] Phase 1 — typed style structs
- [ ] Phase 2 — theme context / resolve 链
- [ ] Phase 3 — 核心 shell 控件去硬编码
- [ ] Phase 4 — GUIWorkbench theme 接入
- [ ] Phase 5 — Editor/Game 扩展留口

## Phase 0 — capability audit

- [x] 审计核心 shell 控件现有硬编码字段
- [x] 审计 UIStyleSet / FWidgetStyle 真实使用点
- [x] 列出控件 -> 目标 typed style 映射表
- [x] 标注过渡 token 改动只保留在 app/demo 层

## Phase 1 — typed style structs

- [x] 新增 FTextStyle
- [x] 新增 FPanelStyle
- [x] 新增 FButtonStyle
- [x] 新增 FMenuBarItemStyle
- [x] 新增 FTabStyle
- [x] 新增 FDockSpaceStyle
- [x] 新增 FFloatingWindowStyle
- [x] 定义 framework fallback 默认值（成员默认即 fallback）
- [ ] 定义状态命名约定（已定：*Fill + textColor + padding + accent，待文档化）
- [x] **补 FSplitPaneStyle（divider 三态）** —— 已落地（dividerFill/dividerHoveredFill/dividerDraggingFill），divider 色从 FDockSpaceStyle::splitDividerColor 迁出
- [x] **补 FScrollBarStyle** —— 已落地（trackColor/thumbColor/width），默认值从 UIScrollViewport 三字段复制
- [x] typed style 的 operator== 改反射生成（reflectEqual<T> 或 YA_REFLECT），禁手写 —— **实际用 C++20 `= default` 更简洁**（编译器生成逐字段比较，字段增删永不漏改）
- [ ] **FBrush 抽象（第一阶段必须，蓝图调研新增）** —— tintColor + resource + drawType（Image/NinePatch/Border）+ margin 九宫格；纯色 = 无 resource 退化形态；typed style 字段从 glm::vec4 升级为 brush 引用
  - [x] FBrush 类型落地（Brush.h）
  - [x] UIFrameBuilder::addBrush 对接（Image 型复用 addSprite）
  - [ ] NinePatch/Border 渲染（UV 子区域切片：暴露 drawTextureInternal 的 uvTranslation）

## Phase 2 — theme runtime

- [x] 设计 UITheme —— Theme.h（组合泛型 UIStyleSet，define/find<TStyle> 委托）
- [ ] 设计 UIThemeContext —— WidgetTree 已挂树级 theme（setTheme/getTheme）；subtree override 留后续
- [ ] 设计 style key 命名规则 —— resolveThemeStyle<TStyle>(key, level) helper 已落地，key 命名约定待 Phase 4 定
- [x] 确定 WidgetTree / GUIWindowHost 的 theme owner 边界 —— 挂 WidgetTree（树级资源）
- [ ] 实现 resolve 顺序：explicit override -> style key -> subtree override -> tree/window theme -> framework fallback —— 第一刀只做 style key -> fallback
- [x] **UIStyleSet 泛型化**：`define<TStyle>(name, style)`，typed styles 复用 Reactive<T>，不另起第二套容器
- [x] **resolve 上游换人失效传播**：WidgetTree 持 `Reactive<uint64_t>` generation token，setTheme 时 +1 触发依赖控件重绘
- [ ] **token → typed style 转换**：配置代码烘焙（app 构造 theme 时用 token 初始化 typed style），framework 不做运行时 token 求值（见 plan.md §3.4）
- [x] **white/dark 切换端到端验收**：Theme 页 toggle 换 UITheme，按钮 sprite 颜色 dark 0.16→light 0.94（draw item 层）+ assert_validation_clean 零漏标脏

## Phase 3 — 第一批控件接入

- [ ] UIText -> FTextStyle
- [ ] UIPanel -> FPanelStyle
- [ ] UIButton -> FButtonStyle
- [ ] UIMenuBarItem/UIMenuBar -> FMenuBarItemStyle
- [ ] UITabButton/UITabBar -> FTabStyle
- [ ] UISplitPane -> FSplitPaneStyle（divider 三态）
- [ ] UIDockSpace -> FDockSpaceStyle
- [ ] UIDockFloatingWindow -> FFloatingWindowStyle
- [ ] **resolve 读取路径契约**：paint 属性在 paintSelf 内走 `Reactive<T>::get()`，禁止缓存 resolved style 进成员（防 layout→paint 漏标脏）

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

