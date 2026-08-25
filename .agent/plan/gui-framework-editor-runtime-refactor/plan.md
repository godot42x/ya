# GUI Framework / Editor / Game UI 重构计划

## 目标

让同一套 retain-mode GUI framework 同时承载独立 GUI app、Game Runtime UI 和未来移除 ImGui 后的 Game Editor。目标参考 Slate/UMG：WidgetTree、布局、输入、focus、popup、drag/drop、snapshot/compose 由 framework 提供；Game UI 的外观由 project/application authored properties 控制，不强制依赖 UITheme；UITheme 保留为 Editor、tooling、standalone app 的可选默认样式机制。

UI document（独立文件格式）暂不做；Scene 的 Game UI entry 目前只承载 inline document。重构主线先收口 "GUI 替换 Editor UI"：同一套 framework + DSL 先吃自己的狗粮（Editor），再支撑 Game UI，类似 UMG 基于 Slate、Godot Node2D 基于 Control 树的关系。

## 架构决策

Framework 提供 WidgetTree、UIElement、Layout、Input、UIFrameBuilder、Brush、draw primitives、visual state 和 invalidation contract；Editor 提供 EditorTheme/semantic tokens；Project/Game 提供 authored colors、textures、brushes、documents 和 runtime bindings。

外观解析优先级固定为：explicit runtime/authored property > widget-local authored default > optional scoped/theme style > framework neutral fallback。Game UI 不要求挂载 theme。

不在本计划中恢复 ImGui、强制 Game UI 使用 UITheme、一次性重写全部控件，或把 project-specific tokens 放入 framework。

## 当前缺口

1. FBrush 的 NinePatch/Border 目前仍降级为整图拉伸。
2. 大量控件直接暴露颜色/尺寸字段，runtime 修改没有统一的 changed-only setter/invalidation contract。
3. CheckBox、ComboBox、Slider、TextField、TreeView、TableGrid、Dialog、Menu、InputExtras 缺少统一 visual state contract。
4. DSL（Declarative）与 Game UI document 体系是两套平行系统：闭集 EWidgetKind vs UITypeRegistry typeId、typed description 字段 vs 反射 fields，尚未收口。
5. rounded rect、border、opacity、gradient、shadow、transform 等视觉原语不完整。
6. Editor 未来迁移需要 editor-grade dense controls、property editing、validation visual states。

## Phase -1：React-style 函数式 DSL 与 Reconciler

第一阶段先做 DSL，但目标不是 XML/CSS 外部语法，而是 React 风格的 C++ 函数式 DSL：UI 函数返回 builder/description，reconciler 再把 description 应用到 retained WidgetTree。暂不做 ui document / 外部文件格式映射。

目标形态：UI function(state) -> UIBuilder/UIDescription -> keyed reconcile -> retained WidgetTree -> layout/input/snapshot。

示意 API：UIBuilder buildSettings(const SettingsState& state) { return ui::column("settings").child(ui::text("title").text(state.title)).child(ui::button("save").text("Save").onClick(...)); }

函数每次可以重新返回 builder，但 stable key 对应的 retained widget 必须复用，不能每次重建所有 live widget。

### 渐进式实施原则

- 不预先实现完整数据绑定、完整组件生命周期或完整 DSL 语法；每一层只实现当前垂直切片所需的最小能力。
- 每个阶段先写契约测试，再写实现，再接入一个真实控件，最后跑 windowed/offscreen/headless 回归。
- 旧 retain API 在迁移期继续可用；DSL 只能作为新增路径，不能先破坏 WidgetTree。
- 每一步都必须能独立编译、运行和回滚；不允许跨多个 Phase 的大批量重写。
- 任何新抽象必须先有一个真实 consumer 和测试，禁止为了未来可能需求提前建复杂框架。

### 必须先冻结的契约

- Widget identity：区分 type、stable key、display name；key 用于声明式重建和状态保留，不能只依赖 name。
- Ownership/lifecycle：声明式节点如何 mount、update、detach、reparent；哪些状态 keep-alive，哪些随节点销毁。
- Property mutation：颜色、brush、布局、文本、selection 等属性的 setter、事务和 invalidation 规则。
- Data flow：Reactive/observable 的读取依赖、写入通知、双向绑定、computed 派生值和 batch 更新。
- Resource reference：纹理、字体、材质、图标的引用格式、resolve 时机、缺失 fallback 和 queue-submit 保活。
- Event contract：事件 callback 的生命周期、capture/focus、popup/modal、取消和异步回调安全边界。
- Serialization boundary：哪些字段属于 scene inline document authoring，哪些属于运行时状态，哪些不能序列化。
- Layout vocabulary：Overlay、Grid、Wrap/Flow、Canvas/absolute、List virtualization 是否进入第一版 DSL。

### DSL 设计边界

第一版吸收 React 的 render function、keyed children、单向 state -> description 数据流，以及 EUI-NEO 的链式 builder；保留现有 retained tree 和 immutable snapshot。第一版必须支持 stable key、条件子树、列表 keyed children、属性绑定、事件绑定、slot/layout props 和显式 state ownership。

公共 API 采用控件专属 builder/factory 加共享内部节点契约的分层：基础层只管 identity、children、通用 metadata；Text/Button/TextField/Container 等各自暴露自己的语义字段。不要让一个通用 builder 承载未来所有控件的属性，这会把后续重构锁死在最宽松的最小公分母上。

第一版不承诺 XML/CSS 语法、Virtual DOM 全量替代 WidgetTree、自动双向数据绑定、任意 lambda 捕获 live widget 裸指针，或在 command recording 期执行 render function。

### Phase -1 实施顺序

#### -1A：契约冻结（只读设计与测试，不迁移业务）

1. 定义 UIBuilder/UIDescription 节点模型和 stable key 规则。
2. 定义 builder 分层：公共 node contract、控件专属 builder、共享属性注入点。
3. 定义 builder props：layout、appearance、text/value、event、children、visibility。
4. 定义 mount/update/remove/reorder/detach 的生命周期和 callback 安全边界。
5. 定义 state ownership：外部 state、widget transient state、derived state 的边界。

退出条件：设计文档、重复 key/缺失 key 诊断、最小契约测试全部明确；没有未决的 identity/lifecycle 语义问题。

#### -1B：最小 Reconciler（不接复杂数据绑定）

1. 实现 same-key reuse、insert、remove、reorder、detach。
2. 接入现有 WidgetTree，但只支持 Panel、Text、Button、Column/Row。
3. 增加 mount/update/remove/reorder 的 WidgetTreeDump 和 snapshot 测试。

退出条件：同一 description 重复应用不会重复创建同 key widget；删除和重排不会留下 parent/focus/capture 残留。

#### -1C：第一个有状态控件

1. 接入 TextField 或 ScrollViewport（二选一，优先 TextField）。
2. 验证 focus、caret 或 scroll offset 在 same-key update/reorder 后保留。
3. 验证条件子树移除时 transient state 和事件 capture 正确清理。

退出条件：状态保留和状态清理都有自动化断言，且 windowed/offscreen snapshot 一致。

#### -1D：最小数据流

1. render function 读取只读 state/props。
2. event callback 写回外部 model，由下一次 render 产生 description 更新。
3. Reactive 只接入已存在的依赖追踪和 dirty invalidation，不在此阶段实现完整 computed/bidirectional binding。
4. 增加 batch update，保证一次业务事务不会重复 reconcile 同一子树。

退出条件：state 变化只更新受影响的 props/子树；command recording 期不读取 live model；重复相同值不会产生多余 dirty transition。

### Phase -1 验收

- React-style render function 连续返回 builder 不会重复创建同 key widget；key 相同的控件能保留 focus、text edit、scroll、popup 等允许保留的状态。
- state 变化只更新受影响的 props/子树；不会在 command recording 期读取 live model。
- DSL 不依赖 UITheme；没有 theme 也能描述和渲染 authored appearance。
- 至少完成一个 Button/TextField/列表 prototype，并用 snapshot/scenario 验证 mount、update、remove、reorder、focus 和 resource fallback。
- 在 Phase -1 完成前，不开始 Editor 全量迁移，不扩展大规模控件样式，不承诺外部 XML 语法稳定。

### Phase -1 禁止事项

- 不实现 XML/CSS parser。
- 不实现完整 Virtual DOM 或全树替换。
- 不把 DSL 节点直接绑定为裸 UIElement 指针并由业务长期持有。
- 不在同一阶段同时迁移 TreeView、DockSpace、Inspector 等复杂 Editor 控件。
- 不以“demo 能显示”为完成标准，必须通过生命周期、状态、snapshot 和 teardown 测试。

## DSL 在 Game UI 中的使用设计（案例驱动）

（yaui 文件格式已移除；本节以 UMG / Godot Control 树对照的具体案例定义 DSL 的应用层用法。）

### 概念映射

| UMG / Godot | YA 对应 | 说明 |
|---|---|---|
| UserWidget (WBP) / 场景里的 Control 根 | `UIScreen`（render function + 生命周期） | 一个自包含的 UI 单元 |
| CanvasPanel + 锚点 | `Panel`/`Container` + position/size（anchor 待 Phase 4） | 左上原点 |
| AddToViewport + ZOrder / CanvasLayer | `ScreenStack::push(zOrder)` | 栈顶决定输入归属 |
| Blueprint Property Binding / NativeTick 更新 | 写 state → `invalidate()` → 下帧 flush diff | 显式单向数据流 |
| NamedSlot / WidgetSwitcher | slot 容器 + 子 reconciler（G4） | document 骨架 + 代码填内容 |
| FocusPath / DirPad 导航 | EWidgetFocusPolicy 文档序焦点链 | gamepad 导航需新增（见缺口） |
| UMG Preview / Godot 编辑器 | UIDesignerPanel preview（G5 reconciler 化） | 同一套树 |

### 现状盘点：两套平行系统

DSL 侧（`Framework/GUI/Runtime/Declarative/`，ya::ui）：
- `UIDescription` + 闭集 `EWidgetKind`（Column/Row/Panel/Text/Button/TextField）。
- 控件专属 builder（`ui::column("key").child(...)` / `.content(...)` / `.when(cond, ...)`）。
- `UIReconciler`：keyed reconcile（same-key reuse / insert / remove / reorder / detach），身份 = (kind, stableKey)。
- `UIRenderController`：render function + invalidate + batch + flush（单向数据流）。
- 消费者：仅 DeclarativeContractTest，还没有任何宿主。

Game UI 侧（`Framework/GUI/Runtime/Widgets/` + `GameRuntime/GUI/GameUI/`，ya）：
- `UITypeRegistry`：稳定 typeId（"engine.panel"）+ factory + module lease；`UIDocument` inline 挂在 `SceneWidgetEntry` 上。
- `GameUIHost`：WidgetTree owner，viewport/presentation、event dispatch、snapshot build；`IGameUIController` 可替换挂载策略。

断点：EWidgetKind 闭集 vs typeId 平行；typed optional 字段 vs 反射 fields 两套属性系统；DSL 没接入 GameUIHost 帧循环与场景生命周期。

### 统一模型：三个收口

1. 类型收口：DSL 节点身份从 `EWidgetKind` 迁到 `UITypeRegistry` typeId；`UIDescription` 内部 = `{ typeId, stableKey, fields, children }`；Column/Row 折叠为 `engine.container`。builder API 保持控件专属 typed C++。
2. 属性收口：DSL 属性应用走与 `UIDocument::fields` 相同的反射字段路径，same-key update 时 changed-only diff。本质：`UIDescription` = 一帧的 `UIDocument` 视图。
3. 生命周期/时机收口：screen 由 `GameUIHost` 统一挂载，随场景激活/停用挂卸；flush 固定在 app update 阶段、buildSnapshot 之前、command recording 之外。

---

### 案例A：动作游戏 HUD（动态 screen，最高频）

对应 UMG 的 WBP_HUD（血条 + 弹药 + 准星）+ 属性绑定。

```cpp
// ---- game state（普通 C++ struct / ECS 组件镜像，UI 不拥有它）----
struct HudState {
    float    health    = 1.0f;   // 0..1
    int      ammo      = 30, ammoMax = 90;
    bool     bReloading = false;
    glm::vec2 crosshairPos = {400, 300};
};

// ---- screen = UMG 的 UserWidget ----
class HudScreen final : public UIScreen {          // 需新增 UIScreen
public:
    explicit HudScreen(HudState& state) : _state(state) {}

    UIDescription render() override {
        return ui::panel("hud-root")
            .child(ui::row("top-bar").setPosition({16, 12}).setSpacing(8)
                .child(ui::panel("health-bg").setSize({220, 18}).setColor({0, 0, 0, 0.5f})
                    .child(ui::panel("health-fill")               // 过渡期用 Panel 填色；
                        .setSize({220 * _state.health, 18})        // ProgressBar 列入缺口清单
                        .setColor({0.9f, 0.2f, 0.2f, 1})))
                .child(ui::text("ammo")
                    .setText(std::format("{} / {}", _state.ammo, _state.ammoMax))
                    .setFontSize(20)))
            .child(ui::panel("crosshair")
                .setPosition(_state.crosshairPos).setSize({4, 4})
                .setColor({1, 1, 1, 0.8f}))
            .when(_state.bReloading, [] {
                return ui::text("reloading").setText("RELOADING...").setFontSize(28);
            });
    }
private:
    HudState& _state;   // 事件只写 state 再 invalidate，绝不持有 widget 指针
};
```

挂载与数据流（GameApp 侧）：

```cpp
// 场景激活时挂载（controller 策略内）：
auto hud = host.getScreenStack().push(0, HudScreen{_hudState});

// 游戏逻辑（ECS 掉血回调 / 网络回写）：
void GameApp::onPlayerDamaged(float hp01) {
    _hudState.health = hp01;
    _hudState->invalidate();      // 下一次 host.flushDirty() 才 re-render + diff
}
```

UMG 的属性绑定在此显式化：回调写 state → invalidate → flush 时 reconciler 对 same-key 节点只 diff 变化字段（血条 width、弹药文本），准星等未变节点零开销。UMG NativeTick 式高频更新 = 高频 invalidate，因 changed-only diff 成本可控。

### 案例B：主菜单（静态布局 + 事件 + 面板切换）

对应 Godot 的 `MainMenu(Control) > VBoxContainer > [Title, BtnNewGame, BtnContinue, BtnQuit] + SettingsPanel`。

```cpp
class MainMenuScreen final : public UIScreen {
    UIDescription render() override {
        return ui::panel("menu-root").setColor({0.04f, 0.05f, 0.08f, 1})
            .child(ui::column("menu-col").setPosition({80, 120}).setSpacing(12)
                .child(ui::text("title").setText("YA").setFontSize(48))
                .child(ui::button("new-game").setText("New Game")
                    .setFocusPolicy(EWidgetFocusPolicy::Click)
                    .onClick([this] { _game.startNewGame(); }))
                .child(ui::button("continue").setText("Continue")
                    .setEnabled(_saves.hasSave())            // 无存档置灰
                    .onClick([this] { _game.loadLatest(); }))
                .child(ui::button("settings").setText("Settings")
                    .onClick([this] { _state.bShowSettings = true; invalidate(); }))
                .child(ui::button("quit").setText("Quit")
                    .onClick([this] { _game.quit(); })))
            .when(_state.bShowSettings, [this] { return renderSettingsPanel(); });
    }

    UIDescription renderSettingsPanel() {
        return ui::panel("settings").setPosition({240, 160}).setSize({400, 260})
            .setColor({0.1f, 0.1f, 0.12f, 0.95f}).setPadding({24, 24})
            .child(ui::column("settings-col").setSpacing(10)
                .child(ui::text("msaa-label").setText("MSAA"))
                .child(ui::comboBox("msaa-choice")          // 需新增：DSL comboBox builder
                    .options({"Off", "2x", "4x"}).selected(_state.msaaIndex)
                    .onSelect([this](int i) { _state.msaaIndex = i; invalidate(); }))
                .child(ui::button("close").setText("Close")
                    .onClick([this] { _state.bShowSettings = false; invalidate(); })));
    }
};
```

关键语义：`_state.bShowSettings` 翻转时 `when()` 增删子树，reconciler 只对 settings 子树 mount/unmount，菜单其余节点原样保留（含焦点位置）。

### 案例C：背包（keyed 列表 + 条件子树）

React keyed children 的典型场景；Godot 里对应手动 add_child/remove_child 的地方。

```cpp
class InventoryScreen final : public UIScreen {
    UIDescription render() override {
        auto grid = ui::column("inv").setSpacing(8);
        for (const Item& item : _inventory.items()) {
            grid.child(ui::row(item.guid)              // key = guid（不是数组下标！）
                .child(ui::panel("icon").setSize({48, 48}).setColor(item.rarityColor()))
                .child(ui::column("meta")
                    .child(ui::text("name").setText(item.name))
                    .child(ui::text("count").setText("x" + std::to_string(item.count))))
                .when(item.bEquipped, [&] {
                    return ui::panel("equipped-badge").setSize({6, 6}).setColor({1, 0.8f, 0, 1});
                }));
        }
        return ui::panel("inv-root")
            .child(ui::text("weight")
                .setText(std::format("{:.1f} / {:.1f} kg", _inventory.weight(), _inventory.maxWeight())))
            .child(std::move(grid));
    }
};
```

拾取/丢弃/整理后 `items()` 变化：guid 不同的格子 insert/remove；guid 相同的格子只 diff 文本/徽标；格子的选中态、hover 态因 same-key reuse 保留。不写任何 add_child/remove_child。

### 案例D：暂停菜单（ScreenStack + 模态输入）

对应 UMG 的 AddToViewport(ZOrder) + SetGamePaused，Godot 的 CanvasLayer。

```cpp
// 栈：Content 层(autoMount entries) 之上有 ScreenStack，zOrder 分层
void GameApp::onPauseKeyPressed() {
    _stack.push(100, PauseMenuScreen{_game});   // push: HUD.onBlur()，Pause.onMounted()
    _game.setPaused(true);
}

class PauseMenuScreen final : public UIScreen {
    EInputBlocking getInputBlocking() const override { return EInputBlocking::Modal; } // 吃掉全部输入

    UIDescription render() override {
        return ui::panel("pause-dim")                    // 全屏半透明遮罩
            .setSize(_state.viewportSize).setColor({0, 0, 0, 0.6f})
            .child(ui::column("pause-col").setSpacing(10)
                .child(ui::text("paused").setText("PAUSED").setFontSize(36))
                .child(ui::button("resume").setText("Resume")
                    .onClick([this] { _stack.pop(); }))  // pop: unmount + HUD.onFocus()
                .child(ui::button("quit-menu").setText("Quit to Menu")
                    .onClick([this] { _game.backToMainMenu(); })));
    }
};
```

输入路由：`host.dispatchEvent` 先问栈顶 `getInputBlocking()`；Modal 返回 Exclusive（WASD 不再移动角色），Passthrough 允许穿透（UMG 的 bShouldShowCursor / Input Mode 对应物）。

### 案例E：document 骨架 + DSL slot（混合，队伍栏）

UMG 的 NamedSlot：策划用 UI Designer 排静态骨架（inline document），程序只填动态槽位。

```cpp
// 编辑器 authored 的 inline document：
//   engine.panel "party-frame"      ← 静态外观/布局（UI Designer 可视化编辑）
//     engine.container "slots"      ← 命名为 slot 的容器
//       engine.text "placeholder" ("Drop party widgets here")

class PartyHudMount final : public IGameUIMount {   // 需新增：entry 的代码挂载钩子
    void onEntryMounted(SceneWidgetEntry& entry, UIElementRef root) override {
        if (UIElement* slots = root->findChildByDisplayName("slots")) {
            _ctrl = std::make_unique<UIRenderController>(_host.getTree());
            _ctrl->attachTo(*slots);               // 需新增：reconciler 挂到 retained 子树
            _ctrl->setRenderFunction([this] { return renderParty(); });
        }
    }

    UIDescription renderParty() {
        auto row = ui::row("party");
        for (const PartyMember& m : _party.members()) {
            row.child(ui::column(m.guid)
                .child(ui::panel("hp").setSize({60 * m.hp01(), 6}).setColor(hpColor(m.hp01())))
                .child(ui::text("name").setText(m.shortName()).setFontSize(12)));
        }
        return row;
    }
};
```

### 一帧的完整时序

```
1. App::update(dt)
   ├─ ECS systems 写 _hudState（掉血/换弹/拾取）
   ├─ 事件回调: screen->invalidate()
   └─ host.flushDirty()               ← 唯一 flush 点（update 末尾）
        ├─ for dirty screen: desc = render()
        └─ reconciler.apply(desc)     （same-key changed-only diff）
2. 输入: host.dispatchEvent(...)      → ScreenStack 栈顶决定 modal/passthrough
3. host.buildSnapshot()               → layout + paint，immutable snapshot
4. Render: compose(snapshot)          （command recording 只读 snapshot）
```

### UIScreen / ScreenStack API（需新增）

```cpp
class UIScreen {                       // UMG UserWidget / Godot Control 根 的对应物
public:
    virtual ~UIScreen() = default;
    virtual UIDescription render() = 0;
    // 生命周期：UMG NativeConstruct/Destruct；Godot _ready/_exit_tree
    virtual void onMounted();          // 首次 reconcile 前
    virtual void onUnmounted();        // 从树移除后（释放 texture lease）
    virtual void onFocus();            // 成为栈顶（获得输入焦点）
    virtual void onBlur();             // 不再是栈顶
    virtual int  getZOrder() const;
    virtual EInputBlocking getInputBlocking() const;   // None/Passthrough/Modal
    void invalidate();                 // 置脏，下一次 flush 重渲染
};

class ScreenStack {
public:
    template<class TScreen, class... TArgs>
    std::shared_ptr<TScreen> push(int zOrder, TArgs&&... args);
    void pop();                        // unmount 栈顶，恢复下层焦点
    void popToRoot();
    [[nodiscard]] UIScreen* top() const;
};
```

### 由案例倒推的缺口清单

| 缺口 | 案例 | 现状 |
|---|---|---|
| `UIScreen` + `ScreenStack`（生命周期/焦点/模态/flush 点） | A/B/D | 无，`UIRenderController` 是裸轮子 |
| ProgressBar（血条/读条） | A/E | 无，过渡期 Panel 填色 |
| `ui::comboBox/image/checkBox/slider/list` builder | B | 控件已有，DSL 层 typed 包装未接 |
| viewport 尺寸/anchor 注入 render（全屏遮罩） | D | 仅 setSize 硬编码 |
| slot 挂载（reconciler attach retained 子树 + `IGameUIMount`） | E | 无 |
| gamepad/键盘焦点链导航（DirPad 上下左右） | B/D | 只有 focusPolicy，无导航 |
| keyed children 稳定性（guid 而非下标）文档 + 断言 | C | reconciler 已支持，无诊断 |

### 增量接入步骤（G1-G5）

- G1 类型统一：UIDescription 内部契约改 typeId；builtin kinds 注册进 UITypeRegistry；adapter create/apply 走 registry + 反射字段。门禁：同 description 重复应用不重建；未知 typeId 有诊断；DeclarativeContractTest 全绿。
- G2 Host 接入：`UIScreen`/`ScreenStack`/`flushDirty` 进 GameUIHost 帧循环（案例A/D 落地）。门禁：场景切换挂卸干净、teardown 无残留、flush 严格在 snapshot 前、Modal/Passthrough 输入路由有测试。
- G3 状态保留验证：状态驱动重建下 focus/caret/scroll/selection 保留（复用 Phase -1C 验收；案例C keyed 列表）。
- G4 混合 slot：inline document slot 节点 + DSL reconcile 子树（案例E）。门禁：slot 内容与静态兄弟节点 zOrder/事件互不干扰。
- G5 Editor 狗粮：UIDesignerPanel preview 改 reconciler 驱动，为 editor 迁移铺路。

### 边界（不做）

- 不做 XML/CSS 外部语法；暂不做独立 ui document 文件格式。
- 不做自动双向绑定；render function 不在 command recording 期执行。
- 事件 lambda 不捕获裸 live widget 指针（只捕获 model/host 句柄）。
- DSL 节点不做长期持有的对象图：description 每帧构建、用后即弃，常驻状态只存在于 model 与 retained widget。

## 稳定交付门禁

每个后续 Phase 都必须通过以下门禁后才能进入下一阶段：

1. Contract gate：API、所有权、invalidation、序列化语义有文档和单测。
2. Behavior gate：mount/update/remove/reorder、focus/capture/popup/drag 行为有自动化覆盖。
3. Render gate：snapshot JSON/digest、windowed GPU shot、offscreen parity 通过。
4. Resource gate：纹理、字体、brush、descriptor 保活到 submit 完成，teardown 无残留。
5. Compatibility gate：旧 retain API 和未迁移 host 继续通过原有测试。
6. Performance gate：记录 reconcile widgets、rebuilt widgets、draw items、paint/layout 时间；没有未经解释的回归。

任何门禁失败时，停止扩展功能，先修复当前 Phase；不允许带着已知生命周期或 cache 问题进入下一阶段。

## 推荐的完整迭代顺序

1. Phase -1A：identity/lifecycle/data-flow 最小契约。
2. Phase -1B：Panel/Text/Button + Row/Column 的最小 reconciler。
3. Phase -1C：TextField/ScrollViewport 的状态保留。
4. Phase -1D：只读 props、事件写回、Reactive dirty、batch update。
5. G1：DSL 类型收口（typeId + 反射字段统一）。
6. G2/G3：GameUIHost screen 接入与状态保留验证。
7. Phase 0：基线矩阵、无 theme/authored/theme-only 测试和性能基线。
8. G4：document 骨架 + DSL slot 混合。
9. G5 + Phase 1：UIDesignerPanel preview reconciler 化；visual property setter/invalidation 收口（Editor 狗粮线与属性收口合并推进）。
10. Phase 2：Brush/NinePatch/Border/rounded primitives。
11. Phase 3：基础控件 visual state 完备。
12. Phase 5：Editor core controls 和 EditorTheme（editor-grade dense controls、property editing、validation）。
13. Phase 6：按 panel 逐步移除 ImGui，三宿主收敛。

每个编号都应作为可独立 review、测试、提交和回滚的增量，不把多个编号合并成一次“大重构”。

## Phase 0：基线、契约与迁移护栏

- 建立 standalone、Game Runtime、headless snapshot 回归矩阵。
- 增加无 theme、authored properties、theme-only 三种模式测试。
- 定义 visual property 的 Paint/Layout/SubtreePaintContext 影响级别。
- 定义 common visual states 和 inline document authored appearance 版本策略。
- 建立 scenario、snapshot JSON、GPU parity 验收。

验收：无 theme 仍能稳定生成 snapshot；直接属性变更不会绕过 paint cache。

## Phase 1：Visual property 与 invalidation

- 为颜色、brush、font size、padding、border、尺寸提供 changed-only setter/getter。
- runtime 禁止依赖裸字段写入；reflection/deserialization 走事务式写入。
- 统一 UIElement visual mutation 辅助路径。
- 迁移 Panel、Image、Text、Button、CheckBox、Slider、TextField、ComboBox、SelectableRow。

验收：改颜色/brush 下一帧可见；相同值无多余 dirty transition；layout-affine 修改不扩大为全树重绘。

## Phase 2：Brush 与基础视觉原语

- 实现真正 NinePatch 和 Border；支持 UV/crop/flip 与资源丢失 fallback。
- 增加 rounded rectangle、border thickness、opacity/alpha 合成规则。
- 评估 gradient、shadow、inner shadow 是否进入 framework primitive。
- 验证资源保活到 queue submit 完成。

验收：button、panel、dialog、tooltip、window 只用 brush 即可正确绘制；不同尺寸和 DPI 下九宫格角点不变形；windowed/offscreen parity 一致。

## Phase 3：统一控件 visual state

统一 normal、hovered、pressed、focused、disabled、selected、checked、dragging、read-only、validation/error 状态。

- 为 CheckBox、ComboBox、Slider、TextField、TreeView、TableGrid、Menu、Dialog、Popup、SelectableRow 补齐 appearance 数据。
- 分离行为状态和视觉状态；状态变化统一通过 VisualFlag 或 invalidation。
- 定义 disabled ancestor 绘制策略。

验收：Editor 和 Game UI 使用同一控件 API 配置状态外观；每个状态有 scenario 和 snapshot 验收。

## Phase 4：Game UI authored appearance 与资源边界（原 yaui 步骤，已收口为 inline document）

- 区分 geometry、behavior、authored appearance 字段。
- 将必要 brush、颜色、font、可选 style role 纳入稳定 schema（inline document，无独立文件格式）。
- 统一纹理、字体 resolver 和资源缺失 fallback。
- 增加 document version migration；theme role 不作为文档有效性的前置条件。

验收：Editor 编辑的 inline document 在 Runtime 中外观一致；无 theme 也可实例化；资源缺失有稳定诊断。

## Phase 5：Editor 控件与迁移准备

- 完善 DockSpace、Tab、FloatingWindow、Menu、Popup、TreeView、TableGrid、List、Property row、Search/Filter。
- 增加 enum、numeric、vector、color、asset picker、reference picker。
- 增加 read-only、mixed value、modified、validation error、tooltip 状态。
- 完善 keyboard navigation、focus scope、shortcut routing、selection model、undo/redo 边界。
- 设计 EditorTheme，但只作为 Editor layer 内容。

验收：核心 Editor shell 不依赖 ImGui；WidgetTree 能承载 editor panel、dock、inspector、viewport overlay。

## Phase 6：ImGui 移除与三宿主收敛

- 逐 panel 迁移 Editor。
- 移除 ImGui event/style/render backend 依赖。
- Editor 使用 EditorTheme，Game Runtime 使用 project-authored appearance，standalone app 可使用 UITheme/Workbench。
- 清理旧 GUI、旧兼容路径和重复控件实现。

验收：Editor 无 ImGui 编译/运行时依赖；三种宿主均使用同一套 WidgetTree/Render2D compose；均通过 headless、snapshot、GPU smoke 和 teardown 验收。

## 验收矩阵与提交策略

每阶段覆盖 standalone windowed、offscreen/headless、Game Runtime viewport、Editor embedded/offscreen、theme mounted、no theme、direct authored color/brush、runtime state transition、resize/DPI、资源缺失、detach/reparent/reload、GPU teardown。

推荐入口：python3 Script/ya.py cfg；python3 Script/ya.py test --target ya --filter Suite.Test；xmake b/r ya-gui-closure-test；xmake b/r GUIWorkbench。真实渲染必须执行 GPU shot，不能只依赖 tree assertion。

每个 Phase 单独提交，格式为 [gui] phase N: description。Phase 内顺序为 contract/tests、framework implementation、control migrations、documentation/baseline。

## 风险

- NinePatch 会改变 draw item 数量和 baseline digest。
- setter 收口会暴露业务层直接写字段的隐式依赖。
- inline document authored appearance schema 变更需要 scene migration（无独立文件格式后migration 跟随 scene 版本）。
- DSL typeId 收口会动 EWidgetKind 的所有契约测试，需保持 DeclarativeContractTest 行为等价迁移。
- Editor 迁移同时涉及 input、focus、undo/redo 和 resource picker。
- theme 与 authored appearance 优先级必须先固定，否则控件行为会不一致。
