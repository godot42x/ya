# Skills Index

**本文件是路由的唯一真源。** 根 `AGENTS.md` 与 `.agent/AGENTS.md` 只指向这里，不重复列表。

按需读取单个 skill，不要全量加载。若问题描述不清晰，先读 `soul` 澄清，再切主 skill。

## 优先级

任务跨多个 skill 时，按此顺序取第一个命中的：

`ya-build > gui-framework > cross-platform > profiling > vscode > resource-system > material-flow > scene-object-boundary > render-arch > cpp-style > code-reorganize > debug-review`

`soul` 是前置澄清，不参与优先级比较。

## 索引

### 构建 / 环境

- `ya-build`：XMake 构建、目标、shader 生成、测试
- `vscode`：VS Code 任务、调试、clangd、`compile_commands`
- `cross-platform`：Windows/MSVC 与 macOS/Clang 跨平台编译规则、DLL 导出、平台差异
- `profiling`：profile 模式、automation trace、低噪音性能冒烟
- `speedscope-analysis`：speedscope CPU trace 抽样转文本、热点定位

### GUI

- `gui-framework`：WidgetTree / 控件 / 布局契约 / Render2D pass slot / GUI host 诊断与 teardown
- `gui-gallery-design`：GUIWorkbench / feature gallery 的信息架构与视觉层级
- `font-rendering`：FontManager / Atlas / Bitmap+SDF / flavor split / CJK fallback

### 渲染 / 资源 / 场景

- `render-arch`：RenderRuntime、后端边界、render pipeline、shader 生成链
- `resource-system`：AssetManager、resolve、dirty queue、environment lighting、GPU 资源生命周期与保活
- `material-flow`：ECS 到 runtime material 到 render consumer 的数据流
- `scene-object-boundary`：生成物边界（gizmo / 图标 / 子 mesh）、派生视觉的序列化与可编辑边界
- `reflection`：TypeOps / facet / MetaValue、type_index 不落盘、组件 JSON

### 玩法 / 内容

- `2d-gameplay`：俯视 2D 玩法约定（遮挡 z、tilemap 通行 / entityAt、脚本互调）
- `image-gen`：为游戏生成 2D 资源（角色 / 道具 / 图标 / tile），本地 ComfyUI 优先

### 工程方法

- `cpp-style`：命名、所有权、类布局、热路径风格
- `code-reorganize`：文件拆分、目录重组、include 修复
- `debug-review`：崩溃排查、diff 自检、review 风险
- `subagent-delegation`：派发 subagent（新开 / 续接 / 分叉、模型选择、交接 prompt、并行文件归属、断线恢复、结果核对）
- `soul`：需求不清晰、方向未收敛时先做澄清

## 分层约定

渐进式披露分四层，读的时候只进到够用的一层：

| 层 | 位置 | 放什么 |
| --- | --- | --- |
| L1 入口 | 根 `AGENTS.md` | 只做路由，不放规则细节 |
| L2 索引 | 本文件、`../memories/AGENTS.md` | 路由与索引，唯一真源 |
| L3 契约 | `<skill>/SKILL.md` | 稳定规则、责任边界、intent |
| L4 细节 | `<skill>/references/*.md` | 展开的实现细节、分场景补充 |

跨文件的 `references/*.md` 只在 `SKILL.md` 明确指向时才读。

## 维护规则

- 一个任务默认只进入一个主 skill。
- 每个 `SKILL.md` 与 `references/*.md` 各不超过 300 行 / 12 KB；超了下沉到 L4。
- 只写符号名与路径，**不写行号**（行号最快腐）。
- 相对链接必须能从当前文件所在目录解析。
- 新增 skill 必须同时登记到本文件；本文件与磁盘集合由
  `python3 Script/ya_agent_docs_lint.py` 双向核对。
- 通用稳定规则继续往 skill 收敛，不要回流到顶层 `AGENTS.md`。
- 已不再是默认路径的内容，不要继续堆在 skill 开头。
