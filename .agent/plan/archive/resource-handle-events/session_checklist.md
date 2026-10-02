# Session Checklist

## 开工

1. 读 `plan.md` §4 与当前 checkpoint 行；复述目标与边界，这一轮只推进一个 checkpoint。
2. `git status`：只按文件显式 stage，禁止 `git add -A`；不碰 `Engine/Plugins/log.cc`、`.vscode/settings.json` 和别人的未提交文件。
3. 查并发改动：
   `git log --oneline -10 -- Engine/Source/Framework/Resource Engine/Source/Framework/Core/Common Engine/Source/Framework/Render/Render3D/Services Engine/Source/Framework/Render/Render3D/Terrain Engine/Source/Framework/Render/Render3D/EnvironmentLighting`
4. 开工 H1 前：核实没有非游戏线程读写 ref / 槽（`rg -n "std::thread|TaskQueue::get\(\)\.submit" Engine/Source/Framework/Render`）。

## 收尾

1. `xmake b ya-game-runtime ya-game-editor ya-testing 2DRpgPrototype`
2. `xmake r ya-testing`；总数与上一轮对比，资源相关用例全绿。
3. 冒烟（均 `-- --exit-after-frame=120 --log-level=warn`）：
   - `python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject`
   - `python3 Script/ya.py run --project Example/2DRpgPrototype/2DRpgPrototype.yaproject`
   - `python3 Script/ya.py run --project Example/GreedySnake/GreedySnake.yaproject`
   - `python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject`
   - H4 起：HelloMaterial 截图自动化必须到 stable。
4. 性能（H1 基线、H5 验收）：命令同 `../rpg-prototype/r4-measurements.md` §2，场景 TownLarge，
   用 `../rpg-prototype/measure_trace.py` 读 `ResourceResolve/*`。
5. 删除检查（对应 checkpoint 完成后应为空）：
   - H1：`rg -n "TextureFuture|ResourceTable|PathRegistry|hardFailure|textureRef\.resolve|image\.resolve" Engine/Source`
   - H3：`rg -n "AssetFuture|getResourceVersion|bumpResourceVersion|\.resolve\(\)" Engine/Source -g '*Ref*' -g '*TextureSlot*'`
   - H2：`rg -n "needsResolve|onModified|sweepNeedsResolve" Engine/Source/Framework/Render`
   - H5：`rg -n "activeEntities|sweepAuthoringDirty" Engine/Source/Framework/Render`
6. 更新 `progress.md`（完成 / 验证 / 保留与未完成 / 手测步骤）、`feature_matrix.json`、`todo.md`，与代码同一提交。
