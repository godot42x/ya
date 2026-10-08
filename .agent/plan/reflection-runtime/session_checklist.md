# Session Checklist

## 开工

1. 读 `plan.md` §1 决策、§3 当前 checkpoint、§4 边界，读 `todo.md`。
2. 确认测试基线（`python3 Script/ya.py test --target ya` 全量通过数）。
3. 动 `Engine/Plugins/reflects/` 前确认 `reflects-core` 仍零依赖（xmake 无新增 deps / packages）。

## 收尾

1. 全量 `python3 Script/ya.py test --target ya`，以及 `xmake b reflects-core-test && xmake run reflects-core-test`、`reflects-generator-test` 同理；RR0 的 characterization 测试必须原样通过。
2. 涉及调用路径的 checkpoint 手动跑脚本调用微基准（`DISABLED_` 用例，`--gtest_also_run_disabled_tests`），与 RR0 基线对照。
3. 动序列化的 checkpoint：`Example/` 全部场景与资产元数据重存，与 RR0 基线零 diff。
4. Windows 验证点（RR1 / RR5b / RR8）：请用户在 Windows 上跑构建与 `ya-testing`，结果记入 `progress.md` 才算该 checkpoint 完成。
5. 冒烟：`Script/automation/2d-rpg/walk_one_cell.py`；GreedySnake `--exit-after-frame=90`。
6. 更新 `todo.md` / `progress.md` / `feature_matrix.json`。
7. 不 commit；主 agent 审查后提交。
