# 3D Demo — 第三人称探索解谜（vertical slice）

2026-10-12 立项，`showcase-roadmap` §3 G3 组的子计划。G3.0 方案问答（2026-10-12 两轮）已收口，结论在 `progress.md`；本文是执行口径。

## 0. 目标与验收

第三人称探索解谜 demo：庭院 → 地下室两段关卡，三个谜题，展示多光源阴影 / 聚光灯 / IBL / 后处理。

**总验收**：打包后在干净运行路径上，从庭院走到地下室结局，全程有性能叠加层；有一段完整通关录屏（对应 showcase `G2.5` / `G3.11` 的录屏口径）。

谜题与引擎能力的对应（每个引擎工作项都被谜题真实需要，不做谜题用不到的能力）：

| 谜题元素 | 依赖引擎项 |
| --- | --- |
| 走动的角色（移动 / 跳跃 / 推箱子） | G3.3 角色控制器、G3.5 动画、G3.6 相机与输入 |
| 关卡静态碰撞（庭院地面、地下室墙体） | G3.1 glTF → 三角网格碰撞 |
| 推到压力板开门的箱子 | G3.1 动态 box、传感器、接触事件；G3.2 onTrigger |
| 踩板开门 | G3.1 传感器、G3.2 onTriggerEnter/Exit、G3.7 计时器 |
| 门（平移 / 旋转开闭） | G3.1 Kinematic、G3.2 Kinematic 目标位置、G3.7 计时器 |
| 交互物（E 键检查 / 拉杆） | G3.2 raycast、G3.6 输入 |
| 三区光照（庭院日光 / 走廊聚光灯 / 地下室点光群） | G3.9 + R15b + 聚光灯阴影 + R2 + R3 |

## 1. 决策（2026-10-12 问答拍板，逐条见 progress.md）

| 议题 | 决定 |
| --- | --- |
| 玩法范围 | 第三人称探索解谜（showcase 2026-10-11 决策的 A 方案）；轻动作（战斗 / 敌人 AI）维持 P2 不进本期 |
| 验收 | 打包后从庭院走到地下室结局；性能叠加层在画面上；录屏留档 |
| 物理组件 | 新增 Kinematic 类型；box / sphere / capsule 带尺寸参数；从 glTF 生成静态关卡三角网格碰撞与 convex 碰撞；读取世界变换与缩放；传感器类型；接触事件进 ECS（G3.1） |
| 物理 API | raycast（E 交互）、onTriggerEnter / onTriggerExit（压力板）、Kinematic 目标位置（门）、施力 / 速度接口（推箱子）都按谜题需要给（G3.2） |
| 角色控制器 | 基于 `JPH::CharacterVirtual`：移动 / 跳跃 / 贴地；推动态刚体在接触回调里施加冲量（G3.3） |
| glTF 导入 | skin + animation + 节点层级；用选中素材的实际骨骼数评估 `MAX_BONE_COUNT=64` 是否够，不够就提升上限（G3.4） |
| 动画 | 按名字播放 + 双 clip 交叉淡入；需要的 clip：idle / walk / run / jump / push；素材没有 push 就先用 walk 代替，记为已知缺口（G3.5） |
| 相机与输入 | 第三人称弹簧臂 + 碰撞避让；InputMap 补鼠标轴（G3.6） |
| 运行时生成 | `world.spawn(path)`；世界计时器（开门动画、箱子掉出地图后重生）（G3.7） |
| 光照 | 三区光照依赖 R15b 聚光灯、**聚光灯阴影（本轮新增工作项）**、R2、R3（G3.9） |
| 收尾 | 性能叠加层依赖 F5 修正后的指标口径；音频 A1 排在 G3.11 之前（G3.11） |
| 素材 | 场景 KayKit Dungeon Remastered、角色 KayKit Adventurers + Character Animations（调研见 §3）；Quaternius 因许可证改版（QAL v1.0 非_CC0_，禁止再分发素材本身）降为备选 |
| 待用户确认 | 示例项目目录名（§4）；素材最终选型（§3 候选表） |

## 2. 工作项与波次

优先级全部 P0（G3.10 编辑器项维持 showcase 的 P1，不在本表；G3.12 维持 P2）。

| id | 内容 | 冲突面 |
| --- | --- | --- |
| G3.1 | 物理组件重设计：Kinematic 类型；box / sphere / capsule 尺寸参数；glTF 静态关卡 → 三角网格碰撞、凸包 → convex 碰撞；读世界变换与缩放；传感器；接触事件进 ECS。谜题用法：关卡网格、箱子（动态 box）、压力板（传感器）、门（Kinematic） | 物理模块 |
| G3.2 | Lua 物理 API：`raycast`（E 交互）；`onTriggerEnter` / `onTriggerExit`（压力板）；Kinematic 目标位置（门）；施力 / 速度接口 | 物理模块 + 脚本绑定 |
| G3.3 | 角色控制器：`JPH::CharacterVirtual` 移动 / 跳跃 / 贴地；推动态刚体（接触回调施加冲量） | 物理模块 |
| G3.4 | glTF 导入：skin、animation、节点层级；用选中素材实际骨骼数评估 `MAX_BONE_COUNT=64` | 资源导入器 |
| G3.5 | 动画：按名字播放 + 双 clip 交叉淡入；clip 清单 idle / walk / run / jump / push；素材缺 push 用 walk 代替（已知缺口） | 动画 / 资源导入器 |
| G3.6 | 相机与输入：第三人称弹簧臂 + 碰撞避让；InputMap 补鼠标轴。**输入轴部分在第一波先行** | 输入系统 |
| G3.7 | 运行时生成：`world.spawn(path)`；世界计时器（开门动画、箱子掉出地图后重生） | 脚本绑定 |
| G3.8 | 谜题内容（Lua）：庭院 → 地下室，三个谜题，压力板 / 推箱 / E 交互 / 门 | demo 项目目录 |
| G3.9 | 三区光照：庭院（日光 + CSM）、走廊（聚光灯 + 聚光灯阴影）、地下室（点光群 + R2）。依赖 R15b、聚光灯阴影（新增，归 `shadow-and-light-line`）、R2、R3 | demo 项目目录 + LightPass shader |
| G3.11 | 性能叠加层（M 组数据实时显示）+ 打包 + 录屏。依赖 F5 修正后的指标口径；音频 A1 排在它之前 | UI 叠加层 + 打包脚本 |

> 注意编号沿用 showcase §3：没有 G3.10 在本计划内（它是 P1 编辑器项），没有 G3.12（P2）。

### 波次

同一波内可并行（按 §4 冲突面分配文件）；跨波按依赖。

1. **第一波**（互不重叠）：G3.4 glTF 导入；G3.1 物理组件；G3.6 的输入轴部分。
2. **第二波**：G3.2（依赖 G3.1 组件与事件）；G3.3（依赖 G3.1）；G3.5（依赖 G3.4）；G3.7。
3. **第三波**：G3.8 谜题内容（依赖第二波全部）；G3.9 三区光照（依赖 R15b + 聚光灯阴影 + R2 + R3）；G3.11 收尾（依赖 A1 与 F5）。

外部依赖（不在本计划内做，但要盯）：
- `shadow-and-light-line/`：R15b（已落地，待内容场景像素级确认）、**聚光灯阴影（新增项）**、R2、R3。
- `reflection-runtime/` RR8（脚本 native 注册迁移）— G3.2 的新绑定写在新 ABI 上；若 RR8 未收口，G3.2 开工前与该线对齐时序。
- A1 音频（showcase P1）— 排在 G3.11 之前。
- F5 指标口径修正（perf-baseline；**该线 plan 尚未登记此项，G3.11 开工前先在 perf-baseline 立项**）。

## 3. 素材调研（2026-10-12）

许可证均以官方页面原文为准，链接如下；选定后入库时在素材目录附 `LICENSE.md`（沿用 `Example/2DRpgPrototype/Content/Textures/LICENSE.md` 的做法）。

### 候选

| 套件 | 许可证（原文要点 + 链接） | 骨骼数 | 自带动画 | 体积 | 备注 |
| --- | --- | --- | --- | --- | --- |
| **KayKit Dungeon Remastered**（场景） | CC0，「Free for personal and commercial use, no attribution required. (CC0 Licensed)」；附加约束：不得原样转卖、不得署名为己。[kaylousberg.itch.io/kaykit-dungeon-remastered](https://kaylousberg.itch.io/kaykit-dungeon-remastered) | —（静态环境件） | 无（纯静态：墙 / 地板 / 楼梯 / 门 / 箱子 / 火盆等 200+ 模块件） | Free 1.1 = 31 MB（FBX / GLTF / OBJ） | 地下室 + 庭院模块件齐全；有门、箱子、压力板可用的道具件 |
| **KayKit Adventurers**（角色） | 同上 CC0。[kaylousberg.itch.io/kaykit-adventurers](https://kaylousberg.itch.io/kaykit-adventurers) | Rig_Medium（5 个角色：Knight / Rogue / Mage / Barbarian / Ranger） | 基础移动动画；完整集在 Character Animations 包 | Free 2.0 = 12 MB（FBX / GLTF） | GLTF 直出，正好喂 G3.4 |
| **KayKit Character Animations**（动画集） | 同上 CC0。[kaylousberg.itch.io/kaykit-character-animations](https://kaylousberg.itch.io/kaykit-character-animations) | Rig_Medium / Rig_Large | **161 个**：idle、walk、run、jump、crouch、sneak、melee / ranged / magic、emote（wave / cheer / sit）、工具（digging / lockpicking / hammering）等 | Free 1.1 = 14 MB（FBX / GLTF） | 页面分类里**没有 push**——印证 G3.5 的已知缺口预案（push 用 walk 代替）；有 interacting，可用于 E 交互 |
| Quaternius Universal Animation Library（备选） | **不是 CC0**：官网 License 页现为 Quaternius Asset License (QAL) v1.0——personal / educational / commercial 免费、免署名，但「may not resell or redistribute the Assets themselves」；itch.io 页仍写 CC0 属过时。[quaternius.com/packs/universalanimationlibrary.html](https://quaternius.com/packs/universalanimationlibrary.html)、[quaternius.com/license.html](https://quaternius.com/license.html) | universal humanoid rig | 120+：8 向 locomotion、jog、sprint、**crawling / swimming / sitting / death**；页面宣传含 push（"push" 在描述里，逐 clip 清单要下载后核对） | 页面未标；FBX / GLB / Blend | 动画更全（疑似有 push），但 QAL 禁止再分发素材本身，入库仓库的合规性比 KayKit 差一档 |
| Quaternius Animated Humans（备选，角色） | 同 QAL v1.0 | humanoid | 未核实（页面抓取被限流） | 未核实 | 与 UAL 配对使用 |

### 推荐（待用户确认）

- **场景：KayKit Dungeon Remastered**（免费档 31 MB，CC0，模块件覆盖庭院 + 地下室）。
- **角色：KayKit Adventurers（Knight）+ KayKit Character Animations**（全 CC0，GLTF 直出）。
- 骨骼数：KayKit Rig_Medium 按低模惯例远低于 64（下载后实测核写进 progress）——`MAX_BONE_COUNT=64` 大概率够，但以实测为准。
- 已知缺口：素材包没有 push clip → G3.5 用 walk 代替，记入 feature_matrix。
- 若用户更看重 push 动画本身，可换 Quaternius UAL 动画集（需接受 QAL：素材入库仅限本仓库使用、不得再分发——与开源仓库相容性存疑，需用户裁决）。

## 4. 项目与目录（待用户确认）

- 示例项目目录名**待用户确认**（候选：`Example/3DPuzzleDemo`、`Example/3d-puzzle-demo`，或与 2D 命名风格对齐的 `3DExploreDemo`）。下文用 `Example/3DPuzzleDemo/` 占位，选定后全文替换。
- 内容结构沿用 2D demo：`Content/Models/`、`Content/Scenes/`、`Content/Scripts/`、`Content/Textures/`，素材原包放 `Content/Assets/<pack>/` 并附 `LICENSE.md`。

## 5. 冲突面（派发约束）

同一时间只允许一个 worker 改：

- **物理模块**（G3.1 / G3.2 / G3.3 三项串行或分文件认领；组件定义、物理系统、JPH backend 同属一处）
- **资源导入器**（G3.4 / G3.5）
- **输入系统**（G3.6 输入轴部分）
- **AppLifecycle.cpp**
- 脚本绑定（G3.2 / G3.7；且与 `reflection-runtime` RR8 全局互斥）

派发 prompt 必须写明：用 `Script/agent/ya_lock.sh run --` 排队、每个 worker 的文件范围不重叠、长驻进程不放锁内。

## 6. 本文件的维护

- 每个 checkpoint 收口时同步 `todo.md` / `feature_matrix.json`，`progress.md` 记一条；与代码同一提交。
- 素材选型、目录名两个待确认项落定后，回填 §1 / §3 / §4 并在 progress 记录。
