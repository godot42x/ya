# 进度

## 2026-10-14 — P0–P4 落地

### 做了什么

**P0 框架能力**（`[core/locale] P0`）

- `LocalizationManager`：`onLanguageChanged` 单订阅者改为
  `MulticastDelegate<void(std::string_view)>`，
  新增 `addLanguageChangedListener` / `removeLanguageChangedListener`。
- `tr(key, fallbackSource)`：命中表 → fallback 链 → `fallbackSource`（非空）→ key。
  语言激活但未命中时记入 `missingKeys()`。
- `format(text, args)`：`{N}` 位置替换，越界保持原样（不引入 CLDR）。
- `LocalizedText`（`GUI/Binding`，不在 Core）：key + args + fallback 源串的 reactive
  句柄，语言变化时自刷新；`toReactive()` 用 aliasing shared_ptr 让订阅与句柄/绑定
  同寿命。
- 脚本 `locale.tr(key[, fallback])` 支持第二参数。
- 测试：多播多订阅者、`format`、fallback 源串、`missingKeys`、reactive 跟随语言。

**P1 静态 chrome**（`[editor/locale] P1`）

- 新建 `GameEditor/Localization/EditorLocalization.{h,cpp}`：`loadEditorTables` /
  `setEditorLanguage` / `editorTr`，表路径 `Engine/Content/Locale/editor.<lang>.json`。
- `Engine/Content/Locale/editor.zh.json`：菜单 / tab 标题 / 命令标签 / 设置串共约 100 条。
- 菜单栏（File/Edit/View + Window/Layout）、命令标签、窗口标题走 `editorTr`。

**P2 原地刷新**（`[editor/locale] P2`）

- `FDockContext::FPanel::titleBinding` + `addPanel(...,titleBinding)` +
  `bindPanelTitle` / `setPanelTitle`；`extractPanel`/`adoptPanel` 携带 binding，
  跨 dock / tear-off 不丢。
- `UITabButton::bindLabel` 与 `UIMenuBarItem` 的 reactive label：paint 与 measure
  都读 resolved label，tab / 菜单项按新文案重新量尺寸。
- `FEditorTabSpawner::titleKey`；`EditorDockWorkspace::materializeTab` 建 binding；
  `EditorSurface::syncPageTabs` 把 binding 绑到 page tab。
- `EditorSurface` 订阅 `LocalizationManager`，语言切换刷新窗口标题 / 原生菜单。
- 测试 `DockNodeTest.ReactivePanelTitleUpdatesTabInPlace`：`title->set()` 后 tab label
  变化且 dock leaf id 不变（无 rebuild）。

**P3 面板正文 + 设置**（`[editor/locale] P3`）

- 设置对话框新增 Language 下拉：切换 + 持久化 `editor.locale.language`；
  `relocalize()` 每 tick 重译静态标签（本帧生效）。
- `EditorModule::onConfigure` 读回保存语言。
- Inspector empty / Content Browser（Back/List/Grid/New UI/New Script）/ Play toolbar
  Detach 走 `editorTr`。
- 测试 `EditorSettingsDialogTest.LanguageSwitchRelocalizesOpenDialog`。

**P4 工具**（`[editor/locale] P4`）

- `Script/editor_locale_lint.py`：扫描 `editorTr` / `editorText` 调用，key 既无表项又
  无 fallback 即报错；接入 `Script/ci.sh`。

### 验证结果

- `xmake r ya-testing`：**1838 passed**（P0 前 1836，+2 新用例）。
- `Script/editor_locale_lint.py`：33 call site 全覆盖，exit 0。
- CJK 实跑：`Engine/Saved/Config/Editor.json` 临时 `locale.language=zh`，
  `xmake r` 编辑器 + `--screenshot`，截图确认「关卡 / 层级 / 视口 / 统计 / 检查器 /
  运行时 / 分离 / < 返回 / 新建 UI / 列表 / 网格 / 在层级中选择一个实体」渲染正常
  （CJK fallback 命中）。
- lint：`Script/editor_locale_lint.py` 在 P0 后第一次运行即抓到 `Linear`/`Nearest`
  两条无表项无 fallback 的 key，已补表。

### 剩余 / 偏离

- 面板正文只覆盖三个代表面板（Inspector / Content / Play toolbar）；其余 tab 文案
  仍为英文源串。它们本身可读（英文源串即显示），后续按需补 `editorTr` + 表项即可，
  lint 会对新增的 dotted key 兜底。
- 动态 key（如按组件名拼 `editor.property.<name>`）尚未在渲染点接入，留给后续；
  lint 只覆盖字面量 key。
- 未改 `Example/2DRpgPrototype`（RPG 线并行），`layout` 表与游戏侧 `Content/Locale/zh.json`
  互不影响。
