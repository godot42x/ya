# Session Checklist — RPG Tactics (v2)

## 开工

1. 读 `plan.md` §7 当前 checkpoint 与 §2 边界，复述目标；这一轮只推进一个 checkpoint。
2. 写出这个 checkpoint 的引擎改动是被哪段游戏需求逼出来的（plan §7「逼出来的需求」）；写不出来就不做。
3. `git status`：工作区可能有其他线的未提交改动（`Engine/Plugins/log.cc`、`Engine/Shader/Slang/Common/Limits.slang` 等）。
   只按文件显式 stage，禁止 `git add -A`。提交前 `git diff --cached --stat` 核对。
4. 查活跃线是否在改同一处：
   - `rg -n "registerModuleFunction|GameplayScriptFunctions|Sprite2DFeature|PostProcessingState" Engine/Source`
   - 看 `.agent/plan/showcase-roadmap/plan.md`（A1 音频）、`render-view-family/`、`shadow-and-light-line/` 最新条目。
5. 多 agent 并行时用锁：`Script/agent/ya_lock.sh run -- <cmd>`；文件范围不重叠。

## 收尾

1. 构建：`xmake b ya-game-runtime ya-game-editor ya-testing 2DRpgPrototype`（新 target 一并加）。
2. 测试：`xmake r ya-testing`；本 checkpoint 相关用例全绿，总数与上一轮对比。
3. 冒烟：
   ```
   python3 Script/ya.py run --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- --exit-after-frame=120 --log-level=warn
   ```
   exit 0；有显示器的环境补 `run-editor` 冒烟。
4. 端到端（战斗相关）：`Script/automation/2d-rpg/` 下对应脚本；新增战斗流脚本。
5. 核对边界：
   - 没有新增 `Node2D` / `Transform2D` / `Camera2D`；
   - 新脚本函数没有直接写 sol2（走中立层 `registerModuleFunction`）；
   - 没有在帧录制中途重建 GPU 资源；粒子/后处理资源变更延迟到安全时机；
   - 新组件都反射，能过边界的字段自动可见。
6. 更新 `progress.md`（完成 / 验证 / 保留与未完成 / 手测步骤）、`feature_matrix.json`（status + evidence）、
   `todo.md`；与代码同一提交。
