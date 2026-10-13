# 进度

## 2026-10-13 — A3 + A3.6 落地（拆分完成，lint 全绿）

### 做了什么

三个超预算 skill 全部按 H2 标题**机械抽取**拆分（不重打一个字，因此不会静默改写），
每个 `SKILL.md` 留作入口 + 一张 `references/` 索引表（写明"何时读哪份"）。

| 对象 | 之前 | 之后 | references |
| --- | --- | --- | --- |
| `gui-framework` | 861 行 / 98.5 KB | **158 行** / 9.2 KB | 7 份 |
| `ya-build` | 366 行 | **107 行** | 3 份 |
| `render-arch` | 310 行 | **65 行** | 3 份 |

**作用域越界已修正**：原 `gui-framework` 的 `## GameEditor chrome`（166 行 / 16 KB，最大单节）
与 `## 编辑器内嵌（已废弃）` 属 `Engine/Source/Applications/` 产品内容，迁出为
新 skill `.agent/skills/game-editor/SKILL.md`（193 行），并在 frontmatter 明确写它是
Application 层、framework 能力见 `../gui-framework/SKILL.md`。

`gui-framework/references/` 分册：widget-tree / layout-and-controls / animation-and-dsl /
style-theme / dock / host-and-render / interaction-contract。
`SIZE_EXEMPT` 表已清空为空 dict——债还清了，表就不能留行。

### 验证

- **内容零丢失**：对三个对象逐行比对 HEAD 版本（≥40 字符的实质行），
  唯一差异是刻意修正的链接层深（`../../` → `../../../`）；gui-framework 与
  game-editor 合并后 677 行实质内容 0 丢失。
- 全部 21 个 skill 的 `SKILL.md` 均 ≤300 行，最大 243 行。
- `python3 Script/ya_agent_docs_lint.py` → **0 violation / 0 debt**（首次全绿）。
- lint 在两次拆分中各抓到一处"移动后引用深度少一层"（`references/` 比原目录深一级），
  说明该检查确实在拦真问题，不是摆设。

### 自我修正记录

`render-arch` 首次拆分报 `UNMAPPED: '目录锚点'`——我的分节计划漏了 6 个 `##` 节
（目录锚点 / 相关 skills / Windows DLL Boundary / 决策原则 / Dynamic Rendering 约束 / 退出条件）。
脚本对未映射节直接 `SystemExit` 而不是静默丢，这正是要的行为；补齐映射后重跑。

---

## 2026-10-13 — A1 + A2 + A4 落地

### 做了什么

**A1 结构一致性**

- 三张路由表合一：`.agent/skills/AGENTS.md` 成为唯一真源（含优先级与完整
  索引，按「构建/环境、GUI、渲染/资源/场景、玩法/内容、工程方法」分组）；
  根 `AGENTS.md` 与 `.agent/AGENTS.md` 改为指针 + 少数高频入口。
- 索引补全：skills 20/20、memories 27/27（补 3 条漏登，描述按各 memory 正文重写）。
- 相对链接层深修复：计划里只列了 4 处，lint 建好后实际查出并修掉 **10 处**
  （`debug-review` ×2、`ya-build`、`render-arch/windows-dll-boundary`、
  `font-rendering`、`gui-framework`、`windows_msvc_compile_portability` ×2、
  `agents/copilot`、`agents/dsh` ×3）。
- 消除 `windows-dll-boundary` 重复：该主题原本散在 **三处**（`cross-platform` §2
  导出规则、`render-arch/windows-dll-boundary.md` 21 行规则、memory 225 行历史）。
  规则集归并进 `cross-platform` 新增的规则 6（"有全局状态的 runtime 库不能被多个
  DLL 各自静态持有"），删掉 render-arch 子文件，memory 保持为历史与排查单。
- 删除死重：`.agent/misc/`（11 文件，最后修改 2026-04-16，内含已腐的代码索引）与
  `.agent/prompts/`（9 个 speckit 模板，`specs/` 最后提交 2026-04-05，无引用链）。
  两者都完整存在于 git 历史，恢复锚点：`git show 1685766c4:.agent/misc/<file>`、
  `git show b665317a4:.agent/prompts/<file>`。

**A2 立规矩**

- 新增 `Script/ya_agent_docs_lint.py`，五项检查：索引双向覆盖、链接可解析、
  体量预算、行号引用、路由唯一真源。已接入 `Script/ci.sh`（放在最前，无需构建）。
- 规则成文进根 `AGENTS.md` 的 Documentation Policy（L1–L4 分层表 + 四条硬纪律）
  与 `.agent/AGENTS.md` 维护规则。

**A4 定点核对**

- 行号引用：全 skills 只有 `speedscope-analysis` 一处真命中，且是**粘贴的工具输出
  示例**而非引用；据此给 lint 加了代码块豁免（示例输出不会像引用那样腐）。
- 改名符号：`RenderImage` 已被 `ImageResource` / `RenderTexture` 取代，修了 3 处
  活引用（`cpp-style` ×2、`gui-framework` ×1）。`legacy_test_target_break_after_module_move.md`
  里的 `RenderImage` 是**记录这次改名本身**的，保留。

### 验证

- 负向用例：注入 4 类缺陷（不存在的 skill 名、坏相对链接、坏 memory 链接、
  源码行号引用），lint 全部报出，退出码 1。
- 正向用例：恢复后 lint 退出码 0。
- 结构核对：磁盘 ↔ 索引集合完全相等（skills 20/20、memories 27/27）。
- 路由核对：优先级串只在 `skills/AGENTS.md` 出现一次（另两处 0 次）。

### 自我修正记录

负向用例的清理用了 `git checkout -- .agent/memories/AGENTS.md`，把当时尚未提交的
3 条索引新增一并回滚了——lint 立刻报出 3 条 "memory on disk but not in index"，
等于被自己的守卫抓到。已重新应用。教训：清理探针要用逐文件副本恢复，不要用
`git checkout` 回滚还带着未提交改动的文件。

### 未完成 / 遗留

**本批次当时的状态（A3 落地前）**，保留作为记录：A3 未开工、`SIZE_EXEMPT` 还有 3 条债。
这两项已在同日提交 `f2684e06` 结清，见上一节。

**仍然成立的遗留（明确不在本计划范围）：**

- 未测 skill 内容与当前代码的**语义**一致性（本轮只测路径、符号存在性与结构）。
  概念层准确性留给未来实际改到某个 skill 时顺带做。
- `plan/archive/` 的 ~21% 陈旧路径引用**不治理**：`plan/AGENTS.md` 声明它是历史
  记录，失效率是设计如此。

### 一处事后更正

A1 提交信息里写的"链接层深修了 10 处"**不准确**。事后用独立解析器在基线
`5187d22c^` 的完整树上逐条核验（解包 commit、对每条 backticked/markdown 的
`.md` 引用同时按 doc-relative 与 repo-root 两个基准解析）：**基线是 13 条坏引用，
修完是 0 条**。审计阶段预测的"4 处"和提交信息里的"10 处"都不对——前者只统计了
`../memories/` 这一种形态，后者是修复过程中的中间计数。`feature_matrix.json`
已按 13 → 0 更正。

教训：验收数字要**在基线上用同一套判据量一遍**再写进提交信息，不要用修复过程中
的观察值代替。

---

## 2026-10-13 — 立项 + 审计基线

审计在一次只读摸底中完成，未修改任何文件。以下是建立后续批次所依据的**实测数字**。

### 体量

| 对象 | 实测 |
| --- | --- |
| `.agent/skills/gui-framework/SKILL.md` | 861 行 / 98.5 KB / 约 2.8 万 token |
| `.agent/skills/` 全量 | 20 skill / 286 KB（全载约 8 万 token） |
| `.agent/memories/` 全量 | 28 文件 / 96 KB（约 2.4 万 token） |
| `.agent/plan/` | 249 文件 / 4 MB（archive 29 目录） |
| `.agent/misc/` | 10 文件 / 55 KB，最后修改 2026-04-16 |
| 入口 `AGENTS.md` + `.agent/AGENTS.md` | 9.9 KB / 约 2500 token（每任务都付） |

决策级事实：**单个 `gui-framework/SKILL.md` ≈ 全部 28 个 memory 之和**。

### 索引覆盖（目录 ↔ 索引）

- `skills/AGENTS.md`：覆盖 15/20，漏 `2d-gameplay`、`font-rendering`、
  `reflection`、`gui-gallery-design`、`image-gen`。
- 根 `AGENTS.md`：漏 `gui-gallery-design`、`image-gen`、`speedscope-analysis`。
- `memories/AGENTS.md`：漏 `font_cjk_fallback_brightness_regression.md`、
  `scenemanager_on_scene_destroy_alias_bug.md`、
  `terrain_processor_active_pump_regression.md`。
- `.agent/AGENTS.md` 只列 6 项是**设计如此**（自述二级路由），不算缺陷。

### 三张路由表

| 位置 | 内容 |
| --- | --- |
| `AGENTS.md:45` | 12 项优先级串（含 gui-framework / scene-object-boundary） |
| `.agent/skills/AGENTS.md:7` | 10 项（**缺** gui-framework / scene-object-boundary），顺序不同 |
| `.agent/AGENTS.md:25-33` | 第三个 6 项列表 |

### 路径引用腐坏（3634 条）

- 可精确解析 955 条（26%）
- 只能靠 basename 反查 2062 条（57%）
- 全库找不到 617 条（17%）

按文档类别：

| class | refs | stale | stale% |
| --- | --- | --- | --- |
| skills | 484 | 66 | 13.6% |
| memories | 122 | 20 | 16.4% |
| plan/active | 3189 | 715 | 22.4% |
| plan/archive | 2085 | 437 | 21.0% |

`plan/` 的比例高但按其自身声明属设计如此，本轮不治理。

### 符号引用腐坏 —— 大部分是误报（重要）

逐条对源码做词边界核验后确认，所谓失效项中：

- `C7595` / `C7560` / `C3861`：MSVC 错误码，不是符号
- `AEAUApp` / `AEAVApp`：MSVC 修饰名
- `FD_SET`：POSIX 宏
- `GLM_FORCE_CTOR_INIT`：第三方宏

真实腐坏是个别改名（如 `RenderImage`）与硬编码行号。**结论：不做概念层重写。**

### 相对链接层深错误（4 处）

`../memories/` 应为 `../../memories/`，当前解析到不存在的 `.agent/skills/memories/`：

- `.agent/skills/render-arch/windows-dll-boundary.md:20`
- `.agent/skills/ya-build/SKILL.md:249`
- `.agent/skills/debug-review/SKILL.md:45`、`:46`

`.agent/skills/resource-system/SKILL.md` 用的是正确的 `../../memories/`，可作模板。

### 同题双份

- `.agent/skills/render-arch/windows-dll-boundary.md`（21 行）
- `.agent/memories/windows_dll_boundary.md`（225 行）

同名同题，内容重叠仅 2 行（即基本不重叠）。需定一个家。

### `gui-framework/SKILL.md` 章节体量

| 行数 | 字节 | 章节 |
| --- | --- | --- |
| 166 | 16062 | GameEditor chrome ← **作用域越界：Application 层产品内容** |
| 117 | 8995 | 基础控件权责 |
| 85 | 5894 | WidgetTree 模型 |
| 62 | 3363 | 动画（framework 层） |
| 56 | 2637 | 产品循环 |
| 45 | 8791 | Style / Theme |
| 45 | 2787 | Host（ya-gui-host） |
| 42 | 1972 | 控件交互契约 |
| 39 | 6593 | Dock 权责 |
| 30 | 1734 | GUI render surface |
| 27 | 1481 | 模块地图 |
| 26 | 1249 | 人肉测试前的自动化验收 |
| 23 | 1301 | Render2D pass slot |
| 21 | 810 | 构建 / 测试 |
| 15 | 919 | 责任规则 |
| 13 | 672 | UX vs closure 测试 |
| 13 | 2420 | 静态 DSL（live construct） |
| 12 | 1679 | 布局契约 |

### 渐进式披露现状

- 完整分层（SKILL + references + scripts）：仅 `image-gen`
- 有子文件：`render-arch`（1 个 21 行子文件，即上面那份重复）
- 其余 18 个 skill 均为单文件

### 未做的验证

- 未测 skill 内容与当前代码语义的一致性（只测了路径与符号存在性）。
  概念层的准确性需要在 A3 拆 `gui-framework` 时逐节确认。
