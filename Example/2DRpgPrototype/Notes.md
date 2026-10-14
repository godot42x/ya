# 2D RPG Prototype 反馈记录

> **说明**：本文件记录用户反馈与解决状态。完整反馈表见 `.agent/plan/rpg-prototype/plan.md` §0.1（U1-U5）、§0.3（U6-U11）、§0.4（U12）。

---

## 原始反馈（v1，2026-09 之前）

**U1: 本地化 feature**
本地化 feature（游戏 + 引擎 + 编辑器）应该是一个优先级比较高的东西，实际场景中大家都需要多语言。

**状态**：
- ✅ 游戏侧已解决（`LocalizationManager` + `locale.*` 脚本模块 + `Content/Locale/zh.json`，提交 `6125b9f8` + `23ce126a`）
- ✅ 引擎侧已解决（同上）
- 🔄 编辑器侧进行中（P0-P3 实施中，4 个并行 worker 之一）

---

**U2: AI Native 与生态位**
AI Native 和 Yet Another Game Engine 是冲突的。我们应该去找自己的生态位。现在 2D Game Framework 已经非常成熟了（Godot/Unity 无需多言），我们应该考虑一下要怎么去做才能体现自己的亮点和优势。

**状态**：战略方向，已登记但未落地。相关探索见"AI Native 工作流探索"报告。

---

**U3: 伤害跳字应用世界空间文字**
伤害跳字用了 digits 图，应该改用 Sprite2D 或者世界空间文字组件。（实际上 2drpg 的时候是有 Sprite2D 的，只不过现在的伤害跳字用的是 sprites 做出来的数字图片）

**状态**：✅ 已解决（WT1 落地 `Text2DComponent` + `world.spawnText`，提交 `f2149908`；`BattleFx.lua` 的伤害跳字改用世界文字实体，数字图集退役）

---

**U4: 行动面板与 HUD**
行动面板是否更应该在底部横向来排布，实际上现在关于敌人、技能的选择非常简陋（只有 1234 菜单），应该有更多的操作空间和 UI 展示。血条血量也应该在单位头顶，直接展示。

**状态**：✅ 已解决（UB1 底部横向战术面板 + 头顶血条，提交 `28a2aeb5` + `e6f0a608`）

---

**U5: UI 面板风格化**
目前的 UI 面板和 HUD 都非常地简陋，应做风格化面板，可能需前置开发。

**状态**：⚠️ 部分解决（UB2 落地 Kenney UI Pack 九宫格接入，`Border.setFillImage` / `Button.setFillImage` 支持九宫格；真正的按钮控件留待后续风格化 UI 一起做，已归入 `gui-style-system-convergence` 线）

---

## 后续反馈（v2，2026-09 至今）

**U6-U12** 见 `.agent/plan/rpg-prototype/plan.md` §0.3/§0.4，包括：
- U6: wasd+空格主流程 bug（已解决）
- U7: 本地化引擎化提前（已解决）
- U8: 鼠标操纵缺失（已解决）
- U9: 参照其他战旗游戏 UI（已解决）
- U10: 世界文字字形断裂（已解决）
- U11: 世界文字为何"用 3D text"（已调研定案）
- U12: 单位头顶文字遮挡/排版/色调（已解决，提交 `410c7bd7` + `a1f363ff`）

---

## 工作流探索问题

> 真的让一个开发者手动开发，不借助 AI，是什么样？

这个问题的探索见 AI Native 工作流调研报告（2026-10-14），结论：YA Engine 的 `.agent/` 体系在"反过早完成"和"验证留痕"两项上强于业界方案（Cursor/Copilot/Claude Code），但在 prompt cache 控制、worktree 隔离、跨 session 接力成本上弱于 Claude Code。

---

## 参考资料

- 完整反馈表：`.agent/plan/rpg-prototype/plan.md` §0.1/§0.3/§0.4
- 进度追踪：`.agent/plan/rpg-prototype/progress.md`
- 能力矩阵：`.agent/plan/rpg-prototype/feature_matrix.json`
