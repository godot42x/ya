# GUI Style System 收敛进度记录

> 建立日期：2026-08-21
> 作用：记录 style system 收口过程中的已完成切片、阶段证据、当前阻塞与下一轮接力点。

## 2026-08-21 — 计划建立

（见 7720e428：plan/progress/todo/checklist/matrix 建立）

## 2026-08-21 — Phase 0 audit 完成

- 明确反模式：AM-1 framework 裸颜色字段扩张、AM-2 app 临时 token header + 遍历覆写、AM-3 paint 内 magic color；
- 建立控件 -> 目标 typed style 映射表（落盘 phase0-audit.md，a38c1d36）；
- 弃用 style token stash（stash@{0} 已 drop），过渡文件 WorkbenchStyle.h 移至 /tmp。

## 2026-08-21 — Phase 1 typed style structs 落地

### 本轮完成

- 在 GUI/Widgets/Style.h 新增 typed style structs：
  - FTextStyle（textColor/fontSize）
  - FPanelStyle（fillColor）
  - FButtonStyle（normal/hovered/pressed/focused/disabled fill + textColor + padding）
  - FMenuBarItemStyle（text + normal/hovered fill）
  - FTabStyle（text + normal/hovered/selected fill + accent + padding）
  - FDockSpaceStyle（canvas + split divider + drop preview）
  - FFloatingWindowStyle（body/inner/border/edge affordance/title + minSize）
- 每种 style 的成员默认值即 framework fallback；
- 默认值从各控件当前外观复制，保证 Phase 3 迁移行为不变；
- 状态命名约定落定：*Fill（normal/hovered/pressed/focused/selected/disabled）+ textColor + padding + accent。

### 当前结论

- typed style 类型层已定型，framework fallback 已定义；
- Phase 1 只加类型，未接入控件（Phase 3 才接），现有行为零变化；
- ya-gui-widgets 编译通过。

### 当前未完成 / 风险

- 控件尚未从 typed style resolve（Phase 3）；
- 尚无 UITheme / UIThemeContext / style key（Phase 2）；
- FWidgetStyle 仍被 UIText 使用，与 typed styles 并存，属过渡状态。

### 下一轮直接接力点

1. Phase 2：UITheme 容器 + UIThemeContext owner 挂载点 + style key 命名；
2. Phase 3：第一批控件从 typed style resolve 并删除裸颜色字段。

### 本轮验证

- xmake b ya-gui-widgets 通过；
- 本轮仅新增类型，不改运行时行为，无需跑 scenario。

## 2026-08-21 — 架构评审 + 计划修订

三视角交叉评审（行业框架对照 / 引擎设施一致性 / 需求落地），发现并修订以下结构性空洞：

- **B1（blocker）resolve 上游换人失效传播缺失**：Reactive 只覆盖「值变」，覆盖不了「换 theme」。UIThemeContext 须持 Reactive<UITheme> 或换 theme 走 invalidateSubtreePaint；white/dark 切换列为 Phase 2 显式端到端验收。
- **B2（major）FSplitPaneStyle 落地遗漏**：plan/audit 承诺了但 Style.h 缺失，divider 色被错误塞进 FDockSpaceStyle::splitDividerColor；UISplitPane 是通用控件，三态 divider 被压成单色。Phase 2 前补。
- **B3（major）UIStyleSet 命运未定**：写死 Reactive<FWidgetStyle> 无法承载 typed styles。决定泛型化为 define<TStyle>。
- **B4（major）token→typed style 转换未讲清**：决定配置代码烘焙（app 构造 theme 时 token 初始化 typed style），framework 不做运行时求值。
- **M1（major）手写 operator== 漏改触发静默漏标脏**：改反射生成。
- **M2（major）white/dark 切换未作为显式验收**：补进验证计划。
- **M3（major）paint resolve vs 缓存未定义**：写死 paintSelf 内 get()，禁缓存。
- **次要点**：状态态「统一七态」改为「全局词汇表 + 每控件声明子集」；tool-only style 分层待 Phase 5 明确。

修订落盘：plan.md（§3.1/3.2.1/3.2.2/3.3/3.4 + Phase 1/2/3 + §7）、phase0-audit.md、feature_matrix.json（split_pane_style=fail、reflect_equal/theme_toggle_e2e 等新 item）、todo.md。

## 2026-08-21 — FSplitPaneStyle 代码层补漏（B2 收口）

- Style.h 新增 `FSplitPaneStyle`（dividerFill/dividerHoveredFill/dividerDraggingFill 三态，默认值复制 UISplitPane 的 `_dividerColor/_dividerHoveredColor/_dividerDraggingColor`）；
- `FDockSpaceStyle::splitDividerColor` 已移除（原值 0.28/0.30/0.36 既非 normal 也非 hovered，是状态丢失的折中静态色）；
- 本次仅补类型 + fallback，不接控件（Phase 3 才让 UISplitPane 从 FSplitPaneStyle resolve）；
- ya-gui-widgets 编译通过；feature_matrix split_pane_style → pass，todo 勾选。

**剩余未补**：FScrollBarStyle（plan 承诺但 Style.h 仍缺，需先确认 UIScrollBar 现有裸字段再定 fallback 值）。

## 2026-08-21 — FScrollBarStyle 代码层补漏（B2 全部收口）

- Style.h 新增 `FScrollBarStyle`（trackColor/thumbColor/width，默认值复制 UIScrollViewport 的 `_scrollbarTrackColor/_scrollbarThumbColor/_scrollbarWidth`）；
- `_bShowScrollbar` 保留为控件行为开关，不进 style（visibility 是行为，颜色/宽度才是样式）；
- 本次仅补类型 + fallback，不接控件（Phase 3 才让 UIScrollViewport 从 FScrollBarStyle resolve）；
- ya-gui-widgets 编译通过；feature_matrix scroll_bar_style → pass，todo 勾选。

**Phase 1 至此完整**：计划承诺的 9 个 typed style struct 全部落地（FTextStyle/FPanelStyle/FButtonStyle/FMenuBarItemStyle/FTabStyle/FSplitPaneStyle/FScrollBarStyle/FDockSpaceStyle/FFloatingWindowStyle）。剩余 reflect_equal（== 改反射）是独立待办，非遗漏。

