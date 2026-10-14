# 编辑器本地化 — 进度速览

| 阶段 | 目标 | 状态 | 验证 |
| --- | --- | --- | --- |
| P0 | Core：多播事件 / format / tr(key,fallback) / missingKeys / LocalizedText / locale.tr | ✅ | `ya-testing` 13 例 |
| P1 | 静态 chrome：EditorLocalization + editor.zh.json + 菜单/标题 | ✅ | 编辑器截图（zh） |
| P2 | dock tab 标题绑定，原地刷新不 rebuild | ✅ | `DockNodeTest.ReactivePanelTitleUpdatesTabInPlace` |
| P3 | 设置语言下拉 + 持久化 + 面板代表串 | ✅ | `EditorSettingsDialogTest.LanguageSwitchRelocalizesOpenDialog` |
| P4 | `editor_locale_lint.py` 接入 CI | ✅ | lint exit 0 |

## 未完成 / 后续

- 面板正文只覆盖 Inspector / Content / Play toolbar；其余 tab 仍显示英文源串。
- 动态 key（`editor.property.<name>` 按组件名拼）未接入渲染点。
- RPG 线（`Example/2DRpgPrototype/`）不在本线范围。
