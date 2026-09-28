# Session Checklist

## 开工

1. 读本目录 `plan.md` §3 决策门与 `progress.md` 最近一轮；决策门未确认时先问用户，不开工。
2. 读根 `AGENTS.md`、`.agent/skills/gui-framework/SKILL.md`。
3. `git status --short`；保留他人脏改动，禁止 blanket stage。
4. 核对调用方：`rg -n "UIBehavior|wantsTick|tickSubtree|onClicked|onClick|canAcceptDrop|onDragDetected" Engine/Source Engine/Test`。
5. 先跑基准记下改前数据（临时测试，不提交）。

## 收尾

1. `python3 Script/ya.py test --target ya`；`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test`。
2. `xmake b GUIWorkbench && xmake r GUIWorkbench --smoke-actions`；GreedSnake / HelloMaterial `--exit-after-frame=60`。
3. 同一构建模式下重跑基准，结果写进 `progress.md`。
4. 代码、测试与计划工件同一提交；提交前 `git diff --stat` 与 `git diff --check`。
