# session checklist — gui-anchor-to-slot

每轮开工 / 收尾固定步骤：

## 开工
- [ ] 读 `../../AGENTS.md` 确认默认路径仍是 gui-framework > ya-build。
- [ ] 读本目录 `plan.md` §3 确认当前 checkpoint。
- [ ] grep 全仓 `fillParent|setAnchors|fillWidth|fillHeight` 确认起点范围。

## 收尾（每个 checkpoint）
- [ ] 改完跑 `python3 Script/ya.py cfg` + 对应 target 构建。
- [ ] 跑 `python3 Script/ya.py test --target ya --filter Suite.Test`。
- [ ] 追加 `progress.md` 本轮 checkpoint 记录（做了什么 / 验证命令 / 剩余问题）。
- [ ] 更新 `feature_matrix.json` 对应 scenario 状态。
- [ ] 更新 `todo.md` 勾选。
- [ ] 检查 diff 再提交，单 checkpoint 单 commit，格式 `[gui/declarative] message`。
