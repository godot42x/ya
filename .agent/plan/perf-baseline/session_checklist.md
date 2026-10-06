# Session Checklist

## 开工

1. 读 `plan.md` §1 决策、§2 输出格式、§3 当前 checkpoint；复述目标与边界，一轮只推进一个 checkpoint。
2. git status：只按显式路径 stage；其他 worker 并行改的文件不要碰。
3. 高冲突面：`GameRuntime/Profiling.cpp`、`AppLifecycle.cpp`、`RenderGraphExecutor.cpp`、`VulkanRender.cpp`——同批次只允许一个 worker 改。

## 收尾

1. `python3 Script/ya.py test --target ya` 全绿，记录总数。
2. 冒烟：HelloMaterial / GreedySnake / 2DRpgPrototype `--exit-after-frame=120`、编辑器 `--exit-after-frame=30`，无新 ERROR。
3. 基准相关 checkpoint：贴一次实际运行的 `bench_summary.json` 关键字段；连跑三次的 p50 波动。
4. 更新 progress.md（完成 / 验证 / 保留 / 偏离 / 下一步）、todo.md、feature_matrix.json，与代码同一提交；同步 `../showcase-roadmap/` 的 todo 与 feature_matrix。
