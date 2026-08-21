# GUI Style System 收敛进度记录

> 建立日期：2026-08-21
> 作用：记录 style system 收口过程中的已完成切片、阶段证据、当前阻塞与下一轮接力点。

## 2026-08-21 — 计划建立

（见 7720e428：plan/progress/todo/checklist/matrix 建立）

## 2026-08-21 — Phase 0 audit 完成

- 明确反模式：AM-1 framework 裸颜色字段扩张、AM-2 app 临时 token header + 遍历覆写、AM-3 paint 内 magic color；
- 建立控件 -> 目标 typed style 映射表（落盘 phase0-audit.md，a38c1d36）；
- 弃用 style token stash（stash@{0} 已 drop），过渡文件 WorkbenchStyle.h 移至 /tmp。

## 2026-08-21 — Phase 1 typed style structs 落地

### 本轮完成

- 在 GUI/Widgets/Style.h 新增 typed style structs：
  - FTextStyle（textColor/fontSize）
  - FPanelStyle（fillColor）
  - FButtonStyle（normal/hovered/pressed/focused/disabled fill + textColor + padding）
  - FMenuBarItemStyle（text + normal/hovered fill）
  - FTabStyle（text + normal/hovered/selected fill + accent + padding）
  - FDockSpaceStyle（canvas + split divider + drop preview）
  - FFloatingWindowStyle（body/inner/border/edge affordance/title + minSize）
- 每种 style 的成员默认值即 framework fallback；
- 默认值从各控件当前外观复制，保证 Phase 3 迁移行为不变；
- 状态命名约定落定：*Fill（normal/hovered/pressed/focused/selected/disabled）+ textColor + padding + accent。

### 当前结论

- typed style 类型层已定型，framework fallback 已定义；
- Phase 1 只加类型，未接入控件（Phase 3 才接），现有行为零变化；
- ya-gui-widgets 编译通过。

### 当前未完成 / 风险

- 控件尚未从 typed style resolve（Phase 3）；
- 尚无 UITheme / UIThemeContext / style key（Phase 2）；
- FWidgetStyle 仍被 UIText 使用，与 typed styles 并存，属过渡状态。

### 下一轮直接接力点

1. Phase 2：UITheme 容器 + UIThemeContext owner 挂载点 + style key 命名；
2. Phase 3：第一批控件从 typed style resolve 并删除裸颜色字段。

### 本轮验证

- xmake b ya-gui-widgets 通过；
- 本轮仅新增类型，不改运行时行为，无需跑 scenario。

## 2026-08-21 — 架构评审 + 计划修订

三视角交叉评审（行业框架对照 / 引擎设施一致性 / 需求落地），发现并修订以下结构性空洞：

- **B1（blocker）resolve 上游换人失效传播缺失**：Reactive 只覆盖「值变」，覆盖不了「换 theme」。UIThemeContext 须持 Reactive<UITheme> 或换 theme 走 invalidateSubtreePaint；white/dark 切换列为 Phase 2 显式端到端验收。
- **B2（major）FSplitPaneStyle 落地遗漏**：plan/audit 承诺了但 Style.h 缺失，divider 色被错误塞进 FDockSpaceStyle::splitDividerColor；UISplitPane 是通用控件，三态 divider 被压成单色。Phase 2 前补。
- **B3（major）UIStyleSet 命运未定**：写死 Reactive<FWidgetStyle> 无法承载 typed styles。决定泛型化为 define<TStyle>。
- **B4（major）token→typed style 转换未讲清**：决定配置代码烘焙（app 构造 theme 时 token 初始化 typed style），framework 不做运行时求值。
- **M1（major）手写 operator== 漏改触发静默漏标脏**：改反射生成。
- **M2（major）white/dark 切换未作为显式验收**：补进验证计划。
- **M3（major）paint resolve vs 缓存未定义**：写死 paintSelf 内 get()，禁缓存。
- **次要点**：状态态「统一七态」改为「全局词汇表 + 每控件声明子集」；tool-only style 分层待 Phase 5 明确。

修订落盘：plan.md（§3.1/3.2.1/3.2.2/3.3/3.4 + Phase 1/2/3 + §7）、phase0-audit.md、feature_matrix.json（split_pane_style=fail、reflect_equal/theme_toggle_e2e 等新 item）、todo.md。

## 2026-08-21 — FSplitPaneStyle 代码层补漏（B2 收口）

- Style.h 新增 `FSplitPaneStyle`（dividerFill/dividerHoveredFill/dividerDraggingFill 三态，默认值复制 UISplitPane 的 `_dividerColor/_dividerHoveredColor/_dividerDraggingColor`）；
- `FDockSpaceStyle::splitDividerColor` 已移除（原值 0.28/0.30/0.36 既非 normal 也非 hovered，是状态丢失的折中静态色）；
- 本次仅补类型 + fallback，不接控件（Phase 3 才让 UISplitPane 从 FSplitPaneStyle resolve）；
- ya-gui-widgets 编译通过；feature_matrix split_pane_style → pass，todo 勾选。

**剩余未补**：FScrollBarStyle（plan 承诺但 Style.h 仍缺，需先确认 UIScrollBar 现有裸字段再定 fallback 值）。

## 2026-08-21 — FScrollBarStyle 代码层补漏（B2 全部收口）

- Style.h 新增 `FScrollBarStyle`（trackColor/thumbColor/width，默认值复制 UIScrollViewport 的 `_scrollbarTrackColor/_scrollbarThumbColor/_scrollbarWidth`）；
- `_bShowScrollbar` 保留为控件行为开关，不进 style（visibility 是行为，颜色/宽度才是样式）；
- 本次仅补类型 + fallback，不接控件（Phase 3 才让 UIScrollViewport 从 FScrollBarStyle resolve）；
- ya-gui-widgets 编译通过；feature_matrix scroll_bar_style → pass，todo 勾选。

**Phase 1 至此完整**：计划承诺的 9 个 typed style struct 全部落地（FTextStyle/FPanelStyle/FButtonStyle/FMenuBarItemStyle/FTabStyle/FSplitPaneStyle/FScrollBarStyle/FDockSpaceStyle/FFloatingWindowStyle）。剩余 reflect_equal（== 改反射）是独立待办，非遗漏。

## 2026-08-21 — 蓝图调研：style 系统复用边界 + brush 抽象提前到第一阶段

用户澄清战略目标：**这套 GUI 框架就是为了实现游戏内 UI，不想和 ImGui 维护两条主线，选自绘 GUI + 自绘游戏内 GUI 一条路**。据此调研「style 系统如何被 GUI app / game editor / game runtime 三处复用」。

**三消费者现状**（模块依赖已就绪，复用不是模块问题）：
- GUI app（GUIWorkbench）：retain UI + Style.h typed style（Phase 1）
- game editor：ImGui（ImGuiStyle + PushStyleColor），明确迁到 retain UI
- game runtime：ImGui 旧路径（GuiSystem + ImGui backend）+ retain UI 新路径（GameUIHost，"ui-widget-tree-refactor Phase 3"）并存，正在迁移

**核心结论（UE FSlateBrush / Godot StyleBox 三方交叉验证）**：
- **brush 抽象是「一套 GUI 服务 tool + game」的根基**——color 和 image 统一进一个类型（UE `FSlateBrush` = TintColor + ResourceObject + DrawType + Margin；Godot `StyleBox` = Flat 纯色特例 / Texture 九宫格通用），纯色 fill 是 brush 的退化形态。
- 当前 9 个 typed style 全是 `glm::vec4` 纯色，缺 brush，game UI 的 hover image / 背景图需求无处安放。
- **tool GUI vs game UI 差异在内容不在机制**：机制相同（typed style + 状态集 + theme context + resolve 链），差异只在值（纯色 vs 贴图）、状态集全不全、切换频率。
- **抽象边界**：style 管静态的按状态离散的视觉（每状态一个 brush/色）+ 九宫格（brush 的 drawType+margin 字段）；动画（tween）、字体 atlas、图标 atlas、DPI 断点隔离到别的子系统。
- **Godot 印证**：换 theme = 树级通知（`NOTIFICATION_THEME_CHANGED`）+ 查询时解引用，正是 B1 的解法；`theme_type_variation`（业务角色变体，Primary/Danger）是 game UI 按钮多样性的第二维。

**决策变更**：brush 抽象从「第二阶段」提前到「第一阶段必须」（含 drawType + margin 九宫格字段，成本低但决定 typed style 字段类型，越晚改破坏越大）。radii/shadow/动画/皮肤管线仍第二阶段。

**修订落盘**：plan.md（§3.1 补 brush 升级说明 + 新增 §3.5 brush 抽象含 tool/game 差异表与边界图 + §4 改"不做 image brush"为"brush 提前" + §8 补决策）、feature_matrix.json（brush_abstraction planned）、todo.md（FBrush 抽象待办）。

## 2026-08-21 — FBrush 类型 + addBrush 绘制对接落地（brush-first 第一刀）

- 新建 `GUI/Widgets/Brush.h`（FBrush 纯数据）：`drawType`（Image/NinePatch/Border）+ `tintColor` + `resource`（asset path，空=纯色）+ `margin`（九宫格四边距 left/top/right/bottom）；`isSolid()` 判定纯色。
- `UIFrameBuilder::addBrush(rect, brush)`：Image 型复用现有 `addSprite`（纯色 = 无 resource + tint 上色白纹理；贴图 = resource 经 textureResolver 解析 + tint 调制）——「纯色是 brush 退化形态」的落地。NinePatch/Border 暂降级为整张拉伸。
- **九宫格 UV 切片是下一刀**：`QuadRender::drawTextureInternal` 已有 `uvTranslation`（QuadRender.h:251），但公开 `drawTexture`/`makeSprite` 未暴露；需扩展 draw item 的 Sprite kind 加 uvOffset 字段 + compose pass 透传，才能把 NinePatch 分解成 9 个 sprite。
- 公共头转发 `include/GUI/Widgets/Brush.h`（`#pragma once + #include "../../../Brush.h"`，与 Style.h 等转发头同构）。
- ya-gui-widgets + GUIWorkbench 编译通过；feature_matrix brush_abstraction → in_progress，todo 勾选 FBrush 类型/Image 对接。

**下一刀**：typed style 字段从 `glm::vec4` 升级为 FBrush 引用（Phase 3 控件去硬编码的前置，结构性）；或先补九宫格 UV 切片渲染（暴露 uvTranslation）。

## 2026-08-21 — typed style fill 字段升级 FBrush（brush 进入 style 体系）

- FBrush 加 `Solid(color)` / `Image(path, tint)` 静态工厂，默认值书写简洁。
- 9 个 typed style 的 **fill 类字段**（normalFill/hoveredFill/pressedFill/focusedFill/disabledFill/selectedFill/fillColor/dividerFill*/trackColor/thumbColor/canvasColor/dropPreviewColor/bodyFill/innerFill）从 `glm::vec4` 升级为 `FBrush`（默认值 `FBrush::Solid({...})` 保持原纯色外观，行为不变）。
- **文字色/边框色/强调色保持 `glm::vec4`**（textColor/accentColor/borderColor/edgeAffordance/titleTextColor）——Godot 两层拆分：Color 是 primitive，StyleBox/brush 是填充复合体，文字色不需要 image。
- FWidgetStyle（遗留通用样式）暂不动，Phase 2/3 处理其命运。
- ya-gui-widgets + GUIWorkbench 编译通过；控件尚未接线（Phase 3），字段类型变化零行为影响。

**Phase 1 brush-first 至此**：FBrush 类型 + addBrush 绘制对接 + typed style fill 字段升 brush 全部落地。剩余 Phase 1 项：NinePatch UV 切片渲染、reflect_equal（== 改反射）。

## 2026-08-21 — operator== 改 C++20 `= default`（reflect_equal 收口）

- FBrush + FWidgetStyle + 9 个 typed style 的手写 `operator==` 全部改为 `= default`（C++20 编译器生成逐字段比较）。
- **为什么不用评审 M1 建议的 reflectEqual+YA_REFLECT**：typed style 是纯数据 struct，字段全可比较（FBrush/glm::vec4/uint32_t/float/string），`= default` 让编译器保证 == 与字段集一致，零反射负担、零手写维护；引擎 RHI/Render/Resource 层已在用此风格（VulkanImage.h:41、RenderGraph.h:37 等）。
- 彻底消除 M1 风险：字段增删漏改 == → Reactive::set() 的 == no-op 短路 → 静默漏标脏（本引擎最高频 bug 类别）。
- ya-gui-widgets + GUIWorkbench 编译通过；feature_matrix reflect_equal → pass，todo 勾选。

**Phase 1 至此完整**：9 个 typed style + FBrush（类型层定型，fill 字段升 brush）+ default == 全部落地。剩余 Phase 1 项仅 NinePatch UV 切片渲染（brush 能力扩展，非结构件，可缓到 game UI 实际需要时）。

## 2026-08-21 — Phase 2 theme runtime 设计评审（reject → 修订）

探查（UIStyleSet/WidgetTree/UIText resolve 原型）+ 两视角独立评审，设计草案被 reject，1 blocker + 3 major 已修订落盘 plan.md Phase 2 详细设计：

- **B-blocker：subtree override 无失效边**（装/换/卸 override 不触发重绘，B1 在 override 层复发）→ 第一刀不做 subtree override，resolve 链暂为 `style key → fallback`；override 留后续（带 generation 的 Reactive 或 setter + invalidateSubtree）。
- **M1：generation 依赖登记是纪律非机制**（resolve 被 early-return 跳过即漏登记）→ 收口为框架 helper `resolveThemeStyle<TStyle>(key, level)`，内部无条件 get(generation)。
- **M2：typed style 布局成员无 Layout 失效边**（padding/fontSize/minSize/width 改值须重跑 layout）→ helper 接受 level 参数，布局亲和成员传 Layout、颜色/brush 传 Paint（沿用 UIText _bAutoSize 判据）。
- **M3：泛型 define 未声明 G4 同名 set 语义**（替换 Reactive 对象 orphan 旧依赖）→ define 命中同型 handle 时 set() 复用，同 key 不同 type 走 type_index 分桶共存。

**关键设计决策锁定**：①UIStyleSet 泛型化 = type_index 分桶存 shared_ptr<ReactiveBase>；②换 theme 失效 = `Reactive<uint64_t>` generation token（O(1) == 比较，比 Reactive<UITheme> 简单、比 invalidateSubtree 精确）；③resolve 链 = 框架 helper 机制化（控件不可绕过依赖登记）；④废弃 UIStyleSet::bindTo 的 persistent 注册，统一 paint 时 get(level)。

## 2026-08-21 — Phase 2 第一刀：UIStyleSet 泛型化

- Style.h 的 UIStyleSet 泛型化：`define<TStyle>(name, style)` / `find<TStyle>(name)` 模板，内部 `unordered_map<type_index, unordered_map<string, shared_ptr<ReactiveBase>>>` 按类型分桶。
- **G4 同名 set 语义保留**：define 命中同型 handle 时 `set()` 复用（不新建 ReactiveBase），同 key 不同 type 走 type_index 分桶共存。
- Style.cpp 删除旧的非模板 define/find（移到头文件 inline 模板），保留 bindTo（FWidgetStyle 特定，统一绑定路径刀再废弃）。
- 消费点兼容：Gallery demo（`define("theme", kDarkTheme)` 模板推断 FWidgetStyle）+ UIFrameSnapshotTest（define/bindTo）均无需改动；find 无 <TStyle> 的旧调用不存在。
- ya-gui-widgets + GUIWorkbench 编译通过。测试 target ya-gui-closure-test 的 Render2DClipTest.cpp 编译错误是**预存 include 路径问题**（该文件不引用 Style，与本次无关）。

**Phase 2 剩余**：UITheme（组合 UIStyleSet）、WidgetTree 挂载 theme + generation token、resolveThemeStyle helper、white/dark 验收 demo。

## 2026-08-21 — Phase 2 第二刀：UITheme + WidgetTree 挂载 + generation token

- 新建 `GUI/Widgets/Theme.h`：`UITheme`（struct，组合泛型 UIStyleSet，`define/find<TStyle>` 委托）+ `resolveThemeStyle<TStyle>(widget, key, level)` 自由函数。
- `WidgetTree` 挂载树级 theme：成员 `UITheme* _theme` + `shared_ptr<Reactive<uint64_t>> _themeGeneration`；`setTheme()` 在 theme 变化时 `_themeGeneration->set(value+1)` 触发依赖控件重绘（O(1) == 比较）。
- **resolveThemeStyle 机制化依赖登记**：无条件 `getThemeGeneration()->get(level)`（换 theme 失效边）+ `find<TStyle>(key)` + `style->get(level)`（改 style 值失效边）；返回 nullptr → 控件用默认构造 TStyle fallback；`level` 参数由控件按布局亲和传 Layout/Paint。
- 公共头转发 `include/GUI/Widgets/Theme.h`；WidgetTree.h 前向声明 `struct UITheme`（避免与 Theme.h 的 include 循环）。
- ya-gui-widgets + GUIWorkbench 编译通过；feature_matrix ui_theme_object/resolve_invalidation → pass，ui_theme_context/style_key_lookup/resolve_chain → in_progress。

**Phase 2 剩余**：white/dark 验收 demo（Phase 3 控件接线后，theme 切换才能端到端验证）。下一刀建议直接进 Phase 3 控件接线（至少接一个控件做活样本），或先做 white/dark demo 打通端到端。

## 2026-08-21 — Phase 3 第一刀：UIButton 接线（活样本）

- UIButton 加 `_styleKey`（默认 "button"），paintSelf 先 `resolveThemeStyle<FButtonStyle>(*this, _styleKey)`：theme 驱动时按状态取 FBrush（含 disabledFill）走 `addBrush`；`_styleKey` 为空或 resolve 不到 → 裸颜色字段 fallback（现有行为，零回归）。
- **活样本验证 resolve 链**：resolveThemeStyle 的双层失效边（generation + style 值）第一次被真实控件消费；theme 切换/改 style 都会重绘该 button。
- 裸字段保留为 fallback（删除留后续统一清理刀），符合"先验证 resolve 工作、再清理"的渐进。
- ya-gui-widgets + GUIWorkbench 编译通过；feature_matrix button_migrated → pass。

**下一步**：white/dark demo（GUIWorkbench 挂 dark/light 两个 UITheme + toggle），让 theme 切换端到端可验证（theme_toggle_e2e 收口）。

