# Session Checklist

## 开工

1. 读 `plan.md` 与 `todo.md`，复述当前目标与边界；返工项先核对 plan §4 的返工原因。
2. gui profile 相关构建一律在独立 worktree / 构建目录做（plan §2），不在共享工作区 `xmake f --ya_profile=gui`。
3. 基准 / 门禁改动先确认 perf-baseline 的基准命令可用；性能结论必须带数字（profile 口径；debug 短跑不算基线）。
4. 冲突面核对：`Script/ci.sh`、`Script/automation/gui/*`、`Example/GUIWorkbench/*`、GUI host 文件同一时间只允许一个 worker 改。
5. git status：只按显式路径 stage，不碰用户的未提交文件。

## 收尾

1. 门禁全绿：`workbench gate`（或当时等价的一条命令）+ `ya-gui-closure-test` / GUIWorkbench smoke；碰过窗口 present / 初始化路径时 smoke exit=0 是硬门禁。
2. 更新 `todo.md`、`feature_matrix.json`，在 `progress.md` 记一条；计划与代码同一提交，按显式路径 stage。
3. 收口的项写吸收笔记（问题 / 方案取舍 / 关键代码位置 / 面试追问）。
4. Windows 相关数字与结论标注来源，由用户回传。
