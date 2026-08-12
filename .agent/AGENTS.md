# Agent Workspace

先读仓库根 `AGENTS.md`，这里只负责二级路由。默认不要一次性加载全部内容。

## 入口原则

- 任务明确：直接进入对应单个 skill。
- 任务不明确：先读 `./skills/soul/SKILL.md` 做澄清。
- 只有遇到历史坑、回归、相似故障时，才读 `./memories/*.md`。
- `agents/` 是特定工作模式说明；只有确实需要时才打开。
- `prompts/` 是模板资产，不属于默认上下文。
- `misc/` 不是规范来源。

## 目录

- `./skills/AGENTS.md`：skill 索引
- `./memories/AGENTS.md`：memory 索引
- `./agents/AGENTS.md`：可选代理角色索引
- `./prompts/AGENTS.md`：提示模板索引
- `./plan/`：阶段性计划与进度，先读 `./plan/AGENTS.md`
- `./misc/`：临时分析资料

## 默认路由

1. 构建、运行、测试、target、shader 生成：`./skills/ya-build/SKILL.md`
2. GUI 框架（WidgetTree/控件/布局/pass slot/host）：`./skills/gui-framework/SKILL.md`
3. VS Code / clangd / launch / tasks：`./skills/vscode/SKILL.md`
4. 资源、材质、渲染、代码组织等专项问题：只进入对应单个 skill
5. 历史坑与回归：按需补充 memory

跨平台（Windows/MSVC 与 macOS/Clang 切换、DLL 导出、平台差异导致的编译/链接报错）：
`./skills/cross-platform/SKILL.md`

## 维护规则

- 稳定规则优先写 skill。
- 历史坑优先写 memory。
- 阶段性状态优先写 plan。
- 若某条信息不是“当前默认路径”，不要留在顶层入口。
