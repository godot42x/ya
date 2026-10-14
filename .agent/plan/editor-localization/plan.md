# 编辑器本地化（P0–P4）

> 建立：2026-10-14
> 关联：`./.agent/skills/game-editor/SKILL.md`、`./.agent/skills/gui-framework/SKILL.md`、
> `./.agent/skills/font-rendering/SKILL.md`
> 状态：P0–P4 已落地

## 0. 问题

编辑器 chrome（菜单 / dock tab / 面板正文 / 窗口标题）此前全是硬编码英文字面量，
没有本地化入口。引擎侧只有 `Core/Localization/LocalizationManager`（content 用，
`tr(key)` + 单订阅者 `onLanguageChanged`），且 key=源串对长串不友好。

## 1. 边界（不做的事）

- 不引入第三方本地化库，不做 CLDR 复数 / 性别规则；`{}` 风格插值即可。
- 不重建 widget：语言切换必须**原地**刷新，dock 布局（split / 浮动窗 / tear-off 位置）
  不变。
- Core 不依赖 GUI：`Reactive` 属 binding 层，句柄放 `GUI/Binding`，不放 Core。
- 不手改 `Generated/*`；不动 shader / 渲染链。

## 2. 关键决策

- **key 策略**：短串（<30 字符，如 `"File"`）key=源串；长串用点分标识符 +
  English fallback 源串，`editorTr("editor.inspector.empty", "Select an entity")`。
  两者都未命中时返回 fallback，最后才是 key，保证任何情况下都可显示。
- **分层**：`LocalizationManager`（Core，多播事件 + `format` + `missingKeys`）→
  `LocalizedText`（GUI binding，句柄 + reactive，随语言变化自刷新）→
  `EditorLocalization`（GameEditor，`editorTr` / `editorText` / 表加载）。
- **原地刷新**：dock panel 携带可选 `titleBinding`，docked tab well / 浮动 tab strip /
  菜单栏项都读 `bindLabel`；tab 与菜单项改为按 resolved label 量尺寸。语言切换只
  repaint + re-measure，不 rematerialize dock 树。

## 3. 阶段

| 阶段 | 目标 | 状态 |
| --- | --- | --- |
| P0 | Core 框架能力：多播语言事件、`format`、`tr(key,fallback)`、`missingKeys`、`LocalizedText`、脚本 `locale.tr(key[,fallback])`、缺 key 诊断 | 已落地 |
| P1 | 静态 chrome：`EditorLocalization` 模块 + `editor.zh.json` + 菜单栏 / 命令标签 / 窗口标题 | 已落地 |
| P2 | dock tab 标题绑定：`FDockContext::bindPanelTitle` + tab/菜单项 reactive label，原地刷新不 rebuild | 已落地 |
| P3 | 面板正文 + 设置：语言下拉（切换 + 持久化 `editor.locale.language`）；Inspector / Content Browser / Play toolbar 代表串 | 已落地 |
| P4 | 协作工具：`Script/editor_locale_lint.py` 接入 `Script/ci.sh` | 已落地 |

## 4. 验收

1. `xmake r ya-testing` 全绿（含 P0/P2/P3 新增用例）。
2. 编辑器默认英文，chrome 与改动前逐字节一致。
3. 设置切到中文，本帧内全部刷新，dock 布局不变。
4. 重启后保持中文（`editor.locale.language`）。
5. `Script/editor_locale_lint.py` 无缺失 key。
6. CJK fallback 实跑确认（截图见 `progress.md`）。
