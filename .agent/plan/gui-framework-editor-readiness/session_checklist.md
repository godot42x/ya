# Session Checklist

## 开工前

- [ ] 读取根 `AGENTS.md`、`.agent/plan/AGENTS.md` 和适用 skill。
- [ ] 复述当前 checkpoint 的单一验收目标。
- [ ] 检查工作树，保留用户已有修改，尤其不要覆盖 `.gitignore`。
- [ ] 确认本轮只改计划还是同时改代码/测试。
- [ ] 读取上一轮 `progress.md`、`todo.md`、`feature_matrix.json`。

## 实施中

- [ ] 先写失败测试或契约断言。
- [ ] 只处理当前 phase 的边界。
- [ ] 若发现设计误差，暂停编码并修订计划。
- [ ] 不恢复 legacy API、兼容别名或旧 serialized file。
- [ ] 保持 layout parent-owned、slot-first 和 static DSL direct-live 语义。

## 收尾前

- [ ] 跑最小相关测试。
- [ ] 跑 GUI closure/headless；有渲染影响时跑 windowed/offscreen/GPU parity。
- [ ] 检查 `git diff` 和无关文件。
- [ ] 对照 feature matrix 更新状态和证据。
- [ ] 更新 `progress.md`、`todo.md`。
- [ ] 确认代码、测试、计划文件属于同一 checkpoint。
- [ ] 使用 `[gui] ...` 提交一个完整分类 checkpoint。
- [ ] 在回报中说明保留项、未完成项和任何偏离。

