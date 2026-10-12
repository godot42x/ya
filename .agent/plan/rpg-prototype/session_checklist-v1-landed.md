# Session Checklist

## 开工

1. 读 plan.md §4 当前 checkpoint 与 §5 冲突规则；复述目标、边界，这一轮只推进一个 checkpoint。
2. 写出这个 checkpoint 的引擎改动是被哪段游戏需求逼出来的；写不出来就不做。
3. git status：工作区可能有其他线的未提交改动，只按文件显式 stage，禁止 `git add -A`。
4. 查活跃线是否在改同一处：
   rg -n "spawnSprite|setKeyHandler|onKey|bindGameplayLua" Engine/Source/Applications/GameRuntime/Script
   并看 `game-ui-script-framework/progress.md`、`ui-behavior-capabilities/progress.md` 最新条目。

## 收尾

1. xmake b ya-game-runtime ya-game-editor ya-testing 2DRpgPrototype（R0 之前没有 2DRpgPrototype 目标就跳过）
2. xmake r ya-testing；脚本与 tilemap 相关用例全绿，总数与上一轮对比。
3. 冒烟：
   python3 Script/ya.py run --project Example/GreedySnake/GreedySnake.yaproject -- --exit-after-frame=120 --log-level=warn
   python3 Script/ya.py run --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- --exit-after-frame=120 --log-level=warn
   python3 Script/ya.py run-editor --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- --exit-after-frame=120 --log-level=warn
4. 核对边界：没有新增 `Node2D` / `Transform2D` / `Camera2D`；新脚本函数没有直接写 sol2（B1 起）；
   没有新增渲染 pass。
5. 更新 progress.md（完成 / 验证 / 保留与未完成 / 手测步骤）与 feature_matrix.json，和代码同一提交。
