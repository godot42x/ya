# Session Checklist

## 开工

1. 读本目录 `plan.md` 当前 phase 与 §6 决策门、`progress.md` 最近一轮、`feature_matrix.json` 未完成项。
   当前 phase 依赖的决策门未确认时，先问用户，不开工。
2. 读根 `AGENTS.md`、`.agent/plan/AGENTS.md`、`.agent/skills/gui-framework/SKILL.md`（涉及 GUI 时）。
3. `git status --short`；保留用户/其他 agent 的脏改动，禁止 blanket stage。
4. 只核对当前 phase 的调用方：
   - F0：`rg -n "_bPause|isPaused|tickLogic|gameUIHost->update|setPresentation|destroyNode|destroyEntity|_systems|TimerManager" Engine/Source`
   - S1：`rg -n "ScriptInstance|functionField|invokeLuaCallback|reloadScript|releaseLuaHandles" Engine/Source Engine/Test`
   - S2：`rg -n "UIDocument::|mountSceneAutoMountEntries|_behaviors" Engine/Source Engine/Test`
   - S3：`rg -n "setUiActionHandler|onUiAction|setMountedText|setMountedVisible|bindButtonActions|_action" Engine/Source Example`
   - S4：`rg -n "SceneWidgetEntry|_updateClock|EInputBlocking|dispatchInputFallbackEvent" Engine/Source`
   - S5：`rg -n "setGameKeyHandler|setKeyHandler|kSpriteTexture|rootSlot|resolveChildRect" Engine/Source Example`
   - S6：`rg -n "createAndMountGameUI|EditorLuaPreview|EditorUIDesigner" Engine/Source/Applications/GameEditor`
5. 复述本轮唯一可验收目标、保留项和不做项。

## 实施中

1. 先改契约/数据，再改宿主，再迁移内容；不要从 GreedSnake 的需求倒推特例 API。
2. GUI 模块不 include Lua / ECS / Scene；新增行为数据对 GUI 不透明。
3. 设计器与视口预览不激活脚本；任何新挂载路径都要显式传 activator 或 null。
4. 结构变更（实体销毁、控件 spawn/destroy、条目挂卸）只在 StructuralFlush 生效。
5. 不新增全局单槽回调；回调挂在实例上，由框架按顺序键派发。
6. 不新增中心事件总线或 UIManager。

## 收尾

1. 更新 `progress.md`：完成项、保留项、偏离项、失败测试和下一步。
2. 更新 `feature_matrix.json`：只有行为闭环可运行且测试绿才改 `verified`。
3. 构建与验证：
   - `xmake b ya-testing && xmake r ya-testing --gtest_filter=<本 phase 用例>`
   - `python3 Script/ya.py run --project Example/GreedSnake/GreedySnake.yaproject -- --exit-after-frame=60`
   - `python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=60`
   - 涉及编辑器：`python3 Script/ya.py run-editor --project Example/GreedSnake/GreedySnake.yaproject -- --exit-after-frame=60`
   - 涉及 GUI：`xmake b GUIWorkbench && xmake r GUIWorkbench --smoke-actions`
4. 删除审计：被替换的旧 API 用 `rg` 确认零调用方后再删，不留兼容壳。
5. 代码、测试与计划工件同一提交，提交前 `git diff --stat` 与 `git diff --check`；只在用户要求时提交。
