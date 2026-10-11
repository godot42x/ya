# Progress

## 2026-10-12 — 立项：G3.0 方案问答两轮 + 子计划五件套

- 子计划目录 `.agent/plan/3d-demo/` 建立；showcase §1 追加决策行、§3 G3 表改指向本目录（G3.0 标完成）。
- 两轮问答结论（本次立项的决策事实）：
  1. **玩法与验收**：第三人称探索解谜（showcase 2026-10-11 决策 A 方案），验收 = 打包后从庭院走到地下室结局 + 性能叠加层 + 通关录屏。
  2. **物理组件（G3.1）**：加 Kinematic；box / sphere / capsule 带尺寸参数；glTF 生成静态关卡三角网格碰撞与 convex 碰撞；读世界变换与缩放；传感器；接触事件进 ECS。谜题对应：关卡网格 / 箱子（动态 box）/ 压力板（传感器）/ 门（Kinematic）。
  3. **物理 Lua API（G3.2）**：按谜题给——raycast（E 交互）、onTriggerEnter / Exit（压力板）、Kinematic 目标位置（门）、施力 / 速度接口。
  4. **角色控制器（G3.3）**：`JPH::CharacterVirtual`；推动态刚体走接触回调施加冲量。
  5. **glTF（G3.4）**：skin / animation / 节点层级；骨骼上限用选中素材实测评估，`MAX_BONE_COUNT=64` 不够就提。
  6. **动画（G3.5）**：按名字播放 + 双 clip 交叉淡入；clip 清单 idle / walk / run / jump / push；素材缺 push 用 walk 顶替，记已知缺口。
  7. **相机输入（G3.6）**：第三人称弹簧臂 + 碰撞避让；InputMap 补鼠标轴；输入轴部分提到第一波先行。
  8. **运行时（G3.7）**：`world.spawn(path)`；世界计时器（开门动画、箱子掉出地图后重生）。
  9. **光照（G3.9）**：三区光照依赖 R15b、聚光灯阴影（新增工作项）、R2、R3。
  10. **收尾（G3.11）**：性能叠加层依赖 F5 修正后的指标口径；音频 A1 排在它之前。
  11. **波次**：第一波 G3.4 + G3.1 + G3.6 输入轴；第二波 G3.2 / G3.3 / G3.5 / G3.7；第三波 G3.8 / G3.9 / G3.11。冲突面：物理模块、资源导入器、输入系统、AppLifecycle.cpp 各同时只允许一个 worker。
- 素材调研（写进 plan §3）：KayKit 三包（Dungeon Remastered / Adventurers / Character Animations）全 CC0；Character Animations 161 个 clip，**分类里没有 push**——与 G3.5 的已知缺口预案吻合。Quaternius 官网许可证已改版为 QAL v1.0（免费免署名但**禁止再分发素材本身**；itch.io 页仍写 CC0 属过时）——因此降为备选，push 需求极强时才考虑。推荐组合（待用户确认）：KayKit Dungeon Remastered + Adventurers(Knight) + Character Animations。
- 待用户确认两项（不自行拍板）：示例项目目录名；素材最终选型。
- 已核实：F5 在 `.agent/` 全树无对应计划条目（perf-baseline plan 的 checkpoint 只有 PB1–PB7）——F5 是会话内的口径修正记号，G3.11 依赖它时需先在 perf-baseline 立项，plan §2 外部依赖已如实标注。
