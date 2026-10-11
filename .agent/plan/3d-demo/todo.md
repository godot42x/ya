# TODO

计划见 `plan.md`。图例：`[ ]` 未开工 · `[~]` 进行中 · `[x]` 完成。

## 第一波（可并行）

- [ ] G3.4 glTF 导入：skin / animation / 节点层级；实测骨骼数评估 `MAX_BONE_COUNT=64`（冲突面：资源导入器）
- [ ] G3.1 物理组件：Kinematic、box / sphere / capsule 尺寸、glTF 网格 / convex 碰撞、世界变换与缩放、传感器、接触事件进 ECS（冲突面：物理模块）
- [ ] G3.6（输入轴部分）InputMap 补鼠标轴（冲突面：输入系统）

## 第二波

- [ ] G3.2 Lua 物理 API：raycast / onTriggerEnter / onTriggerExit / Kinematic 目标位置 / 施力与速度（依赖 G3.1；冲突面：物理模块 + 脚本绑定，与 RR8 互斥）
- [ ] G3.3 角色控制器：CharacterVirtual 移动 / 跳跃 / 贴地 / 推箱（依赖 G3.1；冲突面：物理模块）
- [ ] G3.5 动画按名播放 + 双 clip 交叉淡入（依赖 G3.4；push 缺 walk 顶替为已知缺口）
- [ ] G3.7 `world.spawn(path)` + 世界计时器

## 第三波

- [ ] G3.8 谜题内容（Lua）：庭院 → 地下室、三谜题
- [ ] G3.9 三区光照（外部依赖：R15b ✓、聚光灯阴影（新增项）、R2、R3）
- [ ] G3.11 性能叠加层 + 打包 + 录屏（外部依赖：A1 音频先做；F5 指标口径修正——perf-baseline 尚未立项）

## 待用户确认（阻塞项）

- [ ] 示例项目目录名（plan §4 占位 `Example/3DPuzzleDemo/`）
- [ ] 素材最终选型（推荐 KayKit Dungeon Remastered + Adventurers + Character Animations，plan §3）

## 外部依赖盯梢（不在本计划内做）

- `shadow-and-light-line/`：聚光灯阴影新增项、R2、R3（R15b 已落地）
- `reflection-runtime/` RR8 与 G3.2 的时序对齐
- A1 音频（showcase P1）
- F5 指标口径修正（perf-baseline 立项）
