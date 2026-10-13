## 动画（framework 层）

边界：框架只做 Slate 那一半（时钟 + easing + 少量可动画属性）。轨道/关键帧/
clip player 属于未来 Game UI 层（对标 UMG WidgetAnimation），评价结果通过
**同一个可动画属性接缝**写回 widget，绝不在 widgets 内核里再造第二套
属性/失效系统。设计记录见 `.agent/plan/archive/gui-animation/plan.md`。

- 接缝（OCP）：一个 widget 类型用 `FUIAnimPropertyTable` 声明自己可被动画
  操纵的属性（own entries + base 链，见 `UIAnimation.h`）。驱动者（tween /
  将来的 clip player）只认 `(widget, propertyId, value)`：`findAnimatableProperty` /
  `applyAnimatableProperty` / `readAnimatableProperty`。加新可动画属性 = 在控件
  类型里加表项，**不改**驱动者。
- 基础目录：`UIElement` 自带 render transform 四通道 —— `opacity`(Float)、
  `renderTranslation`(Vec2)、`renderScale`(Vec2，围绕 `_pivot`)、`tint`(Vec4)，
  继承到整个子树（UMG RenderOpacity / RenderTransform 语义）。这不是 `UIOverlay`
  （叠层 layout host）。`renderRotation`
  未入目录：快照 item 是 axis-aligned quad，旋转需要 compose 支持 rotated
  quad。
- 写路径唯一真源是 changed-only setter（`setRenderOpacity` 等，失效走
  `EUIPropertyImpact::SubtreePaintContext`，因为子树继承 render transform）。禁止动画
  field poke、禁止把 `_bVolatile` 当动画、禁止在 paint/layout 回调里 spawn 动画。
- 时钟/tween：`UIAnimClock` 是 `UITween` 的内部时钟（对标 FCurveSequence），
  **不是** WidgetTree 上的第二套 tick。`UITween` 不是行为；驱动者是控件唯一的
  `UIAnimatorBehavior`（`IUITickable`）：`WidgetTree::tick` → animator 登记项 →
  按创建顺序 tick 各 tween。animator 只在有 tween 播放时登记（tween 播放状态变化即同步开关），
  结束自动注销、树不再拜访；
  播完且无外部句柄的 tween 由 animator 丢弃。挂上 tween 的**唯一接口**是
  `ya::ui::animate(widget, dt)`（找到或创建 animator，再加一个 tween）；同一控件可多次调用得到
  多个独立 tween（各自时钟）。onFinished 可做 ping-pong（playReverse）。
  写回 widget 一律走可动画属性接缝（`applyAnimatableProperty` / setter）；将来的
  Game UI clip player 也写同一条缝，仍然不是平行 dirty 通道。
- render transform 解析：`UIFrameBuilder::pushRenderTransform`，emit 时映射 rect/color/clip
  （缓存 draw-item 段存的是解析后结果，所以 transform 变化必须 invalidate 子树，
  setter 已保证）。
- 授权 DSL（首选写法）：`ya::ui::animate(widget 或 builder, duration)` 取回 tween，
  链式 `->fade(from,to,ease)` / `.scale()` / `.slide()` / `.tint()` /
  `.track(handle, from, to, ease)` / `.setDuration()` / `.setLoop()` /
  `.setOnFinished()` / `.play()`。typed 句柄 `ya::ui::anim::opacity|scale|translation|tint`
  把值域编进类型；自定义属性用 `ya::TUIAnimProperty<float>{"gauge"}` 声明，写错值类型
  是编译错误而不是运行时拒绝。tween 由 widget 的 animator 持有，所以句柄可丢弃
  （`ya::ui::animate(card, 0.2f)->fade(0.0f, 1.0f).play();` 就是完整动画）。
- 两态/重定位：`playToward(Forward|Backward)` 从当前位置继续，`setLerpNow(v)` 直接落位
  并停表。「目标态变了就朝它走」的控件（开关、hover 反馈）用这两个，不要用
  `play()`/`playReverse()` 重启，否则中途反向会跳值。
- 多关键帧（curve）：`tween->curve(ya::ui::anim::opacity, {ya::animKey(t, v, ease), …})`
  是同一个 track 的分段形式——仍是「一个属性 / 一个时钟 / 一个 widget」，只是求值从两端点
  变成 N 个键。键时间用**归一化时钟时间**（0..1），`setDuration` 整体改速；键拥有自己的时刻，
  同一时刻的后键胜出（离散跳变）；首键之前与末键之后保持该键的值（不外推）；时刻必须非递减、
  值域必须与属性声明一致，否则该 track 在 resolve 时被拒并只警告一次。
  多对象 / 事件轨 / blend 仍属 Game UI clip player，不要塞进框架。
- 默认带动画的控件：`UISwitch`（DSL `ya::ui::toggle(...)`）。它用 `animate()` 取得
  一个 `UITween` 驱动自己声明的 `progress` 通道（`kAnimSwitchProgress`）：值立即
  翻转，knob 位移 + track 配色插值；静止时不在 tick 登记表上，
  `setTransitionSeconds(0)` 可整体关掉动画。
  哪些控件该默认携带动画、哪些应 opt-in，见 `.agent/plan/archive/gui-animation/plan.md` §8。
- 零时长时钟语义：`duration<=0` 表示“无动画”，`getLerp()` 返回被放置的那个端点
  （play→1、playReverse→0、setLerp(v)→v）。不要写回“恒返回 1”，否则 instant 控件会被画成
  终态（已由 `ZeroLengthClockReportsTheEndpointItWasPlacedAt` 锁住）。
- 验收：`GuiAnimationTest` 25 例（接缝/类型/tween 生命周期/render transform 映射/playToward/
  setLerpNow/零时长/curve 求值与拒绝/UISwitch 行为与 knob 几何）；Workbench `Animation/Tween` 页 +
  `Scenarios/animation_gallery.jsonl`（真实点击 + `assert_validation_clean`）。



## 静态 DSL（live construct）

- Builder 所有权：`parent.child(builder)` 传**左值**时只挂载、不消耗调用者的 builder
  （内部 `takeMountRef` 先拷贝再 release），所以「先挂载、后取句柄」是安全的；
  `parent.child(std::move(builder))` 才是显式交出。任何已消耗 builder 上的
  `share()` / `widget()` 会断言报错，而不是递出空指针。禁止恢复「child() 偷偷清空左值」
  的语义：那正是 animation gallery `Toggle all` 崩溃（空句柄在几百帧后才被解引用）的成因；
  契约由 `DeclarativeContractTest.MountingAnLvalueBuilderLeavesItUsable` 等三例锁住。
- 默认路径：`ui::column/row/text/button/checkBox/slider/comboBox/image/textField/canvasPanel/border/splitPane/scroll/overlay/sizeBox/...` 组好 detached builder，再显式 `.release()` / `.share()` 取出 live `UIElement`，最后 `ui::attach(tree, parent, widget, slot)` 挂进树（Slate `SNew` + `SAssignNew`）。`ui::attach` 不隐式 `release()`；build 阶段与 attach 阶段分开。带 slot 的 `ui::attach(tree, parent, widget, slot)` 要求 `parent` 是带 `SlotArgs` 的具体 host（`UICanvasPanel`/`UIBorder`/`UIContainer`/`UISplitPane`/`UICanvasRoot`…），不能传 `UIElement&`，这样 canvas/box/overlay/content 在编译期选对。不要把整棵 DSL 包进 `ui::attach(...)`。`setAnchors` / `fillParent` / Border `setCornerRadius` / Border+Text `setStyleKey` / `setStyle`（freeze）/ `setStyleField`（单键 inherit）/ container `childFill` 与 `child(node, FBoxSlotArgs)` / overlay `child(node, FOverlaySlotArgs)` / content host `child(node, FContentSlotArgs)` 在 Construct 时写到 live widget。`setTooltip` 写在 base builder；Text `setWrap` / `setMaxWrapWidth` 控制折行；base `setVisibility`；split `setPadding`。`ui::button` 没有 `setText`；文字走内部 `UIText` 子 widget。值更新走 `Reactive<T>`；已知结构走 `attach`/`detach`/`setVisible`。自定义 / 复杂 demo widget（MenuBar、TreeView、TableGrid、DragFloat/SpinBox/RadioButton/ColorEdit/SearchComboBox、UIDragDropTile、DockSpace、SelectableRow）用 `child(UIElementRef)` 挂进 DSL 壳，不要为此扩 Construct。GUIWorkbench Feature Gallery 各页与 Workbench 内置 Editor demo（`FWorkbenchSurface::buildEditorDemo`）已是 `.release()` + `ui::attach`。Editor 的 `rebuildItemRows()` 仍是事件期 live attach/detach `UISelectableRow`。弹层（Menu / Modal / Dialog）仍在点击时 live 组装。Dock floating host 仍 `attachToLayer(Popup, host, fill canvas args)`。Render 仍是 raw retained 对照。GameEditor chrome 已切到 `EditorSurface`（整窗 WidgetTree，不是 ImGui 内嵌 panel）。
- `UIDescription` / `UIReconciler` / `UIRenderController` / apply hook **已删除**。不要恢复 Description → apply → widget 转发层。
- Document/script：`UIDocument::instantiate()`（registry factory）只实例化一次。变长集合走列表控件 + `ReactiveList`，不是整页 re-run。
- `UIScreen` 是挂卸 / z-order / input blocking，不是每帧 `render()` owner。Gallery / Editor 不使用它；接到游戏多表面（HUD/模态）之前保持搁置。


