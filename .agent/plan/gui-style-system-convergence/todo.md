# GUI Style System 收敛 TODO

> 更新时间：2026-08-21

## 当前主线

- [x] Phase 0 — style capability audit
- [x] Phase 1 — typed style structs
- [x] Phase 2 — theme context / resolve 链
- [x] Phase 3 — 核心 shell 控件去硬编码
- [x] Phase 4 — GUIWorkbench theme 接入（WorkbenchTheme + 壳层主题化 + Gallery/legacy 迁移 + 基线门）
- [x] Phase 5 — Editor/Game 扩展留口（决策落定；实现型延伸可选）

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
- [x] **FBrush 抽象（第一阶段必须，蓝图调研新增）** —— tintColor + resource + drawType（Image/NinePatch/Border）+ margin 九宫格；纯色 = 无 resource 退化形态；typed style 字段从 glm::vec4 升级为 brush 引用
  - [x] FBrush 类型落地（Brush.h）
  - [x] UIFrameBuilder::addBrush 对接（Image 型复用 addSprite）
  - [x] NinePatch/Border 渲染（UV 子区域切片：draw item 带 uvOffset/uvScale，compose 透传到 makeSprite）

## Phase 2 — theme runtime

- [x] 设计 UITheme —— Theme.h（组合泛型 UIStyleSet，define/find<TStyle> 委托）
- [ ] 设计 UIThemeContext —— WidgetTree 已挂树级 theme（setTheme/getTheme）；subtree override 留后续
- [ ] 设计 style key 命名规则 —— resolveThemeStyle<TStyle>(key, level) helper 已落地，key 命名约定待 Phase 4 定
- [x] 确定 WidgetTree / GUIWindowHost 的 theme owner 边界 —— 挂 WidgetTree（树级资源）
- [x] 实现 resolve 顺序：explicit override -> style key -> subtree override -> tree/window theme -> framework fallback —— authored TStyle（含 setColor 写入）+ style key + fallback 已落地；subtree override 仍留后续
- [x] **UIStyleSet 泛型化**：`define<TStyle>(name, style)`，typed styles 复用 Reactive<T>，不另起第二套容器
- [x] **resolve 上游换人失效传播**：WidgetTree 持 `Reactive<uint64_t>` generation token，setTheme 时 +1 触发依赖控件重绘
- [ ] **token → typed style 转换**：配置代码烘焙（app 构造 theme 时用 token 初始化 typed style），framework 不做运行时 token 求值（见 plan.md §3.4）
- [x] **white/dark 切换端到端验收**：Theme 页 toggle 换 UITheme，按钮 sprite 颜色 dark 0.16→light 0.94（draw item 层）+ assert_validation_clean 零漏标脏

## Phase 3 — 第一批控件接入

- [x] UIText -> FTextStyle（_styleKey="text"，resolvedStyle→FTextStyle，FTextStyle 补 fillColor/padding；legacy bindStyle 保留为 fallback）
- [x] UIPanel -> FPanelStyle（_styleKey="panel"；显式 setColor() 覆盖胜出，未显式着色才 theme 驱动）
- [x] UIButton -> FButtonStyle（前轮活样本）
- [x] UIMenuBarItem/UIMenuBar -> FMenuBarItemStyle（_styleKey="menubar"）
- [x] UITabButton/UITabBar -> FTabStyle（_styleKey="tab"，Layout 粒度；strip chrome 入 FTabStyle）
- [x] UISplitPane -> FSplitPaneStyle（divider 三态，_styleKey="split"）
- [x] UIScrollViewport scrollbar -> FScrollBarStyle（_styleKey="scrollbar"，track/thumb brush + width）
- [x] UIDockSpace -> FDockSpaceStyle（_styleKey="dock"，canvas+preview）
- [x] UIDockFloatingWindow -> FFloatingWindowStyle（_styleKey="floating"，body/border/minSize/resize affordance）
- [x] **resolve 读取路径契约**：paint 属性在 paintSelf 内走 `Reactive<T>::get()`，禁止缓存 resolved style 进成员（TabButton/SplitPane/ScrollViewport/Dock/Floating 均已按此接线；UIText 沿用 _bAutoSize 粒度判据）
- [x] UITreeView -> FTreeViewStyle（_styleKey="tree"）
- [x] UITextField -> FTextFieldStyle（_styleKey="textfield"）
- [x] UIMenu/UIMenuItem -> FMenuStyle（_styleKey="menu"，panel 走 menu.panel FPanelStyle）
- [x] UISelectableRow -> FSelectableRowStyle（_styleKey="selectable"）
- [x] UIDragFloat -> FDragFloatStyle（_styleKey="dragfloat"）
- [x] UICheckBox -> FCheckBoxStyle（_styleKey="checkbox"）
- [x] UIComboBox -> FComboBoxStyle（_styleKey="combobox"）
- [x] UISlider -> FSliderStyle（_styleKey="slider"）
- [x] UITableGrid -> FTableGridStyle（_styleKey="table"）
- [x] UISpinBox / UIRadioButton / UIColorEdit chrome / UISearchComboBox -> typed styles

## Phase 4 — Workbench theme

- [x] 定义 WorkbenchTheme（WorkbenchTheme.h：tokens 层 + buildWorkbenchTheme(dark/light)，全部 canonical key 配置期烘焙）
- [x] 补表单/文案角色 key（tree/textfield/menu/selectable/dragfloat + checkbox/combobox/slider/table/spinbox/radio/coloredit/searchcombo + text.header/muted/error/eyebrow），避免 light theme 落到 dark fallback
- [x] GameEditor `buildEditorTheme` 挂 EditorSurface；chrome 去掉 setColor 字面量
- [x] FWorkbenchSurface 改用 theme key（壳层 chrome 交回 panel.window/panel.canvas/panel；highlight 保留显式色；toolbar label 走 text key）
- [x] Dock demo / Editor demo / 通用 gallery 页统一接入 theme（canonical key 全部经 WorkbenchTheme；Gallery Section 3 迁移到 tree theme，FWidgetStyle/bindStyle app 消费点清零）
- [x] 产出截图与回归基线（Script/gui_style_baseline.py 5 页 headless digest 基线归档）
- [x] **清理刀**：已迁移控件裸颜色字段全部删除（Button/MenuBarItem/TabButton/SplitPane/ScrollViewport/Dock/Floating），fallback=默认构造 TStyle；WorkbenchSurface 遍历覆写循环删除（AM-2 收口）；消费点迁移完成（含 Dialog/GUIFrameworkSmoke/两测试）

## Phase 5 — 复用与扩展（legacy 清理已完成）

- [x] 约定 editor theme key 命名空间（§3.6 + Phase 5 决策 1：family 领域无关，editor.* 前缀仅显式覆盖场景）
- [x] 约定 game HUD/menu/dialog theme key 命名空间（同 §3.6 机制；game.* 前缀限于覆盖场景；HUD 内容 = game theme 烘焙）
- [x] 写清多窗口 theme context owner 与继承语义（Phase 5 决策 2：挂载点=tree，owner=app，共享/各挂安全；subtree override 机制候选明确）
- [x] 判断第二阶段是否需要 selector / 外部文件 / DSL（暂不需要；typed style + key 烘焙已满足）

