# Progress

## 2026-10-11 — 自 showcase-roadmap 迁出

- 方向审查（见 `../showcase-roadmap/progress.md` 同日条目）裁定 GA 线迁独立子计划；GA1 / GA2 / GA3 三条进度随迁。
- GA2 / GA3 第一版实现留在共享工作区未提交，按 GA2r / GA3r 返工（见 `plan.md` §4）。

## 2026-10-11 — GA1 gui profile 可配置可构建（已完成，`2ee635a1`）

- 3D 示例（HelloMaterial / GreedySnake / 2DRpgPrototype / PerfLab）只在 `ya_profile ~= gui` 时 include。GUIFrameworkSmoke 与 GUIWorkbench 两个 profile 都在。
- `Example.xmake.lua` 的示例 include 改成与当时目录一致的 `2dRpgPrototype`（另一种大小写在大小写不敏感盘上是第二个 include，`Entry.cpp` 会进链接两次）。2026-10-11 方向审查拍板规范名 `2DRpgPrototype`，统一已随 showcase W5 落地（`c377e0df`）。
- `Script/ci.sh` 的 `gui-shared` 补上 `ya-gui-headless-host-test` 和 `GUIWorkbench --smoke-actions`。
- 文档：`gui-framework` skill 去掉不存在的 `make test-gui`，改成 closure / widgets / headless-host / workspace 四条；`Test.xmake.lua` 不再把 closure test 写成 gui profile 里唯一的测试；场景数按目录写成 31。
- 验证在独立 worktree（`/tmp/ya-gui-ga1`，不改共享工作区的 engine 配置）：`ya_profile=gui` 配过；`ya-gui-minimal-host`、`GUIWorkbench`、`ya-gui-headless-host-test` 链接通过。共享工作区仍是 engine profile。

## 2026-10-11 — GA2 GUIWorkbench 一条命令门禁（实现未提交，返工中 → GA2r）

- 入口是 `python3 Script/gui_workbench_gate.py`：31 个 `Scenarios/*.jsonl` 的 headless 断言，然后 `gui_style_baseline.py` 的结构化 digest，然后 `run_workbench_gpu_parity.py` 的同帧 GPU / 离屏。二进制由 `Script/automation/gui/workbench_bin.py` 在 `build/` 下查找，不再写死 `build/macosx/arm64/debug`。
- `Script/ci.sh` 的 `gui-shared` 在 `--smoke-actions` 之后跑这条门禁（`--skip-build --skip-gpu`）。像素基准仍不是跨平台门禁。
- 七个场景对着当前控件几何重对过（方向审查判为返工点：像素坐标回放、断言放宽）：Theme 走 View → Light Theme；折行宽度用 `$gt 300` / `$lte 360`；ColorEdit 不再断言已删除的 `activeChannel`；Dialog OK、Console 合并、同叶 tab 重排与关闭、分割条拖拽都改到实测矩形上。`theme_light` 基线用同一条菜单路径，暗色底约 0.11、亮色底约 0.95，两份 semantic digest 不同。
- 验证：`workbench gate: scenarios=31 failed=0 digest=PASS gpu=PASS`。

## 2026-10-11 — GA2r 场景按控件定位（返工完成，`T3`）

- **机制**：`GuiScenarioStep` 增加控件目标字段（`widget` / `widgetPart` / `at` 归一化锚点 / `by` / `to_widget`），新增 `click` 原子步骤（move+press+release）与 `wheel` 控件形式；`mouse_move` 也接受控件目标（tooltip 悬停）。回放时由宿主注入的 resolver 从**当前控件树取布局后矩形**（`WidgetTreeDump::resolveWidgetEventTarget`，与 assert.widget 同一套按名查找：全层、paint order），`part:"handle"` 解析 `UISplitPane::getDividerRect()`、`part:"close"` 解析 `UITabButton::closeHitRect()`。**缺失 / 不可命中 / 被覆盖（`pickAt` 顶点不是目标或其后代）一律让场景失败**，不静默跳过；`GuiScenarioEventSource::bFailed` + executor 返回 false 双路径生效。headless 在 `main.cpp` 接线；windowed 在 `guiApp.init()` 后经 `getEventSource()` 拿到场景源接同一个 resolver（GUIAppHost 未动——T2 禁区）。
- **命名约定**：菜单项由 `Menu{index}` 改为 `Menu_{label}`（对齐 `MenuBar_{label}`；分隔符保留位置名）。索引名在 profile 构建下会因 Frame Inspector 项漂移，标签名稳定。split 手柄不加命名节点——分隔条是布局几何（`UISplitLayout`），已有 `getDividerRect()` 公开访问器，`part:"handle"` 是更轻的既有约定延伸。
- **迁移**：theme（`MenuBar_View` → `Menu_Light Theme`）、dock（`Tab_Console` → `Scene_Body`）、dock_tabs（`to_at:[0,0.5]` 落 Hierarchy 左缘、`part:"close"` 关 Console）、gallery_acceptance（Channel cells `ChannelA`、`Swatch`、SV 区按 picker 固定内边距的归一化锚点、shield=点 `ColorPickerOverlay` 内容区之外、`Menu_Torus`）、gallery_p1（悬停/点击全控件名）、resize_scrollsplit_interaction_stress（`part:"handle"` + `by` 增量，不再按窗口尺寸查像素表）。resize_scrollsplit / resize_scrollsplit_stress 本无指针步骤，不变。`gui_style_baseline.py` 的 theme_light 同步。
- **WrappedText 宽度（原 GA2 放宽为 (300,360] 的原因）**：`12cb87b0`（[rpg] town playthrough，2026-10-11）有意把折行文本的 desired 从「整段折行宽（=maxWrapWidth 360）」改为「最长实际行宽」（desired 与绘制一致）；页面仍是 `setMaxWrapWidth(360)`。**不是回归**——按裁决断言精确新值 `345.5832824707031`（13px 合成字体、固定 advance，确定性），不再用范围掩盖。
- **digest 基线归因（HEAD `b72e2113` 基线 vs 重生成基线）**：差异全部来自已提交代码，GA2 的输入路径切换（ThemeToggle→View 菜单）不改变终态帧：6571a09e（[gui/theme] 表面阶梯抬升 dark bg 0.075→0.110 / light 0.86→0.945、按钮/tabs/list 去描边环、Theme 页重写为 View 菜单提示 + accent/checkbox/style-key 样例）；f2f1a6fe（[gui/animation]）与 05d7c0f9（[gui/workbench] add Windows page）给 rail 各加页（每页 +3 行）。旧基线落后是因为 `gui_style_baseline.py` 当时写死 debug 路径且不在常规门禁内。基线就此接受并随本提交入库。
- **稳健性证明**：临时把 `WorkbenchShellSplit` ratio 0.23→0.30（不提交），重建后 9 个迁移场景全过；已还原并重建。旧像素场景在这种改动下会整批失效。
- **验证**：`GuiEventDriverTest` 新增 5 例（解析/非法形式拒绝/click emit/无 resolver 或 resolver 失败即失败/解析器数学与 part/覆盖/隐藏失败语义）；`ya-gui-closure-test` 651/651；`gui_workbench_gate.py` 两次全绿 `scenarios=31 failed=0 digest=PASS gpu=PASS`（其中一次完整 gate 曾报 failed=2，未留下场景名，随后两次全绿——按偶发记录，不写进门禁豁免）。
- **审查修正（review 2026-10-12）**：初版提交误把 benchmark 接线（`GuiBenchmark.h` include、`--benchmark` 参数、`guiApp.setBenchmark`）随 main.cpp 带进提交，而这些接口当时只在未提交工作区里——干净 checkout 编不过。修复：`read-tree` + `commit-tree` 重建链，T3 重写为本段所在的干净提交（main.cpp 只含场景按控件定位，benchmark 接线归 GA3r 提交），GA3r 提交保留 T2 原内容并叠加正确归属的修正；在 `/tmp` worktree 对重写后的 T3 提交做了干净 checkout 编译验证。修复途中一次 `git commit --amend` 打到了并行 GA3r worker 刚推的提交（HEAD 在操作间隙移动），reflog 定位后用 plumbing 无损重建、未动工作区——教训进 memory（amend 前必须核对 HEAD hash 仍是自己的提交）。随修正一并落地：windowed 的 `bFailed` 检查合并为一次 cast；gate 汇总打印失败场景名单（此前只报数量，failed=2 那次因此查无可查）；`Menu_{label}` 命名的已知代价记进 Menu.cpp 注释（名字跟随显示文案，本地化时查找名会跟着变，做菜单本地化时要回头处理）。

## 2026-10-12 — GA3r 游戏运行时与 GUI 共用一个基准会话

- 状态机、五个数据源（frame、cpu_stages、gpu_passes、render_counters、process_memory）、pass GPU 计时开关的保存/恢复、以及 `bench_summary.json` 的写出都在 `Core/Profiling/BenchmarkSession`。宿主只实现 `IBenchmarkHost`（present mode 标签、按像素匹配窗口、设备元数据、已完成的 pass GPU 时间、退出）。summary schema 没改。
- 配置分两层。公共字段是 `profiling::FBenchmarkCommonConfig`，游戏 `BenchmarkConfig` 和 GUI `FGuiBenchmarkConfig` 都内嵌这一份，只再加各自的扩展字段。游戏扩展是 scene、cameraPath、fixedDeltaSeconds、uncapFrameRate、cpuTrace。GUI 扩展只有 page，`uncapFrameRate` 按未知键拒绝。名字只在解析之后、仍然为空时用文件名补一次。
- RHI 只留通用工具：`EPresentMode` 与字符串互转、`choosePreferredPresentMode`、`INativeWindow::matchDrawablePixelSize`。帧级 GPU 计时只在主窗口的 `recordGuiSnapshot` 里打点。
- GUI 按像素改窗口走 `GUIWindowHost::matchDrawablePixelSize`：按密度换算成窗口尺寸，再调私有的 `requestWindowSize`（置 `bSwapchainRecreatePending` 并清除最小化）。游戏适配器仍直接调 `INativeWindow::matchDrawablePixelSize`。
- 窗口缩放不再丢掉已排队的 present mode，单独提交 `[vulkan] resize keeps a queued present mode`。
- 验收（profile 构建，共享工作区无其它构建，随后已切回 debug / `ya_profile=engine` / `ya_linkage=shared`）：`Example/GUIWorkbench/Benchmark/GUIWorkbench.bench.json`，Dock，1920×1080，immediate，预热 120，采样 600。`meta.swapchainExtent` 与 `renderResolution` 都是 1920×1080，present mode 为 immediate，`sampledFrames` 600。frame p50 **3.050 ms** / p99 16.593 ms，frame.cpuMs p50 **0.775 ms** / p99 14.607 ms，frame.gpuMs p50 **2.122 ms** / p99 3.585 ms，RSS p50 **192.78 MiB**（峰值 192.98 MiB），GPU 合计 **46.66 MiB**，`unifiedMemory` true。`gpu_passes` 为空。Apple M5，MoltenVK 1.4.1，Vulkan 1.3.334。HelloMaterial 短跑（预热 8、采样 16）的 summary 键树与改宿主路径之前的那份一致：顶层 `schemaVersion`、`meta`、`frame`、`cpu_stages`、`gpu_passes`、`render_counters`、`process_memory`、`gpu_memory`，meta 键集合相同。配置测试 13 项通过（`BenchmarkConfigTest`、`GuiBenchmarkConfigTest`、`PerfLabBenchmarkConfigTest`）。`ya-testing` 1783 项里 1781 通过；失败的两项来自工作区里未提交的 Dialogue 文档：`ReflectionSceneResave.UiDocumentsMatchBaseline` 对不上已提交基线，`GameUIHostTest.DialogueDocumentStaysInsideNarrowAndWideCanvases` 在已提交文档上预先就失败（盒 760×168、x=260，测试期望 880×160、x=200）。
