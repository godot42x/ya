# Session Checklist

## 开工前

- [ ] 阅读根 AGENTS.md、.agent/plan/AGENTS.md。
- [ ] 按任务读取 ya-build、render-arch；改 GUI compose 时再读取 gui-framework。
- [ ] 查看 git status、最近相关提交和 progress.md。
- [ ] 复述本轮唯一 checkpoint、边界、保留项和非目标。
- [ ] 用 rg 核对当前符号和调用方，不以旧计划路径替代源码事实。
- [ ] 确认不会重复实现 gui-multi-os-window-editor 已有的 surface/present 能力。

## 实施中

- [ ] 只修改当前 checkpoint 所需的抽象、调用点和测试。
- [ ] 不把 Forward/Deferred 策略差异抽成万能基类。
- [ ] graph execute 不查询 ECS、Scene、live WidgetTree 或 ResourceResolveSystem。
- [ ] 不在 command recording 中途重建 GPU 资源，检查 deferred deletion/lifetime。

## 收尾前

- [ ] 运行受影响的 XMake build/test，target 名称以 xmake l targets 为准。
- [ ] 运行对应 golden/trace；R2 后增加双 View/双 Surface。
- [ ] 检查 git diff --check、staged diff 和生成文件。
- [ ] 更新 progress.md、feature_matrix.json、todo.md。
- [ ] 明确记录保留项、未完成项和偏离项。
- [ ] 代码、测试、plan/progress 使用同一 checkpoint 提交。
