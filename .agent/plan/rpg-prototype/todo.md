# TODO — RPG Tactics (v2)

一眼版进度。详细目标与验收见 `plan.md`；能力覆盖审计见 `feature_matrix.json`。

## 图例

- `[ ]` 未开工 `[~]` 进行中 `[x]` 完成 `[!]` 阻塞

## Wave A — 重编基础

- [x] F1 项目壳重编 + Kenney 素材入库（Tiny Dungeon / Tiny Battle），新默认场景显示营地地图

## Wave B — 战术核心（先跑通主循环）

- [x] T1 棋盘与回合骨架（回合队列按速度、行动序条、格子高亮——高亮随 T2 落地）
- [x] T2 移动与可达范围（BFS 高亮 + 逐格移动）—— 鼠标拾取 screen->cell 延后（键盘光标已够）
- [x] T3 攻击 / 技能 / 伤害结算（伤害数字、HP 条、死亡移除）
- [x] T4 敌方 AI（接近→攻击→移动）—— Boss 分阶段留到内容期（C1）
- [ ] M1 战斗与探索拼接（营地→遗迹→遇敌→战斗→回营地）**第一次完整闭环**

## Wave C — 四子系统（每个都要有真实使用点）

- [ ] R1 后处理运行时控制 + 相机效果（fade / look / shake / zoom）
- [ ] A1 音频子系统（SDL3：BGM crossfade、sfx、总线音量）
- [ ] X1 粒子子系统（.yaparticle 资产 + 发射器 + 命中/魔法/治疗特效）
- [ ] P1 2D 物理 / 触发器（Box2D：推箱、压力板、尖刺、移动平台、击退）

## Wave D — 成长与内容

- [ ] G1 成长：等级 / 属性 / 技能 / 职业
- [ ] G2 装备与背包
- [ ] G3 队伍与同伴
- [ ] G4 任务系统
- [ ] C1 内容：3–4 场战斗 + 3 任务 + Boss + 结局

## Wave E — 收口

- [ ] Z1 存档 / 读档闭环
- [ ] Z2 UI 完善与打磨（战斗 HUD、设置、对话）
- [ ] Z3 收口：矩阵全绿 + 打包 + 通关录屏

## 待用户确认 / 决策门

- [ ] 素材下载与入库（Kenney zip 是否由我去取？还是你手动放？）
- [ ] 单位动画策略（Tiny Battle 静态是否接受；需要动画才用 image-gen 生成）
- [ ] 后处理作用域（管线级先做；是否需要每 View 独立 look 到 R1 再定）
- [ ] Box2D 与 tile 棋盘的空间边界约定（写进 `2d-gameplay` skill）
- [ ] 目录名（沿用 `2DRpgPrototype` 还是改名，如 `2DTacticsRpg`）

## 历史（v1）

v1 落地内容与遗留见 `todo-v1-landed.md`、`progress-v1-landed.md`。
