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

