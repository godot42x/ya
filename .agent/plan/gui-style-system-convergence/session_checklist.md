# GUI Style System 收敛 Session Checklist

> 更新时间：2026-08-21

## 每轮开工前

1. 先读 plan.md / progress.md / todo.md / feature_matrix.json；
2. 看工作区：git status --short；
3. 确认本轮只推进一个最小切片：audit / typed style / theme runtime / 某一批控件接入；
4. 若本轮只是继续调颜色，暂停，回到计划检查是否偏离 style runtime 收口；
5. 若涉及 framework/app 边界，先确认机制在 framework、主题内容在 app。

## 每轮进行中

1. 不把 WorkbenchStyle.h 之类过渡 token 误当终局机制；
2. 不继续给核心 shell 控件加新的 _normalColor/_hoveredColor/... 字段，除非是过渡接线且写明迁入 typed style；
3. 若控件需要自己的 style 结构，优先新增 typed style，而不是继续扩张 FWidgetStyle；
4. 若需要 tree/window/subtree 级主题切换，优先补 UIThemeContext owner 能力，不在 app 发明第二套注入协议；
5. 每次接入控件都要回答 style key、fallback 位置、状态态、是否可被 game/editor 复用。

## 每轮收尾前

1. 至少留下一条结构证据：typed style 定义表 / resolve 链说明 / owner context 图；
2. 至少留下一条运行证据：xmake b GUIWorkbench 与 relevant scenario/screenshot baseline；
3. 更新 progress.md / todo.md / feature_matrix.json；
4. 若形成稳定规则，回写 skill / AGENTS。

## 默认推进顺序

1. Phase 0 — capability audit
2. Phase 1 — typed style structs
3. Phase 2 — theme context / resolve 链
4. Phase 3 — 核心 shell 控件去硬编码
5. Phase 4 — GUIWorkbench theme 接入
6. Phase 5 — Editor/Game 扩展留口

## 当前下一刀

1. 产出 style capability audit
2. 拍板第一批 typed style structs
3. 拍板 UITheme / UIThemeContext 挂载点

