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
- **基础属性目录**：`UIElement` 的 render transform —— `opacity` / `renderTranslation` /
  `renderScale`（围绕 `_pivot`）/ `tint`，继承到子树（UMG RenderOpacity /
  RenderTransform、Godot modulate 语义），hit test 与 layout 不受影响。
  这不是 `UIOverlay`（叠层 layout host）。
- **写路径**：每个属性有 changed-only setter，失效走 `EUIPropertyImpact::SubtreePaintContext`
  （子树继承 render transform）；descriptor 不复制 impact，避免两处声明漂移。
- **驱动**：`UIAnimClock`（duration / play / playReverse / pause / resume / loop /
  timeScale / hasFinished）+ `UITweenBehavior`（一个时钟驱动 owner 的 N 条 track，
  `wantsTick()` 仅播放中为真，结束即回到干净、树不再拜访，无 `_bVolatile`）。
- **解析点**：`UIFrameBuilder::pushRenderTransform` 在 emit 时映射 rect/color/clip；
  缓存段是解析后结果，transform 改动必须 invalidate 子树（setter 已保证）。
- **验证**：`Engine/Test/Source/GuiAnimationTest.cpp`（closure target，8 例：接缝
  继承顺序、类型不匹配拒绝、tween 推进 + 结束后零重建、下游属性驱动、transform 自身
  项映射、transform 子树继承、clock 端点/循环、easing/lerp 域）；Workbench
  `--start-page=Tween`（新 `Animation` 分组）+ `Scenarios/animation_tween.jsonl`
  headless `--scenario-render` 通过，无 G2 validation mismatch。

## 1.1 每帧开销画像（2026-09-15 实测，debug/arm64，1280x720，426 widget 树）

| 场景 | median ms/frame | rebuilt widget |
|---|---|---|
| 静止（全部命中 draw-item cache） | 0.334 | 0 |
| 单个叶子 render transform 动画（按钮/旋钮/卡片） | 0.346 | 1 |
| 子树 render transform 动画（容器 opacity，页级转场） | 3.26 | 421 |
| 递归 paint 脏标记本身 | 0.006 | – |
| `tree.tick`（无动画） | 0.013 | – |
| `tree.tick`（1 条 tween 播放中） | 0.014 | – |

结论与对策：

- **静止帧零成本**：identity render transform 不 push、不走映射，cache 复用照常，`wantsTick`
  为假的树不产生额外重建。
- **叶子动画≈免费**（+0.012 ms）：重建数 1。HUD 的按钮反馈、旋钮、卡片都属这一档，
  是推荐形态。
- **子树 render transform 是唯一热点**：容器上做 opacity/scale 会按设计重画整棵子树
  （缓存的 draw item 是在旧 transform 空间里解析的）。421 widget 的子树约 3.3 ms/帧。
  对策是用法约束而不是隐藏成本：页级转场尽量用 render transform 收敛到较小子树；
  需要持续多帧的整页淡入应评估是否值得（UE/UMG 的 RenderOpacity 同样要让子树重画）。
- **脏标记递归不是瓶颈**（0.006 ms，占子树帧 0.2%）：曾经考虑的
  `invalidateSubtree` 早退优化被实测否决，不做。
- **tick 走树是 O(n)**（426 widget ≈ 0.013 ms，约 30 ns/widget）：当前可接受；
  若未来出现数千 widget 的常驻树且 profile 命中，再考虑 active-tick 注册表。
- 每次 tick 的属性解析改为**按 owner 解析一次并缓存 descriptor**（`_resolved`，
  track/owner 变化时失效），per-tick 不再做字符串查找。代价模型回归测试：
  `GuiAnimationTest.RenderTransformCostModelIsLeafVsSubtree`（确定性计数，非计时）。

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

## 4. 授权 DSL（2026-09-15 已落地）

`ya::ui::animate(widget 或 declarative builder, duration)` 返回 tween，可链式授权：

```cpp
auto pop = ya::ui::animate(card, 0.45f);
pop->fade(0.0f, 1.0f, ya::EUIAnimEase::OutCubic)
    .scale({0.85f, 0.85f}, {1.0f, 1.0f}, ya::EUIAnimEase::OutBack)
    .play();

// 控件自己的通道，值域编进类型（写错是编译错误）
tween->track(ya::kAnimSwitchProgress, 0.0f, 1.0f);   // 或 ya::ui::anim::opacity
```

刻意没做的：

- 不做“按名字查属性”的字符串 DSL：属性用 typed handle（`TUIAnimProperty<T>`）声明在控件旁边，
  驱动者拿到的仍是 `(widget, id, value)`，OCP 不破。
- 不做链式 `.ease()` 这类隐藏“作用于上一条 track”的状态：ease 是 track 的参数，显式传。
- 不做 framework 级 timeline / delay / sequence 语法：序列是应用层组合（见下）。

两态控件（`UISwitch`、以后 hover 反馈）用 `playToward(Forward|Backward)` 从当前位置继续，
`setLerpNow(v)` 直接落位；用 `play()`/`playReverse()` 会在中途反向时跳值。

## 5. 第一个默认带动画的控件：UISwitch

- `UISwitch`（DSL `ya::ui::toggle(...)`，typeId `engine.switch`，style key `switch`
  复用 `FCheckBoxStyle`）：值立即翻转，knob 位移 + track 配色插值；控件**自己**持有
  一个 tween 驱动自己声明的 `progress` 通道（`kAnimSwitchProgress`）。
- 静止时 `wantsTick()==false`（无每帧成本）；`setTransitionSeconds(0)` 关掉动画；
  构造即 on 的开关由 `onAttached()` 落位，不会动一下才画对。
- 顺带修掉一个真 bug：`UIAnimClock::getLerp()` 在 `duration<=0` 时恒返回 1，导致
  “instant 控件” `setLerpNow(0)` 反而被画成终态。现在零时长时钟返回被放置的端点
  （play→1 / playReverse→0 / setLerp(v)→v），由单测锁住。

## 6. Feature Gallery：Animation 页覆盖的形态

`--start-page=Tween`（分组 `Animation`）现在演示 5 类：叶子 tween（fade/scale/slide/
ping-pong）、子树过渡（一个 overlay 驱动整组卡片的显示/隐藏，含中途反向）、
序列（5 张卡用 onFinished 串成 stagger，应用层组合）、默认带动画控件
（4 个不同时长的 switch，含 instant 与“构造即 on”）、app 自定义属性（gauge 的 `fill`）。
场景 `Scenarios/animation_gallery.jsonl` 用真实坐标点击每一个按钮并帧推进，
末尾 `assert_validation_clean` 要求增量绘制与全量重绘一致。

## 7. 下一步顺序

1. `UICheckBox` 的勾选标记动画（复用同一套 leaf tween，控件自带 transition）；
2. pointer enter/leave → behavior 的 dispatch seam，之后做 opt-in hover / implicit
   transition helper（按钮按压缩放、tab 下划线走这里）；
3. 需要布局动画（expander 高度、split 比例）时，先开 slot 目录 + `Layout` impact 切片；
4. HUD 需要多对象时间轴时再写 clip player，消费 §1 的接缝。

## 8. 哪些默认控件该默认携带 tween（判断标准 + 现状）

判断标准：只有当「状态值」与「视觉位置/透明度」是同一件事时才默认动画；只是交互反馈或
叙事效果的走 opt-in；会影响 measure/arrange 的走单独切片。

| 控件 | 默认携带 | 理由 / 做法 |
|---|---|---|
| `UISwitch` | **是（已实现）** | 值变就是 knob 位移本身；不做动画控件语义就残缺 |
| `UICheckBox` | 建议下一刀 | 勾选标记出现/消失做 fade+scale，同一套 leaf tween |
| `UIExpander` / `DisclosureChrome` | 建议（需要形变方案） | 箭头用两个状态 crossfade（不做旋转：快照 item 是轴对齐 quad）；动高度的部分属 layout 切片 |
| `UIButton` hover/press | 否（opt-in） | 工具条/编辑器按钮动起来是口味问题，且每次进出都会重绘；给 helper 不给默认 |
| `UITabBar` 选中下划线 | opt-in | 外观决定；`playToward` 两态 tween |
| `UITooltip` / `UIMenu` / `UIPopupOverlay` | 否（宿主/应用层） | dwell 是 tree 逻辑；弹层淡入淡出属宿主呈现策略 |
| `UISlider` / `UIDragFloat` / `UISpinBox` | 否 | 输入控件必须即时；数值平滑属 presenter |
| `UIScrollViewport` | 否（不同机制） | 需要的是滚动物理/惯性，不是属性 tween |
| 布局类（expander 高度、split 比例、slot padding） | 暂不 | 必须 `EUIPropertyImpact::Layout` + slot 目录，属独立切片 |

补齐 opt-in hover 反馈还差一个 seam：pointer enter/leave 目前只跑控件自己的虚函数
（各控件 override 且不调基类），behavior 收不到；需要给 `UIElement` 加非虚 dispatch
包装（tree 调它，它先调虚函数再转发给 behaviors）。这是下一刀，不在本切片。
