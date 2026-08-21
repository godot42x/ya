# GUI Style System 收敛计划：从硬编码控件色到 Theme/Style 上下文

> 建立日期：2026-08-21
> 状态：活跃计划
> 目标：把当前 GUIWorkbench / Dock / Menu / Tab / FloatingWindow 的硬编码视觉参数，收敛为一套可复用、可切换、可逐步扩展到 Editor 与 Game UI 的 style system。

## 0. 结论摘要

当前仓库并不是完全没有样式机制，而是只有一层很薄的原语：

- UIStyleSet
- Reactive<FWidgetStyle>
- UIText::bindStyle()

这套原语可以证明样式变化触发依赖控件重绘的方向成立，但还远远不是一个可支撑 retain-mode GUI shell 的 style system。当前主要问题不是颜色不好看，而是：

1. 大量控件仍直接持有 _normalColor/_hoveredColor/_selectedColor/... 之类字段；
2. Workbench / Dock / Floating / MenuBar / TabBar 没有统一 token 来源；
3. 没有 tree/window/subtree 级 theme context；
4. 没有统一的 style resolve 链；
5. 没有 typed widget styles，FWidgetStyle 无法承载真实 GUI shell；
6. 没有明确区分 framework 负责样式机制和 app 负责主题内容。

本计划的正式判断：

- GUI framework 必须内建 style system；
- app / editor / game 提供各自 theme 内容；
- 第一阶段不做 CSS/QSS/DSL 解释器；
- 第一阶段先建立 typed style + theme context + resolve 规则，让控件退出硬编码。

一句话主线：

Framework/GUI 提供样式运行时；GUIWorkbench / Editor / Game 只提供主题资产与映射。

## 1. 先回答边界问题

### 1.1 style system 属于 framework 还是 app？

机制属于 framework：style 类型系统、theme context、resolve 链、状态态映射、subtree/window 级覆盖。

主题内容属于 app：Workbench dark theme、Runtime editor theme、Game default HUD theme、未来的 light/high-contrast/theme pack。

因此 UITheme / UIThemeContext / typed style structs / resolve rule 属于 Framework/GUI；WorkbenchTheme / EditorTheme / GameTheme 属于应用层或示例层。

### 1.2 game UI 需不需要 style system？

需要，但诉求比 tool GUI 轻。Tool GUI 需要 menubar/tab/dock/floating/inspector/popup 的统一 shell 风格、状态态系统化表现、多窗口/subtree/popup 的 theme 继承与覆盖。Game UI 也需要统一字体字号文本色、menu/HUD/dialog/button 一致性、平台/模式/skin/mod 切换。

所以不是 GUI 需要而 Game UI 不需要，而是机制共用、主题复杂度不同、style key 空间不同。

### 1.3 StyleSet 放在应用层可以吗？

不行，至少不能只放应用层。如果 style set 只存在于 app，framework 控件无法统一 resolve style，subtree/window override 无法成为正式能力，editor/game/workbench 各自会平行发展第二套样式注入约定。

正确做法是 framework 提供统一 style runtime，app 只往 runtime 填具体 theme 数据。

## 2. 当前现状审计

### 2.1 已有基础

- GUI/Widgets/Style.h：UIStyleSet / FWidgetStyle / Reactive<FWidgetStyle>
- UIText 已经支持绑定 style，并在 paint/layout 时读取绑定值

这说明 reactive 驱动的样式失效链可行，样式变化 -> 控件重绘 方向已有最低证据。

### 2.2 主要缺口

1. FWidgetStyle 过于通用，只有 fillColor/textColor/fontSize/padding，只够 Text 或极简单按钮，无法表达 Dock/Floating/MenuBar/Tab 的真实需要；
2. 样式入口不统一，Button/TabBar/MenuBar/Dock/Floating 各自持有私有颜色字段，app 只能构造后挨个覆盖；
3. 缺少 owner 级 theme context，WidgetTree/window/subtree 不能自然切 theme；
4. 缺少状态态模型，normal/hovered/pressed/selected/active/focused/disabled 没有统一 resolve；
5. Workbench 的样式收口现在仍是临时 token header，属于阶段性整理手段，不是终局机制。

## 3. 终局对象模型

### 3.1 三层模型

A. design token 层：表达主题原料，不直接给控件使用。颜色如 color.bg.window/color.bg.panel/color.border.subtle/color.text.primary/color.text.muted/color.accent；尺寸间距如 spacing.xs/sm/md/lg、radius.sm/md、border.thin/normal；字体如 font.body/font.mono/font.size.sm/md/lg。第一阶段可先用 C++ struct 承载。

B. typed widget style 层：第一阶段重点。每类控件有自己的 style struct：FTextStyle、FPanelStyle、FButtonStyle、FTabStyle、FMenuBarItemStyle、FSplitPaneStyle、FScrollBarStyle、FDockSpaceStyle、FFloatingWindowStyle。不允许继续共用 FWidgetStyle 假装通用；不允许新的 shell 控件继续直接暴露一整套颜色字段；控件只读取与自己有关的 style 属性。

> 关键升级（2026-08-21 蓝图调研，推翻原「第一阶段不做 image brush」决策）：typed style 的视觉字段应从 `glm::vec4` 纯色升级为 **brush 引用**（见 §3.5）。brush 把「画什么资源」与「叠什么颜色」统一进一个类型，纯色 fill 是 brush 的退化形态（无 texture + tint）。这是「一套 GUI 服务 tool + game」的根基——tool GUI 用纯色退化 brush，game UI 用 image/九宫格完整 brush，机制同一、内容不同。

> ⚠ Phase 1 落地遗漏（评审发现）：Style.h 实际只落了 7 个 struct，**FSplitPaneStyle 与 FScrollBarStyle 未落地**，SplitPane 的 divider 色被错误塞进 `FDockSpaceStyle::splitDividerColor`。UISplitPane 是通用控件（非 dock 场景也在用），不应从 dock style 取色；且 SplitPane 有三个 divider 状态（normal/hovered/dragging）被压成单一静态色（状态丢失）。Phase 2 动工前必须补 `FSplitPaneStyle`（dividerFill/dividerHoveredFill/dividerDraggingFill 三态），从 FDockSpaceStyle 移除 splitDividerColor。

C. theme context / style lookup 层：UITheme 持有 token 与 named typed styles；UIThemeContext 挂在 WidgetTree/GUIWindowHost/subtree override 上；UIStyleKey 是命名查找键，如 button.toolbar、tab.workbench、dock.floating。

### 3.2 resolve 链

统一顺序：widget explicit override -> widget style key lookup -> subtree theme override -> tree/window theme default -> framework fallback。

禁止 resolve 不到就临时写 magic color，禁止应用层通过遍历 children 二次覆写控件字段作为主要主题方式。

#### 3.2.1 resolve 上游换人的失效传播（评审 blocker，必须设计）

resolve 链是「查询时解引用」模式，但引擎现有 Reactive 失效是「值变驱动」——`Reactive<T>::set()` 只在值变时 notifyDependents（`Reactive.h:140` 有 `== no-op` 短路），它**覆盖不了「resolve 上游换人」**：切换 UITheme / subtree override 时没有任何已绑定的 Reactive 值发生 set，旧像素不会重画。

因此 UIThemeContext 必须自带依赖载体，二选一：

1. **UIThemeContext 持有 `Reactive<UITheme>`**（或等价 generation token）：控件 paint 时 `get()` 建立依赖，换 theme 时 set() 新值 → 依赖控件整树 Paint dirty。与现有 Reactive 失效链同构，优先推荐。
2. **换 theme 走 `invalidateSubtreePaint()`**：不依赖 Reactive 依赖图，直接对整棵子树标 paint dirty。实现简单，但与 Reactive 失效是两条并行的失效路径，需保证不遗漏。

无论选哪种，white/dark 切换（tree/window 级换 theme）必须是 Phase 2 的**显式端到端验收项**：同一树运行时换 UITheme → 全 shell 重绘、无漏标脏。

#### 3.2.2 UIStyleSet 的最终命运

现有 `UIStyleSet` 写死 `unordered_map<string, Reactive<FWidgetStyle>>`（`Style.h:193`），无法承载 typed styles。决定：**泛型化为 `define<TStyle>(name, style)`**，复用 `Reactive<T>` 已泛型的能力（`Reactive.h:114`），typed styles 与 FWidgetStyle 都走同一容器。Phase 2 的 UITheme 作为 typed-style 的**命名查找层**（style key → named style），底层复用泛型化的 UIStyleSet，不另起第二套容器。`FWidgetStyle` 与 typed styles 的并存是过渡态，退出路径是：Phase 3 完成后 UIText 也迁到 FTextStyle，FWidgetStyle 保留为「最简文本/单色样式」的兼容别名或直接移除。

### 3.3 状态态模型

状态是**全局词汇表**（normal/hovered/pressed/selected/focused/active/disabled，参照 Slate EWidgetState 或 Qt State 位掩码），但**每类 typed style 只声明它真正消费的状态子集**——FButtonStyle 5 态、FTabStyle 3 态、FMenuBarItemStyle 2 态是正确路线，不要按「七态统一」硬凑。widget 只维护当前状态，style system 决定状态对应的绘制属性，业务逻辑不再把 hovered=某颜色写死在 paint 里。

### 3.4 design token → typed style 的转换（评审 major，须明确）

token 是「原料」、typed style 是「成品」，但两者之间必须有明确的派生关系，否则 token 层形同虚设、`修改一个 token 全局生效`（Phase 4 承诺）不成立。

决定：**配置代码烘焙**——app 构造 WorkbenchTheme/EditorTheme 时，用 token 常量初始化 typed style 实例（构造期/编译期），framework **不做运行时 token 求值**。token 层是「命名常量原料层」（C++ struct 承载），typed style 是「由 token 派生出的、控件直接消费的成品」。这条转换发生在 app 层（theme 构造），不发生在 framework resolve 链内。

理由：运行时 token 求值会引入一套新的求值引擎（无谓复杂度），而 retain-mode GUI 的 theme 在 app 启动时确定、运行期只做「切换预构造好的 theme 实例」，编译期烘焙完全够用，且与 §3.2.1 的「Reactive\<UITheme\> 切换」天然契合。

### 3.5 brush 抽象：color 与 image 的统一（第一阶段必须，game UI 复用前提）

**背景**：调研确认（UE FSlateBrush / Godot StyleBox 三方一致），让一套 GUI 同时服务 tool GUI 和 game UI 的根基，不是模块摆放也不是 resolve 链，而是 **brush 抽象**——把纯色与贴图统一进一个类型。

- **UE**：`FSlateBrush` = `TintColor`（着色）+ `ResourceObject`（纹理/材质）+ `DrawType`（Image/Box/Border/RoundedBox）+ `Margin`（九宫格）。纯色按钮 = ResourceObject 为空 + TintColor 上色的退化 brush。
- **Godot**：`StyleBox` 多态——`StyleBoxFlat`（纯色/圆角）是特例，`StyleBoxTexture`（九宫格）是通用；同一控件在 editor 主题拿 Flat、game 主题拿 Texture，换外观只换资源不换控件。

**结论**：typed style 的视觉字段应从 `glm::vec4` 升级为 **brush 引用**（`FBrush`），brush 含：
- `tintColor`（着色，纯色 brush = 无 resource + tint 上色）
- `resource`（可选纹理/材质，game UI 的 hover image / 背景图）
- `drawType`（Image / NinePatch / Border，九宫格 = NinePatch + `margin`）
- `margin`（九宫格四边距，patch 数据属 app 内容，九宫格渲染算法属 framework 能力）

**tool GUI vs game UI：差异在内容，不在机制**：

| 维度 | tool GUI（editor/workbench） | game UI（HUD/menu） |
|---|---|---|
| 视觉形态 | 纯色 fill（brush 退化） | image / 九宫格（brush 完整） |
| 状态态 | 全态（normal/hover/pressed/focused/disabled） | 常只 normal/hover/pressed |
| 主题切换 | white/dark 频繁运行时切换 | 通常固定，换肤=换整套资产 |
| 按钮多样性 | 同型 | Primary/Danger 等**业务角色变体**（Godot `theme_type_variation`） |
| 动画 | 少 | hover 渐变、按下缩放（常见） |

机制相同（typed style + 状态集 + theme context + resolve 链），差异只在内容（值是纯色还是贴图、状态集全不全）。

**抽象边界（style 管什么、不管什么，避免过度设计）**：

```
style 系统管：
  ├─ 静态的、按状态离散的视觉（每状态一个 brush/色）
  ├─ 九宫格 = brush 的 drawType + margin 字段  ← 进 brush 类型
  └─ 字体引用 token（family/size/weight）

style 系统不管（交给别的子系统）：
  ├─ 动画/过渡 = 独立 tween/动画系统（style 管「哪个状态长什么样」，动画管「状态间怎么过渡」）
  ├─ 字体资产/emoji atlas/本地化 = 资源 + text 子系统
  ├─ 图标 atlas 打包 = 资源系统（图标本身 = image brush 实例）
  └─ DPI/分辨率断点 = layout/metric 系统（style 只贡献「可缩放 token 单位」）
```

**分阶段边界**：第一阶段落 brush（含 drawType + margin 九宫格字段，成本低，但决定 typed style 字段类型，越晚改破坏越大）；动画 tween、完整皮肤资源管线、字体 atlas 打包、DPI 断点系统推迟到第二阶段。Godot 的换 theme 机制（`NOTIFICATION_THEME_CHANGED` 树级通知 + 查询时解引用）印证 §3.2.1 的 B1 解法。

## 4. 最小闭环设计

第一阶段只做 retain-mode GUI shell 所需的最小闭环，不做大而全主题系统。

第一批接入控件优先级：UIText -> UIPanel -> UIButton -> UIMenuBarItem/UIMenuBar -> UITabButton/UITabBar -> UISplitPane -> UIDockSpace -> UIDockFloatingWindow。这批就是 Workbench/Dock/Floating 最明显的 shell 构成，接入后 Workbench 外观能从硬编码转成主题驱动。

第一个主题宿主：GUIWorkbench。它是 feature gallery + regression app，能覆盖 menu/tab/dock/floating/editor shell，可作为 editor 主题系统前置验证场。

第一阶段明确不做：CSS/QSS 解析器、selector engine、XML/DSL style file、editor/game 双主题同时落地、radii/shadow 全量体系、运行时样式编辑器。

> ⚠ 修订（2026-08-21 蓝图调研）：原决策「第一阶段不做图像 brush/9-patch」已推翻。**brush 抽象（含 drawType + margin 九宫格字段）提前到第一阶段必须**——它是 game UI 复用 style 系统的前提，且决定 typed style 的字段类型（`glm::vec4` → brush 引用），越晚改破坏越大。纯色 fill 是 brush 的退化形态。radii/shadow 仍推迟到第二阶段。

## 5. 分阶段落地顺序

## Phase 0 - 审计与主入口清理

审计所有核心 shell 控件现有颜色/边距字段，列出哪些已有 style binding 哪些没有，把 Workbench 当前临时 token header 标记为过渡输入。完成标准：有 style capability matrix 和控件 -> typed style 映射表。

## Phase 1 - typed style structs 与 framework fallback

新增第一批 typed style structs，每种 style 定义 framework fallback 默认值，定义状态态结构或命名约定。完成标准：Button/Tab/MenuBarItem/Panel/FloatingWindow 都存在自己的 style struct，新控件不再以加颜色字段为默认扩展方式。

> 遗留待补（评审发现）：FSplitPaneStyle 与 FScrollBarStyle 未落地，需在本阶段或 Phase 2 前补齐（见 §3.1 标注）。另：typed style 的 `operator==` 必须用反射 `visit_fields` 生成（`reflectEqual<T>`）或加 YA_REFLECT 自动生成，禁止手写——手写 `==` 在字段增删时漏改会触发 `Reactive::set()` 的 `== no-op` 短路，造成静默漏标脏。

## Phase 2 - Theme / ThemeContext / resolve 链

新增 UITheme、UIThemeContext，WidgetTree 或等价 owner 能设置默认 theme，控件支持 style key/explicit override/fallback resolve。完成标准：同一控件在不同 tree/window 下可 resolve 不同样式，不需要 app 遍历 children 手动改色。

本阶段必须同时落地三件事（评审 blocker/major）：

1. **UIStyleSet 泛型化**（§3.2.2）：`define<TStyle>(name, style)`，typed styles 走同一容器，不另起第二套。
2. **resolve 上游换人失效传播**（§3.2.1）：UIThemeContext 持 `Reactive<UITheme>` 或换 theme 走 `invalidateSubtreePaint()`。
3. **white/dark 切换端到端验收**：同一树运行时换 UITheme → 全 shell 重绘、无漏标脏。这是本阶段完成标准的硬性条款，不只是「可 resolve 不同样式」。

### Phase 2 详细设计（2026-08-21 评审后修订）

#### 1. UIStyleSet 泛型化（B3）

类型擦除容器，`ReactiveBase`（Reactive.h:70）作基类：

```cpp
class UIStyleSet {
    template <typename TStyle>
    std::shared_ptr<Reactive<TStyle>> define(std::string name, TStyle style);
    template <typename TStyle>
    std::shared_ptr<Reactive<TStyle>> find(const std::string& name) const;
private:
    std::unordered_map<std::type_index,
                       std::unordered_map<std::string, std::shared_ptr<ReactiveBase>>> _styles;
};
```

**G4 同名 set 语义（评审 major，必须保留）**：`define<TStyle>` 命中已存在的**同型** handle 时 `set()` 复用（不新建 ReactiveBase），否则替换 handle 会 orphan 旧依赖（控件的 `_paintDependencies` 存裸 `ReactiveBase*`，替换后收不到通知 → 静默漏标脏）。同 key 不同 type 走 type_index 分桶共存，语义显式化。

#### 2. UITheme

组合泛型 UIStyleSet：`define<TStyle>(key, style)` / `find<TStyle>(key)` 委托。

#### 3. WidgetTree 挂载 + B1 失效传播

WidgetTree 新增：

```cpp
UITheme* _theme = nullptr;
std::shared_ptr<Reactive<uint64_t>> _themeGeneration = std::make_shared<Reactive<uint64_t>>(0);
void setTheme(UITheme* t) { _theme = t; _themeGeneration->set(_themeGeneration->value() + 1); }
```

**用 `Reactive<uint64_t>` generation token 而非 `Reactive<UITheme>`**：换 theme 时 generation +1 触发依赖控件重绘，`==` 是 O(1)（避免 UITheme 递归 == 复杂度），比 `invalidateSubtree` 精确（只重绘依赖控件）。

#### 4. resolve 链 = 框架 helper（评审 major，机制化依赖登记）

```cpp
template <typename TStyle>
const TStyle* resolveThemeStyle(const UIElement& w, const std::string& key, EDirtyLevel level) {
    if (WidgetTree* tree = w.getTree(); tree && tree->getTheme()) {
        tree->getThemeGeneration()->get(level);   // 无条件登记，不可被 early-return 绕过
        if (auto style = tree->getTheme()->find<TStyle>(key)) {
            return &style->get(level);
        }
    }
    return nullptr;  // 未找到 → 控件用 framework fallback（默认构造 TStyle）
}
```

依赖登记收口为 helper（控件无法绕过），resolve 不到返回 nullptr → 控件用默认构造的 TStyle 作 fallback。

#### 5. Layout 粒度（评审 major，禁止一刀切 Paint）

typed style 含布局成员（`padding`/`fontSize`/`minSize`/`width`），改值须重跑 layout。`resolveThemeStyle` 的 `level` 参数由控件按需传：**布局亲和成员传 `Layout`，纯颜色/brush 成员传 `Paint`**（沿用 UIText 的 `_bAutoSize` 判据，Text.cpp:17-18）。generation 依赖的粒度同理。

#### 6. 第一刀范围（评审 blocker 收口）

**第一刀不做 subtree override**（resolve 链暂为 `style key (tree theme) → framework fallback`）。subtree override 留后续：届时 override 字段做成带 generation 的 Reactive（set 时 +1，控件 get 该 token 建依赖），或写入走 setter + `invalidateSubtree`。**禁止 raw `UITheme*` 字段无失效边**——那是 B1 在 override 层的复发。

#### 7. 统一绑定路径（评审 major）

废弃 `UIStyleSet::bindTo` 的 persistent 注册（与 `UIText::bindStyle` 的 paint 时 get 语义重复，且 persistent 有被 clearDependencies 冲掉的隐患），统一走 `resolveThemeStyle` 的 paint 时 `get(level)`。Gallery 的 bindTo/bindStyle 消费点同步迁移。

## Phase 3 - 第一批控件去硬编码

UIText/UIButton/UIMenuBarItem/UIMenuBar/UITabBar/UITabButton/UISplitPane/UIDockFloatingWindow/UIDockSpace 从对应 typed style resolve。完成标准：Workbench 最外层 shell 不再遍历 child 二次覆写，Dock/Floating/Tab/Menu 视觉常量不再散落 cpp。

**resolve 读取路径契约（评审 major，必须写死）**：paint 属性必须在 `paintSelf` 内走 `Reactive<T>::get()`（每帧 resolve + 依赖重收集），**禁止缓存 resolved style 进成员字段**——缓存会脱离 Reactive 依赖图，重演 layout→paint 漏标脏（本引擎最高频 bug）。对 resolve 上游不稳的控件用 `_bVolatile` 兜底。

## Phase 4 - GUIWorkbench 主题接入

新增 WorkbenchTheme 或等价实例，FWorkbenchSurface/demo pages 通过 theme key 组织样式，Dock/Editor/gallery 共用同一主题。完成标准：修改一个 token 或一组 named style 可全局改变 Workbench 壳层外观。

## Phase 5 - Game / Editor 扩展预留

定义 game/editor theme key 命名约定，明确哪些 typed style 需扩展到 HUD/menu/dialog，明确多窗口 theme context owner 语义。完成标准：style runtime 可被 editor/game 复用，不需要重构 core。

## 6. 目录与 owner 建议

- Framework/GUI/Runtime/Widgets/Style.* 继续保留基础 reactive/style 能力；
- Framework/GUI/Runtime/Widgets/Theme.* 放 UITheme/UIThemeContext/typed styles；
- Framework/GUI/Tooling/Workbench/... 放 WorkbenchTheme 等 app/theme 内容。

硬约束：framework public include 只放通用 style runtime；WorkbenchTheme 不反向污染 runtime/widgets core；不允许把 editor/game 具体命名塞进 framework typed style 层。

## 7. 验证计划

编译：xmake b GUIWorkbench、xmake b ya-gui-widgets、xmake b ya-gui-tooling。

行为：menu hover-switch、tab select、dock tear-off/redock、floating resize scenario 仍通过。

视觉：产出 Workbench 全窗口、Dock 页、Editor 页截图基线，检查 menu/tab/floating/dock/status bar 是否共享一致层级，hover/selected/active/focused 是否一致。

架构：新增 shell 控件不得继续默认扩展 _normalColor/_hoveredColor 等；Workbench 不以遍历 children 覆写控件字段作为主要主题机制；Theme/Style runtime 不直接依赖 Workbench/Game/Editor 语义。

**theme 切换（white/dark）端到端验收**：Workbench 运行时在两个 UITheme 实例（dark/light）间切换，配合既有验收体系（scenario + dump_tree 断言解析后 fill/textColor 翻转、assert_validation_clean 验证整树重绘无漏标脏）。这是用户明确要的验收场景，机制做完必须有人验，缺失即视为 Phase 4 未完成。

## 8. 当前默认决策

1. 第一阶段不做 CSS/QSS；2. 采用可编程 theme + typed styles + style keys；3. style runtime 属于 framework；4. theme 内容属于 app；5. GUIWorkbench 是第一接入宿主；6. game UI 复用同一 runtime 但主题复杂度更低；7. 临时 WorkbenchStyle 只是过渡输入；8. **brush 抽象（color+image 统一，含 drawType+margin 九宫格字段）第一阶段必须落**，纯色是 brush 退化形态，是 game UI 复用 style 系统的前提。

## 9. 退出条件

Workbench shell 主要控件都从 theme/style resolve 而非硬编码；tree/window/subtree 级 theme context 已存在；typed style 已替代核心 shell 控件裸颜色字段扩张趋势；app 与 framework 职责边界清楚；下一步扩 editor/game 无需重写 style core。

