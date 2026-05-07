# Agent Workspace

先读本文件，再按任务需要进入更具体的资料。默认不要一次性加载全部内容。

## 入口原则

- 保持主入口克制，只拿完成当前任务所需的最小上下文。
- 先看仓库根 `AGENTS.md` 获取项目事实、构建命令、总规则。
- 若任务已明确，直接跳到对应 `skill`；若任务不明确，先看 `soul`。
- `memory` 只在遇到历史坑、回归问题、相似故障时再读。
- `agents` 是特定角色说明；只有确实需要该工作模式时才打开。
- `prompts` 是模板资产，不属于默认上下文。

## 目录

- `./skills/AGENTS.md`：技能索引与选择顺序
- `./memories/AGENTS.md`：历史经验索引
- `./agents/AGENTS.md`：可选代理角色索引
- `./prompts/AGENTS.md`：提示模板索引
- `./plan/`：长任务计划工件
- `./misc/`：临时分析资料，不视为规范来源

## 选择顺序

1. 判断任务是否清晰；不清晰先读 `./skills/soul/SKILL.md`。
2. 若是构建、运行、编译、target、shader 生成问题，读 `./skills/ya-build/SKILL.md`。
3. 若是 VS Code / clangd / launch / tasks，读 `./skills/vscode/SKILL.md`。
4. 若是资源、材质、渲染、代码组织等专项问题，只进入对应单个 skill。
5. 只有当当前问题明显和过去故障相似时，才补充读取 `./memories/*.md`。

## 约束

- 不把 `misc/` 内容当成正式规范。
- 不因为“可能有用”就预读全部 skills 或 memories。
- 修改架构或长期约定后，优先更新 skill；一次性故障结论优先更新 memory。
