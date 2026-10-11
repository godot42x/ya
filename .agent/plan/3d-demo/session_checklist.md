# Session Checklist

## 开工

1. 读 `plan.md` §1 决策与 §2 波次，从 `todo.md` 当前波次选未完成项；复述目标与边界。
2. 先解两个待确认项（若用户尚未拍板）：示例项目目录名、素材选型（plan §3 / §4）。
3. 查冲突面（plan §5）：物理模块 / 资源导入器 / 输入系统 / AppLifecycle.cpp / 脚本绑定，同一时间一个 worker；派发 prompt 写明 `Script/agent/ya_lock.sh run --` 排队与文件范围。
4. 脚本绑定类工作（G3.2 / G3.7）先确认 `reflection-runtime` RR8 时序。
5. 外部依赖盯梢（`todo.md` 底部）：shadow-and-light-line（聚光灯阴影 / R2 / R3）、A1、F5（perf-baseline 立项）。
6. git status：只按显式路径 stage，不碰用户与并行 worker 的未提交文件。

## 收尾

1. 构建 + `python3 Script/ya.py test --target ya --filter Suite.Test` 全绿；改了窗口 present / 初始化路径则 GUIWorkbench smoke exit=0 是硬门禁。
2. `todo.md` 勾项、`feature_matrix.json` 状态与证据、`progress.md` 记一条；计划文件改动与代码同一提交。
3. checkpoint 说明保留 / 未完成 / 偏离项；方向偏离先停手报告，不用新提交掩盖。
