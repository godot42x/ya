# GUI 动画切片：framework 层已落地，Game UI 轨道层延后

> 建立日期：2026-09-15
> 上游：`.agent/plan/gui-invalidation-architecture/animation-integration.md`
> （本切片细化了它的「动画值 = Reactive」草案：属性驱动动画写 widget 自己的
> 可动画属性并走 changed-only setter，Reactive 仍用于数据绑定动画）
> 状态：框架层第一刀已落地并验证；Game UI 轨道/关键帧层延后

## 0. 边界（业界对齐）

| | 框架控件层（本切片已做） | 游戏 HUD / 设计器层（延后） |
|---|---|---|
| UE | Slate FCurveSequence：时钟 + easing + 少量控件自取样 | UMG UWidgetAnimation：多轨关键帧 + 事件，评价后写 RenderOpacity/RenderTransform |
| Unity | UI Toolkit USS transition（属性变化即插值） | uGUI Animator + AnimationClip |
| Godot | Tween：代码里 tween_property | AnimationPlayer：命名 clip、方法轨 |
| Flutter/CSS | AnimatedOpacity / transition: opacity .2s | 显式 timeline / @keyframes（不在基础 widget 里） |
| YA | `UIAnimClock` + `UITweenBehavior` + 可动画属性表 | clip player：tracks + keyframes + notifies，写在 document/HUD 根 |

共同结论：框架只给「一个时钟 + 一条曲线 + 少数 paint 属性」；时间轴/多对象/
事件属于播放器，播放器消费框架的属性写入面。

## 1. 已落地（framework 层，`Engine/Source/Framework/GUI/Runtime/Widgets/UIAnimation.*`）

- **可动画属性接缝（OCP）**：`FUIAnimPropertyTable`（own entries + base 链）。
  驱动者只认 `(widget, propertyId, value)`：`findAnimatableProperty` /
  `readAnimatableProperty` / `applyAnimatableProperty` / `collectAnimatablePropertyIds`。
  下游控件（含 app 自定义 widget）加自己的可动画属性不需要改框架任何一个文件。
- **基础属性目录**：`UIElement` 的 paint-only overlay —— `opacity` / `renderTranslation` /
  `renderScale`（围绕 `_pivot`）/ `tint`，继承到子树（UMG RenderOpacity /
  RenderTransform、Godot modulate 语义），hit test 与 layout 不受影响。
- **写路径**：每个属性有 changed-only setter，失效走 `EUIPropertyImpact::SubtreePaintContext`
  （子树继承 overlay）；descriptor 不复制 impact，避免两处声明漂移。
- **驱动**：`UIAnimClock`（duration / play / playReverse / pause / resume / loop /
  timeScale / hasFinished）+ `UITweenBehavior`（一个时钟驱动 owner 的 N 条 track，
  `wantsTick()` 仅播放中为真，结束即回到干净、树不再拜访，无 `_bVolatile`）。
- **解析点**：`UIFrameBuilder::pushPaintOverlay` 在 emit 时映射 rect/color/clip；
  缓存段是解析后结果，overlay 改动必须 invalidate 子树（setter 已保证）。
- **验证**：`Engine/Test/Source/GuiAnimationTest.cpp`（closure target，8 例：接缝
  继承顺序、类型不匹配拒绝、tween 推进 + 结束后零重建、下游属性驱动、overlay 自身
  项映射、overlay 子树继承、clock 端点/循环、easing/lerp 域）；Workbench
  `--start-page=Tween`（新 `Animation` 分组）+ `Scenarios/animation_tween.jsonl`
  headless `--scenario-render` 通过，无 G2 validation mismatch。

## 1.1 每帧开销画像（2026-09-15 实测，debug/arm64，1280x720，426 widget 树）

| 场景 | median ms/frame | rebuilt widget |
|---|---|---|
| 静止（全部命中 draw-item cache） | 0.334 | 0 |
| 单个叶子 overlay 动画（按钮/旋钮/卡片） | 0.346 | 1 |
| 子树 overlay 动画（容器 opacity，页级转场） | 3.26 | 421 |
| 递归 paint 脏标记本身 | 0.006 | – |
| `tree.tick`（无动画） | 0.013 | – |
| `tree.tick`（1 条 tween 播放中） | 0.014 | – |

结论与对策：

- **静止帧零成本**：identity overlay 不 push、不走映射，cache 复用照常，`wantsTick`
  为假的树不产生额外重建。
- **叶子动画≈免费**（+0.012 ms）：重建数 1。HUD 的按钮反馈、旋钮、卡片都属这一档，
  是推荐形态。
- **子树 overlay 是唯一热点**：容器上做 opacity/scale 会按设计重画整棵子树
  （缓存的 draw item 是在旧 overlay 空间里解析的）。421 widget 的子树约 3.3 ms/帧。
  对策是用法约束而不是隐藏成本：页级转场尽量用 render transform 收敛到较小子树；
  需要持续多帧的整页淡入应评估是否值得（UE/UMG 的 RenderOpacity 同样要让子树重画）。
- **脏标记递归不是瓶颈**（0.006 ms，占子树帧 0.2%）：曾经考虑的
  `invalidateSubtree` 早退优化被实测否决，不做。
- **tick 走树是 O(n)**（426 widget ≈ 0.013 ms，约 30 ns/widget）：当前可接受；
  若未来出现数千 widget 的常驻树且 profile 命中，再考虑 active-tick 注册表。
- 每次 tick 的属性解析改为**按 owner 解析一次并缓存 descriptor**（`_resolved`，
  track/owner 变化时失效），per-tick 不再做字符串查找。代价模型回归测试：
  `GuiAnimationTest.OverlayAnimationCostModelIsLeafVsSubtree`（确定性计数，非计时）。

## 2. 未完成 / 显式延后

- `renderRotation`：快照 item 是 axis-aligned quad，旋转需要 compose 侧支持
  rotated quad（给 `UIFrameDrawItem` 加 rotation + pivot）。先不占位。
- implicit transition（USS / Flutter implicit / QML Behavior on）：给少数属性配
  duration，目标值变化时自动插值。等 hover 渐变这类真实需求。
- 层次二（播放中直接 `markPaintDirty`、跳过 notify 遍历）：只在 profile 证明
  notify 是热点时做。
- 反射层 `.animatable()` 标记：等设计器 / 轨道编辑器需要「可绑定字段」清单时。
- 布局类动画（expander 高度、slot padding）：可以 tween，但 setter 必须声明
  `EUIPropertyImpact::Layout`，且 slot 目录要单独声明；尚未开口。

## 3. Game UI 层（延后）设计要点

- clip = tracks + keyframes + notifies；归属 UIDocument / UICompoundWidget / HUD 根，
  `WidgetTree` 不知道 clip 存在（对齐 UUserWidget 持 UWidgetAnimation、Slate 不持 Sequencer）。
- track 绑定 `(widget stableKey / name, propertyId)`；每帧 evaluate 后调用
  `applyAnimatableProperty`，失效链、快照、compose 全部复用。
- 禁止：player 直接改 draw item、player 自己开 `_bVolatile`、给每个按钮挂 clip。
- 时序：host tick（含 player 评价）必须在 `buildSnapshot` 之前，与框架 tween 同序。
- 启动条件：出现真正需要多对象 / 时间轴 / 事件轨的 HUD 需求（血条、伤害数字、
  技能盘、过场 UI）。

## 4. 下一步顺序

1. 第一个真实手感需求（按钮按压缩放 / tab 下划线 / expander）直接用 tween；
2. hover fade 出现后做 implicit transition；
3. HUD 需要时间轴时再写 clip player，消费 §1 的接缝。
