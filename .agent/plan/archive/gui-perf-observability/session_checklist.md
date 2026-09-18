# GUI Frame Inspector Session Checklist

> 更新时间：2026-09-08

## 开工前

1. 读仓库根 `AGENTS.md`、`.agent/skills/gui-framework/SKILL.md`、本目录四件套。
2. `git status --short`：只提交本任务文件。
3. `todo.md` 同时最多一个 `[-]`。
4. 复述单一目标、非目标和编译裁剪边界。

## 实施中

1. 重数据采集走 `YA_PROFILING_*`，不另造 define，不用 `NDEBUG`。
2. 不把 overlay 推进产品 snapshot。
3. 不把 HUD 做成被观测树里的 widget。
4. release 热路径不分配 rebuilt 名/rect vector。

## 收尾前

```bash
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='GuiFrameInspectorTest.*'
```

`git diff --check`；JSON 解析 `feature_matrix.json`；对照 diff 排除无关工作区。

## 当前下一刀

无。Mainline（GPO-001..005）完成。
