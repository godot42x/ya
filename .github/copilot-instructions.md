# GitHub Copilot 项目指南

主入口请先读仓库根 `./AGENTS.md`，再按需进入 `./.agent/AGENTS.md`。

## 读取原则

- 不要一次性加载全部 skills / memories。
- 先用 `AGENTS.md` 获取项目事实、构建命令和总规则。
- 任务不明确时，先读 `./.agent/skills/soul/SKILL.md`。
- 任务明确后，只进入一个主 skill；必要时再切下一个。
- 只有遇到历史坑、相似回归时，才读 `./.agent/memories/*.md`。

## 快速路由

- 构建、运行、target、编译、shader：`./.agent/skills/ya-build/SKILL.md`
- VS Code、clangd、launch、tasks：`./.agent/skills/vscode/SKILL.md`
- 资源加载与 resolve：`./.agent/skills/resource-system/SKILL.md`
- 材质数据流：`./.agent/skills/material-flow/SKILL.md`
- 渲染架构与后端边界：`./.agent/skills/render-arch/SKILL.md`
- C++ 风格与生命周期：`./.agent/skills/cpp-style/SKILL.md`
- 代码拆分与目录重组：`./.agent/skills/code-reorganize/SKILL.md`
- 崩溃排查与 review：`./.agent/skills/debug-review/SKILL.md`

完整索引：`./.agent/AGENTS.md`
