# 开工 / 收尾清单 — 编辑器本地化

## 开工

1. 读 `.agent/plan/editor-localization/plan.md` §1 边界与 §2 关键决策，确认本轮的
   单一可验收目标。
2. 构建/测试用 `Script/agent/ya_lock.sh run --` 包裹（共享 build 树）。
3. 改动的文件先 `git status`，确认不和并行线（RPG / render-composition）撞车。

## 收尾（每个 Phase）

1. `python3 Script/editor_locale_lint.py` — exit 0，无缺失 key。
2. `xmake r ya-testing` — 全绿（改动涉及 GUI 时至少跑相关 suite）。
3. 涉及 chrome 的改动实跑一次编辑器并截图确认（`--screenshot`）。
4. `git diff --cached --stat` 核对：本 phase 的文件，不混入无关改动。
5. 提交格式 `[editor/locale] Pn: ...`；一个 phase 一个提交，可独立构建。
6. 更新 `progress.md` / `todo.md` / `feature_matrix.json` 与本提交同一次落地。

## 共享仓库注意

- 本仓库多 agent 并行，`git commit --amend` 会改到别人的 HEAD：**禁止 amend**，
  需要修上一次提交时用新提交。
- 其他线在改 `Engine/Source/Framework/Render/Render3D/**` 时可能短暂编译失败；
  与本线无关的报错先确认归属，不要顺手改别人的文件。
