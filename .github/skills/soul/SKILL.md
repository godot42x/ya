---
name: ya-soul
description: 需求不确定时的澄清与决策引导
---

## 适用场景

- 用户目标不清晰，或描述存在歧义
- 需求包含多个方向，需要先收敛范围
- 技术判断依赖前置约束（平台、目标、性能、风格）

## 核心目标

1. 先澄清问题，再进入实现。
2. 明确假设、风险和优先级。
3. 给出可执行的下一步，并把任务转交到具体技能。

## 执行方式

- 给出 2~3 种合理解释，并标注各自假设。
- 优先提出最小必要澄清问题（1~3 个）。
- 若可默认，说明默认值并继续推进，不停留在讨论层。

## 交接规则

- 澄清完成后，立即切换主技能：
	- 构建/报错 -> `build`
	- VS Code / 调试配置 -> `vscode`
	- 资源加载 / resolve -> `resource-system`
	- 材质数据流 -> `material-flow`
	- 渲染架构 -> `render-arch`
	- C++ 风格与生命周期 -> `cpp-style`
	- 崩溃排查与 review -> `debug-review`

## 相关 skills

- `build`：处理 XMake 构建、目标选择、测试与 shader 生成
- `vscode`：处理工作区任务、调试配置、compile_commands
- `resource-system`：处理资源加载、resolve、environment lighting
- `material-flow`：处理材质 authoring / runtime 数据流
- `render-arch`：处理 RenderRuntime、后端边界、layout / pass
- `cpp-style`：处理风格、所有权与热路径约束
- `debug-review`：处理崩溃排查、diff 自检与提交前 review

## 退出条件

- 问题范围和验收标准已明确，且已选定下一主技能