# Session Checklist — gui-kernel-ux-parity

## 开工前

- [ ] 读根 `AGENTS.md`、`.agent/skills/gui-framework/SKILL.md`、本目录 `plan.md`
- [ ] 复述当前 checkpoint 的单一验收目标和明确不做项
- [ ] 检查工作树：不把 PropertyHandle / Windows / OpenGL 脏文件卷进来
- [ ] 确认本轮只改文档还是同时改代码/测试

## 实施中

- [ ] 只处理当前 checkpoint
- [ ] 不合并 GUIApp 与 ya::App
- [ ] 不按行数拆 WidgetTree / UILayout / GUIAppHost
- [ ] 不为 1–2 个文件发明新目录
- [ ] 不停靠 tab 拖与浮窗标题拖抽 helper
- [ ] 发现方向偏离先停，修订本计划，不继续堆提交

## 收尾前

- [ ] 跑本轮最小验证（Gallery 状态组合或 editor 手测清单，不只 dump 单测）
- [ ] 检查 `git diff` 与无关文件
- [ ] 更新 `progress.md`、`todo.md`、`feature_matrix.json`
- [ ] 代码/文档与计划工件同一 checkpoint 提交，格式 `[gui] ...`
- [ ] 回报保留项、未完成项、偏离项
