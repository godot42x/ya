# GameEditor/UI 关切分组（G1–G3）

> 建立：2026-09-18
> 关联：`./.agent/skills/code-reorganize/SKILL.md`、`./.agent/plan/source-layout-subtraction/plan.md`、
> `./.agent/skills/gui-framework/SKILL.md`
> 状态：G1 已落地

## 0. 问题

S1 之后 `include/` 影子层消失，主流程仍然读不出来：
`Engine/Source/Applications/GameEditor/UI/` 是一个 89 文件的平铺目录，
一个 `ls` 里同时出现 dock 树、tab 实现、viewport compositor、设置对话框、
属性行构造、undo 操作。

读者要回答"我该改哪个文件"时，只能靠文件名里的 `*Tab` / `*Section` /
`*Dock*` 猜。这是**关切**没有被目录表达出来，不是缺抽象。

平铺还有一个看不见的副作用：`add_files("**.cpp")` 走 unity build，
文件顺序决定 unity 批次。平铺时两个各自私有定义 `FEmptyGuiDelegate` /
`dockHasPanels` 的 .cpp 恰好落在不同批次，符号冲突被掩盖；一旦重排批次
就会变成重复定义（本轮真实发生，见 `progress.md`）。

## 1. 边界（不做的事）

- 不新增抽象：不造 `EditorPanel`、不造 workspace/session 新类型、不造中心 bus。
- 不换 target：分组只在 `ya-game-editor` 内部，`xmake.lua` 不因分组新增 include 根。
- 不按行数机械拆文件：本轮只回答"这个文件属于哪个关切"，回答不了就不搬。
- 不动行为、不动接口语义、不动 dock layout 契约。
- 不给 plan 目录陪跑：`G2/G3` 只有在提出具体候选后才动手。

## 2. 分组（现状即契约）

`UI/<Group>/`（私有实现）与 `include/GameEditor/UI/<Group>/`（公开头）
**共用同一组组名**，公开路径 = `GameEditor/UI/<Group>/<Name>.h`。

| 组 | 判据 | 文件数（头/源） |
| --- | --- | --- |
| `Shell/` | editor 壳与会话：Surface、SurfaceContext、Root/Window/Document session、ActionCatalog、TabSpawnerRegistry、WindowRegistry、Theme、ListRows | 10 / 6 |
| `Dock/` | dock 投影与布局：DockWorkspace、NestedDockHost、NativeTearOff、WindowLayout、默认 layout JSON、私有 `EditorDockSupport.h` | 4 / 4 (+2 json) |
| `Tabs/` | 可停靠 tab 的实现：`*Tab` + DebugCatalogView + UIDesignerTools | 16 / 15 |
| `Sections/` | 被 tab 复用的纵向区段：AutoProperty + Runtime\*Section | 7 / 7 |
| `Viewport/` | viewport 呈现链：Compositor、GizmoController/Overlay、Host、OverlayRecord | 5 / 5 |
| `Dialogs/` | 模态/浮层交互：AssetPicker、FilePicker、FilePickerDialog、SettingsDialog | 4 / 2 |
| `Ops/` | 一次编辑动作：HierarchyOps、TransformUndo | 2 / 2 |

合计 48 头 + 41 源 + 2 json 移动，1 头新建。

命名判据（写进 skill，供后续新增文件时沿用）：

1. 一个文件只属于一个组；归属看它**服务哪个关切**，不看它被谁 include。
2. `Tabs/` 只放 `IEditorTab` 实现；被多个 tab 复用的区段进 `Sections/`。
3. `Shell/` 放"谁在驱动这一帧"，`Dock/` 放"面板停在哪"，两者不互相搬。
4. 组内才允许私有头；私有头不入 `include/`，同目录裸文件名 include。
5. 组名一旦出现就成契约；重命名组 = 改公开路径，属于破坏性改动。

## 3. 阶段

| 阶段 | 目标 | 状态 |
| --- | --- | --- |
| G1 | `UI/` 按关切分组，公开路径改为 `GameEditor/UI/<Group>/...` | 已落地 |
| G2 | 分组契约写进 skill，并把 `include/` 路径规则与 `UI/<Group>` 对齐到文档 | 待做 |
| G3 | 同标准扫其余平铺目录（候选见下），一条一条立项 | 待做 |

### G1 验收（已满足）

1. `UI/` 与 `include/GameEditor/UI/` 下**无平铺残留**：只有组目录。
2. `GameEditor/*` 全部 include 字符串解析成功（291 处），0 个旧路径残留。
3. 搬迁保留 blame：`git diff -M --summary` 报 91 rename + 1 create，
   无 `delete`+`add` 对。
4. 私有头 `EditorDockSupport.h` 落地在同目录，`add_headerfiles("**.h")` 补上
   （该 target 此前没有私有头，缺了就静默不进 IDE / 不进安装清单）。
5. 构建：`ya-game-editor`、`ya-testing`、`ya-game-runtime`、`ya-engine`。
6. 测试集合对比 HEAD 基线：**0 新增回归**，另修好 4 条此前被 S1 打断的
   源码守卫测试。

### G3 候选（未立项）

同一把尺子量出来的其余平铺目录：

- `Engine/Test/Source`（114 文件）——按被测模块分组，收益最大。
- `Engine/Source/Framework/Render/Render3D/include/Render3D/Common`（33）
- `Engine/Source/Framework/GUI/Runtime/Widgets/{Controls,include/.../Controls}`（32/31）
  ——已在 `Controls/` 下，可再按 `Panel/`、`Input/`、`Layout/` 细分。
- `Engine/Source/Framework/RHI/include/RHI/Core`（26）

每条候选在动手前必须先说明"哪个读者问题被解决"，否则不搬。

## 4. 退出条件

- 新增一个 editor UI 文件时，不需要问"放哪"：组名给出答案。
- 读 `Shell/` 能看到驱动链，读 `Tabs/` 能看到面板清单，互不夹杂。
- 同目录内不存在两个私有定义同名符号的 unity 隐患（要么合到私有头，要么改名）。
