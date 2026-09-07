# GUI Framework 架构加固进度

> 建立日期：2026-09-07  
> 作用：只记录已发生事实、验证结果、偏离项和下一接力点；计划中的未来设计不在这里冒充完成。

## 2026-09-07 — 架构 review 与计划建立

### 已确认事实

- 主链已是 `WidgetTree -> layout/tick -> UIFrameSnapshot -> Compose -> Render2D`。
- attach/detach、opt-in tick、parent-owned slot、incremental paint 和 immutable snapshot 已落地。
- ColorEdit 的 SV 方使用两层一维顶点色 quad；不是需要重做的架构缺口。
- snapshot 将 inherited clip 展平进每个 draw item；compose 当前逐 item push/pop clip。
- `Render2D::popClipRect()` 会 flush pending batch，因此存在按 clipped item 放大 draw call 的明确代码路径；尚未记录运行时数字。
- `UIElement::layoutAssigned()` 有 `tryReuseAssignedLayout()`，多个 specialized layout host override 直接 arrange；尚未按 host 记录实际重复 arrange 数。
- GameRuntime AssetManager 的 texture completion 当前 dispatch 到 game thread；standalone host source 当前同步加载。`IGuiTextureSource` 接口本身尚未冻结 completion thread。
- `UIFrameSnapshot` 持有强 `Texture` 引用，当前同时承担录制/submit 前资源保活；不能只为依赖图好看直接替换。
- `Render2D` 仍使用静态 session，但当前没有确认的并行 recording 消费者。

### 本轮完成

- 新建 `plan.md`，把执行主线收敛为 clip batching、layout host skip、texture completion thread 三项。
- 补齐 `.agent/plan/AGENTS.md` 要求的长期计划工件：
  - `todo.md`；
  - `progress.md`；
  - `feature_matrix.json`；
  - `session_checklist.md`。
- 为 Widgets/Texture 边界、snapshot payload、Render2D session 设置 profile/consumer 驱动的条件决策门。

### 验证

- 文档结构和当前代码只读审计。
- `git diff --check` 待本轮收尾执行。
- 本轮未修改 `Engine/Source`，未运行构建或运行时测试。

### 工作区边界

建立计划时 GUI、Editor、GameRuntime、Workbench、测试和既有计划中存在大量未提交改动。它们不是本计划创建动作的一部分，后续 checkpoint 必须逐文件确认归属，不得批量吸收。

### 下一接力点

1. 检查共享工作区是否允许建立独立基线。
2. 领取 `GAH-001`，先固定同 clip item 的 flush 放大测试。
3. 不在基线任务中实现 C1。

## 2026-09-07 — GAH-001 Clip run flush 基线

### 目标与边界

- 单一目标：用 CPU-only compose 观测证明当前 flush 随 clipped item 数增长。
- 非目标：不实现 clip-run 合批，不改 `replaySnapshotItems` 状态机。
- 工作区：`Render2DComposePass.cpp` 已有 ColorEdit/opaque-sample 等无关未提交改动，本 checkpoint 不吸收；观测 API 放在新文件。

### 本轮完成

- 新增 `measureUIFrameComposeReplay()`：按当前 per-item `pushClip/emit/popClip` 协议统计 flush / scissor transition / painter order。
- clip 比较走 `bClipped` + `pos` + `extent`，不比较对象表示。
- `ComposeClipReplayTest` 覆盖：同 clip N sprite、unclipped、A→B→unclipped、sprite/text/line、空 clip、nested builder 展平、digest 不变。

### 验证

```text
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='ComposeClipReplayTest.*'
# 7 tests, PASSED
```

基线数字（当前 per-item push/pop 协议）：

| 场景 | items | screenFlushCount | scissorTransitionCount |
|---|---|---|---|
| 同 clip sprite ×4 | 4 | 4 | 8 |
| 同 clip sprite ×8 | 8 | 8 | 16 |
| unclipped sprite ×8 | 8 | 1 | 0 |
| clip A → clip B → unclipped | 3 | 3 | 4（A, none, B, none） |
| nested builder 展平后同 clip ×4 | 4 | 4 | 8 |
| 空 extent clip ×2 | 2 | 2 | 4 |

结论：相邻相同 flattened clip 不能合批；flush 随 clipped item 线性放大。unclipped run 已经是 1 个 screen batch。GAH-101 的目标是把「同 clip ×N」从 N flush 收到 1 flush（overflow 除外），并把 A→B→none 的 scissor 序列收到三次状态切换而不是四次往返。

### 保留 / 未完成

- `measureUIFrameComposeReplay` 目前镜像 `replaySnapshotItems` 的 clip 协议，尚未共用同一 walker。原因是 compose 实现文件已有无关脏改动。GAH-101 改合批时必须把 replay 与 measure 收成同一状态机，否则基线会和 GPU 路径脱节。
- 未跑 GPU/offscreen parity（C0 不要求；GAH-102 才跑）。
- GAH-002 / GAH-003 / GAH-004 未开始。

### 偏离项

无。没有实现 active-clip 状态机，没有改 snapshot schema。

### 下一接力点

领取 `GAH-002`：layout host assigned-rect skip 与 clean sibling arrange 基线。

## 2026-09-08 — GAH-002 Layout host skip 基线

### 目标与边界

- 单一目标：证明 specialized layout host 绕过 `tryReuseAssignedLayout()`，并锁定 clean sibling 的现状 arrange 数。
- 非目标：不统一 skip 入口，不引入 measure cache。
- 工作区：`UIElement.cpp` / `WidgetLayoutTest.cpp` 已有无关脏改动；观测加在干净的 `UILayout` 上，测试放新文件。

### 本轮完成

- `UILayout::arrange()` 改为计数入口，真实算法在 `onArrange()`。`getArrangeCount()` / `resetArrangeCount()` 是 CPU-only 观测。
- `LayoutHostSkipBaselineTest` 覆盖 Panel skip 对照，以及 Container/Button/CheckBox/Overlay/Scroll/Split/SizeBox/Popup/Dock 的 bypass。
- 局部分支 dirty：Fill 的 clean sibling Container 仍 arrange=1；其 Panel 子节点仍 skip。

### 验证

```text
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='LayoutHostSkipBaselineTest.*:WidgetLayoutTest.*'
# 86 tests, PASSED
```

基线数字（parent 以相同 assigned rect 再 assign 一次）：

| host | arrangeCount |
|---|---|
| UIPanel（基类 skip） | 0 |
| Container / Button / CheckBox / Overlay / Scroll / Split / SizeBox / Popup / Dock | 1 |

局部分支 dirty（row 内两个 Fill Container）：

| 节点 | arrangeCount |
|---|---|
| dirty 侧 Container | 1 |
| clean 侧 Container | 1（bypass；GAH-201 应收成 0） |
| dirty 侧 Panel child | 1 |
| clean 侧 Panel child | 0 |

结论：基类 assigned-layout skip 对 Panel/canvas 仍然成立；specialized host override 直接 `setLayoutRect + arrange`，clean sibling host 会随另一分支的局部 dirty 一起重排。

### 保留 / 未完成

- SelectableRow / CompoundWidget / TableGrid 同样 bypass，本任务未列入验收名单，GAH-201 统一入口时应一并覆盖。
- GAH-003 / GAH-004 当时未开始。

### 偏离项

无。没有实现 skip 统一，没有改 dirty taxonomy。

### 下一接力点

领取 `GAH-003`：texture completion 线程事实与 foreign-thread seam。

## 2026-09-08 — GAH-003 / GAH-004 Texture thread 与场景成本基线

### 目标与边界

- GAH-003：锁定两个真实 adapter 的 completion 线程事实，并留下 foreign-thread 红灯用例。
- GAH-004：记录 clipped list / Inspector / Dock / ColorEdit 的 draw item、flush、layout/paint/arrange。
- 非目标：不改 AssetManager 调度，不引入 GUI task queue，不在 catalog/tree 上加 owner-thread fail-loudly（GAH-301），不实现 clip-run 合批。
- 工作区：`WidgetTree` / `GameUIHost` / `UIFrameSnapshot` / ColorEdit / Dock 仍有无关脏改动，本 checkpoint 不吸收。`FGuiTextureCatalog` 源文件此前未被 git 跟踪，但 HEAD `GUIAppHost` 已依赖该头；本任务补进仓库作为测试与 host include 的类型源。

### 本轮完成

- 提交 `GuiTextureCatalog`（`IGuiTextureSource` + `FGuiTextureCatalog`）。接口只有 `lookup` / `requestLoad` / `epoch`，没有 completion-thread 字段。
- `TextureCompletionThreadBaselineTest`：inline source 在 `requestLoad` 内完成；deferred source 把 callback 存起来稍后同线程完成；foreign-thread `notify` 先写 `cached` 再 `revision.set()`。
- `SceneCostBaselineTest`：四类 CPU 场景走 `GuiPerfStats` + `measureUIFrameComposeReplay` + host `getArrangeCount()`。

### 验证

```text
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='TextureCompletionThreadBaselineTest.*:SceneCostBaselineTest.*'
# 7 tests, PASSED
xmake b ya-gui-widgets-test
xmake run ya-gui-widgets-test -- --gtest_filter='TextureCompletionThreadBaselineTest.*'
# 3 tests, PASSED
```

真实 adapter 代码证据（closure-test 不链接 GameRuntime / Host）：

| adapter | 完成线程 |
|---|---|
| standalone `HostGuiTextureSource`（`GUIAppHost.cpp` `requestLoad`） | 调用线程同步：`FGuiTextureReady` 在 `requestLoad` 返回前执行，含 `stbi_load` + `Texture::fromData` |
| `AssetTextureManager` `onReady` | 始终 `AssetManager::dispatchToGameThread`（cache hit / fail / load complete）。无 `g_frameTaskSink` 时 inline 跑 |
| HEAD `GameUIHost` | 仍是 `getTextureByPath` 同步 resolver，没有 catalog adapter |
| 工作区未提交的 `AssetGuiTextureSource` | `loadTexture` + 上述 game-thread `onReady`；不在本提交 |

Foreign-thread 缺口（GAH-301 红灯）：

1. `Reactive::set` 已拒绝非 UI 线程（`wrongThreadMutations + 1`，本测试会打 `YA_CORE_ERROR`）。
2. `FGuiTextureCatalog::notify` 仍先写 `entry.cached` / `bKicked`，join 后 `bind()` 读到 Ready。
3. GAH-301 必须在改 `cached` 或 dependent 之前 fail loudly。

GAH-004 CPU 数字（`SceneCostBaselineTest` stdout，debug macOS）：

| 场景 | drawItems | clipped | flush | scissor | painted | rebuilt | arrange | layoutMS | paintMS |
|---|---|---|---|---|---|---|---|---|---|
| clipped list ×80 rows | 82 | 82 | 82 | 164 | 87 | 87 | 1 | 0.557 | 0.317 |
| Inspector column ×250 | 250 | 250 | 250 | 500 | 256 | 256 | 1 | 3.245 | 0.977 |
| Dock viewport/hierarchy/inspector | 33 | 33 | 33 | 66 | 32 | 32 | 1 | 0.176 | 0.066 |
| ColorEdit picker open | 49 | 49 | 49 | 98 | 8 | 8 | 0 | 0 | 0.065 |

ColorEdit `arrange=0` / `layoutMS=0`：控件本身没有 host `UILayout`；打开 picker 后 `buildSnapshot` 时 layout 已干净，只计量 paint。当前协议下 clipped item 数 = screen flush 数。

Workbench dump 结构对照（`Example/GUIWorkbench/Baselines/`，全部 clipped）：

| dump | items | clipped |
|---|---|---|
| editor.json | 91 | 91 |
| dock.json | 102 | 102 |

### 保留 / 未完成

- `WidgetTree` catalog 挂载（`setTextureSource`）与 GameRuntime `AssetGuiTextureSource` 仍在未提交工作区，不是本 checkpoint。
- 未改 `IGuiTextureSource` 线程契约（GAH-301）。
- 未跑 GPU/offscreen parity（GAH-102）。
- layoutMS/paintMS 是单次 debug CPU 读数，C1/C2 对比以 item/flush/arrange 计数为主。

### 偏离项

无。没有实现 clip-run 合批、layout skip 统一或 owner-thread 诊断。

### 下一接力点

领取 `GAH-101`：相邻相同 flattened clip 共享 scissor run；`measureUIFrameComposeReplay` 与 `replaySnapshotItems` 必须收成同一 walker。

## 2026-09-08 — GAH-101 / GAH-102 Clip-run 合批

### 目标与边界

- 单一目标：相邻相同 flattened clip 共用一次 scissor run；measure 与 GPU replay 走同一 walker。
- 非目标：不改 snapshot schema，不重排 painter order，不关 G1 `_bSelfClip`，不吸收 Compose/ColorEdit 其它脏改动。

### 本轮完成

- `walkComposeClipRuns`：只在 clip 状态变化时 push/pop；A→B 不经过中间 unclipped emit。
- `measureUIFrameComposeReplay` 与 `replaySnapshotItems` 共用该 walker。
- `ComposeClipReplayTest` 改为合批后数字；`SceneCostBaselineTest` 增加 self-clip off 对照。

### 验证

```text
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='ComposeClipReplayTest.*:SceneCostBaselineTest.*'
# 13 tests, PASSED

xmake b GUIWorkbench
python3 Script/automation/gui/run_workbench_gpu_parity.py --skip-build
# GUIAppHost offscreen parity diff: pass=true differing=0 ratio=0.0000
```

CPU 合批对照（GAH-001 → GAH-101，overflow 除外）：

| 场景 | flush 前 | flush 后 | scissor 前 | scissor 后 |
|---|---|---|---|---|
| 同 clip sprite ×N | N | 1 | 2N | 2 |
| unclipped ×8 | 1 | 1 | 0 | 0 |
| clip A → B → unclipped | 3 | 3 | 4 | 3 |
| nested flatten ×4 | 4 | 1 | 8 | 2 |
| sprite/text/line 同 clip | 3 | 1 | — | 2 |

代表性场景（`SceneCostBaselineTest`）：

| 场景 | items | flush GAH-004 | flush GAH-101 |
|---|---|---|---|
| clipped list（默认 self-clip） | 82 | 82 | 81 |
| clipped list（`_bSelfClip=false`） | 82 | — | 1 |
| Inspector ×250 | 250 | 250 | 250 |
| Dock 三栏 | 33 | 33 | 14 |
| ColorEdit picker | 49 | 49 | 2 |

G1 `_bSelfClip` 把每个 widget 的 flattened clip 收成自身 layout rect，所以默认 Inspector/长列表几乎仍是一 item 一 clip。合批只在相邻 item 的 flattened clip 真相等时生效；不在本任务关掉 self-clip。

### 保留 / 未完成

- Render2D texture/capacity overflow 仍会额外 flush；measure 不建模 overflow。
- C2 layout skip（GAH-201）未开始。

### 偏离项

无。没有改 snapshot schema，没有按材质重排 item。

### 下一接力点

领取 `GAH-201`：统一 assigned-layout skip 入口。
