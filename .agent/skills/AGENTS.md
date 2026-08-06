# Skills Index

按需读取单个 skill，不要全量加载。

## 推荐顺序

`ya-build` > `cross-platform` > `profiling` > `vscode` > `resource-system` > `material-flow` > `render-arch` > `cpp-style` > `code-reorganize` > `debug-review`

若问题描述不清晰，先读 `soul`，澄清后再切到主 skill。

## 索引

- `soul`：需求不清晰、方向未收敛时先做澄清
- `ya-build`：XMake 构建、目标、shader 生成、测试
- `cross-platform`：Windows/MSVC 与 macOS/Clang 跨平台编译规则、DLL 导出、平台差异
- `profiling`：profile 模式、automation trace、低噪音性能冒烟
- `speedscope-analysis`：speedscope trace 抽样转文本、热点定位、交给 AI 分析
- `vscode`：VS Code 任务、调试、clangd、compile_commands
- `resource-system`：AssetManager、resolve、dirty queue、environment lighting
- `material-flow`：ECS 到 runtime material 到 render consumer 的数据流
- `render-arch`：RenderRuntime、后端边界、render pipeline、shader 生成链
- `cpp-style`：命名、所有权、类布局、热路径风格
- `code-reorganize`：文件拆分、目录重组、include 修复
- `debug-review`：崩溃排查、diff 自检、review 风险

## 维护规则

- 一个任务默认只进入一个主 skill。
- 通用稳定规则继续往 skill 收敛，不要回流到顶层 `AGENTS.md`。
- 已不再是默认路径的内容，不要继续堆在 skill 开头。
