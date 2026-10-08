# Session Checklist

## 开工

1. 读 `plan.md` §1 决策、§3 当前 checkpoint、§4 边界，读 `todo.md`。
2. 确认测试基线（`python3 Script/ya.py test --target ya` 全量通过数）。
3. 动 `Engine/Plugins/reflects/` 前确认 `reflects-core` 仍零依赖（xmake 无新增 deps / packages）。

## 收尾

1. 全量 `python3 Script/ya.py test --target ya`；RR0 的 characterization 测试必须原样通过。
2. 涉及调用路径的 checkpoint 跑脚本调用微基准，与 RR0 基线对照。
3. 冒烟：`Script/automation/2d-rpg/walk_one_cell.py`；GreedySnake `--exit-after-frame=90`。
4. 更新 `todo.md` / `progress.md` / `feature_matrix.json`。
5. 不 commit；主 agent 审查后提交。
