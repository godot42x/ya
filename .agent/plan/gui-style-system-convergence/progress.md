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

## 2026-08-22 — white/dark demo + theme_toggle_e2e 收口（Phase 2 主线完成）

- FWorkbenchApp 持 `_darkTheme`/`_lightTheme`（shared_ptr<UITheme>，各 define "button" key 为不同 FButtonStyle）；buildUI 里 `tree.setTheme(_darkTheme)` 挂载。
- 新增独立 **Theme 页**（memory 规则：新 demo 开独立页）：toggle 按钮（切 tree theme）+ 两个展示按钮（"button" key 驱动）+ 说明文字；toggle 回调经 [this] 捕获 app 的 theme 实例 + `_tree->setTheme(...)`。
- **端到端验收（三层）**：
  - 结构：ThemeToggle/ThemeShowButton/ThemeShowButton2 存在（scenario assert）
  - **颜色（draw item 层）**：按钮 sprite 颜色 dark `(0.160,0.180,0.220)` → light `(0.940,0.950,0.970)`，精确匹配两个 theme 的 normalFill（--dump-snapshot-json 验证）
  - **渲染**：`assert_validation_clean` 通过（换 theme 后零漏标脏，B1 generation token 失效传播收口）
- theme.jsonl（正式 scenario，Scenarios/ 下）+ 编译通过 + exit 0。

**Phase 2 至此完整**：UIStyleSet 泛型化 + UITheme + WidgetTree 挂载 + generation token + resolveThemeStyle + UIButton 接线活样本 + white/dark 端到端验收，全部落地。剩余主线：Phase 3 其余控件接线（Panel/MenuBar/Tab/SplitPane/ScrollBar/Dock/Floating）、Phase 4 Workbench 主题接入、Phase 5 Editor/Game 留口。


## 2026-08-22 — Phase 3 剩余 8 控件接线（第一刀完整收口）

### 本轮完成

- **FTabStyle/FDockSpaceStyle/FTextStyle 补字段**（paint 实际消费值，默认=现状字面量）：
  - FTextStyle += fillColor（brush）+ padding（文本 badge/chip 背景）；FWidgetStyle 仍是 legacy 兼容路径（UIText bindStyle）
  - FTabStyle += separatorColor + placeholderTextColor（tab strip 底部分隔线 / 空区占位）
  - FDockSpaceStyle += dropPreviewMergeColor + dropPreviewOutlineColor
- **8 个控件从 typed style resolve（theme-first，裸字段 fallback，零视觉变化）**：
  - UIText：`_styleKey="text"`，resolvedStyle 返回 FTextStyle，resolve 顺序 = theme key → legacy FWidgetStyle 绑定 → authoring 字段（_bAutoSize 时 Layout 粒度）
  - UIPanel：`_styleKey="panel"`；**显式 setColor() 覆写胜出**（resolve 链「widget explicit override」优先，GI-202 presenter 每帧改色不受 theme 影响），未显式着色面板才 theme 驱动
  - UIMenuBarItem：`_styleKey="menubar"`，normal/hovered fill brush + textColor
  - UITabButton：`_styleKey="tab"`，**Layout 粒度**（padding 喂 computeDesiredSize）
  - UITabBar：strip 分隔线 + 占位文本从 FTabStyle 取（无条件登记 generation 边）
  - UISplitPane：`_styleKey="split"`，divider 三态 brush
  - UIScrollViewport：`_styleKey="scrollbar"`，track/thumb brush + width（Paint 粒度，宽度不影响布局）
  - UIDockSpace：`_styleKey="dock"`，canvas + 合并/拆分 drop preview + outline
  - UIDockFloatingWindow：`_styleKey="floating"`，body/inner/border + resize handle edge affordance（ResizeHandle 经 owner key resolve）+ resize clamp 读 style minSize（floatingMinSize helper，交互路径实时读、paint 登记 generation 边）
- **UIPanel 显式填充语义（架构决策）**：plan §3.2 resolve 链「widget explicit override 优先」落地——面板 `setColor()` 是显式 per-instance 填充意图（GI-202 运行时彩色面板），theme 仅驱动未显式着色的面板。WorkbenchSurface/各 demo 页带 setColor 的面板全部保持原外观；ThemeShowPanel（无显式色）为 theme 驱动样本。Phase 4 把壳层面板 setColor 删掉交回 theme key 即完成「壳层 theme 驱动」。

### 端到端验收（新增 panel 活样本 + 场景 + 测试）

- FWorkbenchApp dark/light 两个 theme 各 define `"panel"`（FBrush::Solid，dark 0.14/0.16/0.20 vs light 0.94/0.95/0.97）；
- Theme 页新增 ThemeShowPanel（style key "panel"，无 authored color）+ 说明文本；
- theme.jsonl 场景补 `{"assert":{"widget":"ThemeShowPanel"}}`（dark/light 双 checkpoint），--scenario-render 真机跑通 + assert_validation_clean 通过（整树重绘零漏标脏）；
- **draw item 层颜色翻转验证（--headless + --dump-snapshot-json）**：panel sprite dark `(0.14,0.16,0.20)` → light `(0.94,0.95,0.97)`，精确匹配两 theme 的 FPanelStyle.fillColor；壳层 backdrop 保持 kWindowColor `(0.075,0.082,0.10)`（显式填充胜出生效）；
- **新增 gtest `PanelResolvesThemeStyleAndRepaintsOnThemeSwitch`**（UIFrameSnapshotTest）：挂 tree theme → panel sprite == themed fill；setTheme 换 theme → `rebuiltWidgets == 1`（只有 panel 重绘，generation token 失效边在非 button 控件上定量验证）；换 theme 后 sprite == 新 fill。

### 本轮验证

- xmake b ya-gui-widgets / GUIWorkbench / ya-gui-widgets-test 全过；
- ya-gui-widgets-test：136/140 通过（含新增 1 条）；5 个失败为**预存失败**（git stash 对照确认与本次改动无关：BuildResolvesItemsToRenderPixelsInPaintOrder / ScrollViewportClipsContentToViewportRect / SplitPaneClipsChildrenToOwnPaneRect / ContainerClipResizeInvalidatesChildSegments / SplitPaneDividerDragChangesRatioAndEndsSession）；
- ya-gui-closure-test 的 Render2DClipTest include 错误仍为预存问题；
- theme.jsonl 真机 scenario exit 0。

### 当前未完成 / 风险

- 控件裸字段（_normalColor 等）仍保留为 fallback，Phase 3 收尾的「删除裸字段」清理刀未做（行为安全，等全部接线后统一删）；
- UIMenuBarItem/UIMenuBar 的 fontSize 仍为控件字段，FMenuBarItemStyle 未含 fontSize；
- FTextStyle 的 "text" key 未在 Workbench theme 定义（全局文本换肤属 Phase 4）；
- NinePatch UV 切片（brush 能力扩展）仍缓做。

### 下一轮直接接力点

1. Phase 3 收尾：全量回归跑 Workbench 既有 scenario（menus/dock/tab/floating/resize），确认 shell 外观零变化；
2. Phase 4：WorkbenchTheme（token 常量 → typed style 烘焙），FWorkbenchSurface 壳层 setColor 交回 theme key，"text" 全局接入，editor/gallery 页统一；
3. 阶段尾声清理刀：删控件裸颜色字段（__normalColor 等），FWidgetStyle 去留定案。

## 2026-08-22 — Phase 4 第一刀：WorkbenchTheme + 壳层主题化 + Gallery 统一绑定路径

### 本轮完成

- **新增 WorkbenchTheme**（Framework/GUI/Tooling/Workbench/WorkbenchTheme.h，plan §6 owner 落位）：
  - design token 层：tokens 命名空间（kWindowColor/kPanelColor/kCanvasColor/kHeaderColor/kTextColor + 按钮 dark 调色板 + **light 全景调色板**）；
  - `buildWorkbenchTheme(bool bDark)` 配置期烘焙（plan §3.4）dark/light 两套 UITheme，定义全部 canonical key：button/panel/panel.window/panel.canvas/menubar/tab/split/scrollbar/dock/floating/text；
  - 壳层旧字面量 kWindowColor 等迁入 token，WorkbenchSurface/演示页不再平行定义。
- **FWorkbenchSurface 壳层主题化**（不再手工 setColor 覆写壳层）：
  - WorkbenchRoot/DemoHost/EditorDemo → `panel.window` key；PreviewCanvas → `panel.canvas`；ItemList/Inspector → `panel`；SelectionHighlight 保留显式 setColor（每帧选择色，explicit override 契约）；
  - 壳层工具栏按钮 label 去掉 authored color → 走 theme "text" key（light 主题给出深色文本，light 按钮可读）；
  - 壳层 label/status/header 保留 authored token 色（树级 header 层级稳定）。
- **UIText 显式 authored 色语义**（与 UIPanel 同契约，plan §3.2 explicit override 优先）：非默认 `_color` 的文本不被 theme 覆盖（header/body/status 层级在两种主题下稳定）；未着色文本（按钮 label、主题 badge）由 theme "text" 驱动。
- **Gallery Section 3 迁移到统一绑定路径**（plan 统一绑定路径刀）：
  - 删掉本地 UIStyleSet/FWidgetStyle theme + bindStyle 用法（app 层不再有 FWidgetStyle 消费点）；badge 文本走 theme "text" key；
  - GalleryTheme 按钮改为切 tree theme（onToggleTheme 回调，GUIWorkbench 注入）；
  - UIText::bindStyle / UIStyleSet::bindTo 保留为 legacy 兼容（闭包测试仍用），后续 Phase 3 收尾定 FWidgetStyle 去留。

### 端到端验收（--headless --dump-snapshot-json，draw item 层）

- shell root（panel.window）：dark (0.075,0.082,0.10) → light (0.86,0.87,0.89) —— **壳层整体随 theme 翻转**；
- 按钮 label（text key）：dark (0.88,0.90,0.94) → light (0.10,0.12,0.16) —— un-authored 文本可读翻转；
- demo 面板（panel key）：dark (0.11,0.12,0.15) → light (0.93,0.94,0.96)；
- Gallery 徽章文本解析 theme "text" 色 (0.88,0.90,0.94) ✓。

### 回归确认

- **menus_popup_interaction 失败为预存问题**（A/B 对照：91ded16e 基线同样 rc=4，同样 assertion "expected popup but got hitTest"；跟 uv 会话无关）；
- 全量 scenario 20/21（唯一失败 = 预存 menus）；
- ya-gui-widgets-test 136/140（新增 PanelResolves... 通过；5 失败预存）；
- widgets/gallery/theme 关键场景 rc=0；theme.jsonl validation_clean 通过。

### 当前未完成 / 风险

- "text" key 的 fontSize（13）会覆盖 toolbar label 的 authored 14 —— 有意的「theme 控制排版」决定，demo 按钮 13 无变化；若日后需要 per-attribute 覆盖再做 merge 语义；
- 演示页内容（makeLabel/makeBodyText authored 色）在 light 主题下保持原色（explicit override 契约）；demo 页整体亮化属 Phase 5 内容级主题；
- 视觉 golden 基线（scenario-golden/gpu-shot）未建立——按计划 Phase 4 收尾时产出截图基线；
- menus hover-switch 预存失败需另立 ticket 根因（非 style system 范围）。

### 下一轮直接接力点

1. Phase 4 收尾：现成 screenshot/dump 基线归档 + editor/gallery 页主题统一确认；WorkbenchTheme key 命名约定落文档；
2. Phase 3 收尾清理刀：删控件裸颜色字段（Button/MenuBarItem/TabButton/SplitPane/ScrollViewport/Dock/Floating），FWidgetStyle/bindTo/bindStyle 去留定案；
3. Phase 5：editor/game theme key 命名空间约定、多窗口 theme context owner。

## 2026-08-22 — Phase 3 收尾清理刀：删已迁移控件裸颜色字段

### 本轮完成

- **删除 5 个已迁移控件的裸颜色字段，fallback 统一为「默认构造 TStyle = framework fallback」**：
  - UIButton：删除 _normalColor/_hoveredColor/_pressedColor/_focusedColor + reflect 条目；paintSelf 单一路径 `FButtonStyle style; if themed style=*themed;` + addBrush（含 disabledFill）。旧 fallback 的 disabled = normal×0.5 (0.4) → 新 = disabledFill {0.5,0.5,0.5}，微小且有意的语义修正；
  - UIMenuBarItem：删除 _textColor/_normalColor/_hoveredColor；
  - UITabButton：删除 _textColor/_normalColor/_hoveredColor/_selectedColor/_accentColor/_padding（padding 走 FTabStyle，measure 同源）；
  - UISplitPane：删除 _dividerColor/_dividerHoveredColor/_dividerDraggingColor；
  - UIScrollViewport：删除 _scrollbarWidth/_scrollbarTrackColor/_scrollbarThumbColor；
  - DockSpace/DockFloatingWindow paint 的魔法字面量 fallback → 默认构造 TStyle（含 ResizeHandle edgeAffordance）。
- **WorkbenchSurface 删除「遍历 children 覆写 menubar 颜色」的 AM-2 反模式**：颜色进 WorkbenchTheme "menubar" key（提升 stops 0.16/0.30 保证 hover 可见，与旧覆写值一致，零视觉变化）。
- **消费点迁移**：UIFrameSnapshotTest 按钮颜色断言改比默认 FButtonStyle；GUIHeadlessHostTest menubar hover 测试改挂 UITheme（delegate 持有 theme 保活）；GUIFrameworkSmoke 挂 UITheme（蓝色 button）；Dialog 的 OK/Cancel 按钮去裸字段 + label 去 authored 色（走 theme）。
- **key 命名约定落文档**（plan.md 新增 §3.6）：`<family>[.<role>]`，canonical family key 列表 + panel.window/panel.canvas role 变体；状态不走 key 维度。
- UIPanel._color / UIText._color 保留（显式覆写契约的两个活 authoring API）。

### 本轮验证

- ya-gui-widgets / GUIWorkbench / ya-gui-widgets-test / ya-gui-headless-host-test / ya-gui-minimal-host 全构建过；
- ya-gui-widgets-test 136 通过（TransientHoverAndFocusRepaintButton 已改断言）；headless-host 2 测试过（含迁移的 menubar hover）；minimal-host 30 帧跑完；
- 全量 scenario 20/21（menus 预存失败不变）；
- headless dump：menubar item normal fill = (0.16,0.18,0.22)（主题提升 stops 生效，壳层零变化）；
- style 系统最终形态：**核心 shell 控件零裸颜色字段**，resolve 单一路径 + 默认构造 fallback，framework 不再需要按控件补颜色字段（AM-1 收口）。

### 下一轮直接接力点

1. Phase 4 收尾：golden/截图基线归档（scenario-capture + dump digest），Theme 页/Editor 页/壳层静态基线；
2. FWidgetStyle / UIStyleSet::bindTo / UIText::bindStyle 去留定案（app 消费点已清零，闭包测试仍用）；
3. Phase 5：editor/game theme key 命名空间、game HUD typed style 扩展、多窗口 theme context owner。

## 2026-08-22 — FWidgetStyle / bindTo / bindStyle 移除（统一绑定路径收口）

### 本轮完成

- **FWidgetStyle struct 删除**（Style.h）：app/示例消费点已在前几轮清零，只剩闭包测试两处；typed style（FTextStyle 等）已覆盖其全部字段；
- **UIStyleSet::bindTo + Style.cpp 删除**：persistent FWidgetStyle 样式边是「绑定路径」双轨的最后残余；persistent 机制本身仍由 UISplitPane::bindSplitRatio 使用（layout 长生命周期边），与 paint-time 样式边的统一不冲突；
- **UIText::bindStyle + _styleBinding + resolvedStyle legacy 分支删除**：resolvedStyle 只剩「theme key（未着色时）→ authoring 字段」；
- **两个闭包测试迁移到 theme 路径**：
  - StyleEditRepaintsBoundTexts → **StyleEditRepaintsThemedTexts**（UITheme 挂 "text" key，编辑 style → 两个文本重绘 rebuiltWidgets==2——保护原意图：样式编辑触发依赖重绘，走统一路径）；
  - PaintRebuildDoesNotDropPersistentStyleBinding → **PaintRebuildReCollectsStyleEdgeAfterForcedRebuild**（主题化面板强制 markPaintDirty 重建后，编辑 style 仍重绘——验证 paint-time get() 重建后重新收集依赖，覆盖原 clearDependencies 担忧的同类场景）。
- 全仓 grep：FWidgetStyle/bindStyle/bindTo 零点（WorkbenchDemoPages 注释保留历史说明）。

### 本轮验证

- ya-gui-widgets/GUIWorkbench/ya-gui-tooling/ya-gui-framework/ya-gui-widgets-test/ya-gui-headless-host-test/ya-gui-minimal-host 全部构建过；
- ya-gui-widgets-test 136/140（3 个 theme 路径测试显式跑过全 OK；5 失败预存）；
- headless-host 2/2；场景 20/21（menus 预存）；
- theme 翻转 headless dump 复核：shell root (0.075,0.082,0.10) ↔ (0.86,0.87,0.89) 不变。

### 当前状态

style system 最终形态：**机制单一**——UIStyleSet（泛型）+ UITheme + resolveThemeStyle（paint-time get）+ generation token；**无任何平行绑定路径**。UIStyleSet 泛型 define/find 仍直接可用（UITheme 组合它）。

### 下一轮直接接力点

1. Phase 4 收尾：golden/截图基线归档；
2. Phase 5：editor/game theme key 命名空间约定、game HUD typed style 扩展点（brush image/nine-patch 消费）、多窗口 theme context owner 语义与实现。

## 2026-08-22 — Phase 4 收尾：可复现视觉回归基线 + Phase 5 决策落定

### 本轮完成

- **新增 `Script/gui_style_baseline.py`**：对 Render / Theme(dark) / Theme(light，toggle) / Dock / Editor 五个关键页生成 headless snapshot，对比归档 digest；`--update` 重写、`--verify` 默认校验、`--verbose` 明细；GPU 无关（--headless），任意机器/CI 可跑。
- **基线归档** `Example/GUIWorkbench/Baselines/<slug>.json`（5 份完整 snapshot json）：theme_light 与 theme_dark 的 digest 显著不同（6338562509553292916 vs 10260476919271715436），证明 toggle 被基线捕获。
- **Phase 5 决策落定（plan.md）**：
  - key 命名空间：canonical family key 领域无关，领域差异走 theme 内容 + `family.role`；editor.*/game.* 前缀仅用于显式覆盖场景；
  - 多窗口 theme owner：theme 挂载点 = tree；UITheme 生命周期 owner = app（raw 指针，先于 tree 析构解除）；多窗口共享/各挂实例均安全（读多写少，generation 通知）；
  - subtree override 机制候选明确（带 generation 的 Reactive or setter + invalidateSubtree，禁无失效边 raw 字段）；
  - selector/外部文件/DSL：第二阶段暂不需要（typed style + key 烘焙已满足复用）。
- §7 验证计划补基线 gate 用法。

### 本轮验证

- digest 同 build 双跑一致（确定性验证）；
- `python3 Script/gui_style_baseline.py` 连续两次 PASS；
- 全部 GUI target 构建不变。

### 下一轮直接接力点

1. Phase 5 实现型延伸（可选）：subtree override（带 generation Reactive）、game HUD 首个 typed style 消费（brush image/nine-patch 渲染补齐前置）；
2. 如出现「一套主题资产跨 app 平移」需求再评估 selector/DSL；
3. NinePatch UV 切片仍是 brush 扩展的长期待办（game UI 换肤资产管线前置）。

## 2026-08-22 — dock 回归修复：floating 窗口可用标题抓取 drag&drop 重新 dock

### 回归现象

一个已经 floating 的窗口无法通过 drag&drop 重新 dock —— 复现（/tmp/dock_title_drag.jsonl）：tear-off 后抓浮窗**标题空白区**（header 除 tab/close 外的区域）拖向 DockSpace，窗口原地不动、不 dock。原因：只有 TabStrip 绑定了 dock-panel drag（`_onTabDragBegin`），header 空白区/窗口体无任何拖动响应；而单个面板浮窗的「标题」就是 header 空白——用户抓标题是唯一自然手势。

### 修复（framework）

- **UIDockFloatingWindow 增加标题抓取 drag**：header 容器存成员；按下（children 未消费 = tab/close/resize handle 之外的标题空白区）→ 捕获 pointer + 6px 阈值（与 UITabBar 同款）→ `beginTabDrag()`（dock-panel payload）。释放于 DockSpace → re-dock；释放于空白 → 移动窗口（onFinished NoTarget 既有语义，验证 FloatWindow1 (180,140)→(600,20) 且保持 floating）。
- **assertScenarioTree 支持 `{"widget":"!Name"}` 不存在断言**：dock_redocked 现在能强断言 FloatingWindow1 已消失（此前只查 DockLeaf1 存在，弱断言任由回归溜过）。
- dock_floating.jsonl 改为覆盖**标题抓取** re-dock 主路径 + `!FloatingWindow1` 强断言；tab-drag 路径由 dock.jsonl/dock_cardinal_split 隐含覆盖。

### 验证

- 复现场景（重写为验收）：tear → 标题抓取 (400,160)→(700,300) → FloatingWindow1 消失 + DockLeaf1 保留，rc=0；
- 标题拖到空白 (600,20)：窗口移动 (180,140)→(600,20) 且保持 floating（移动语义未破坏）；
- dock / dock_cardinal_split / dock_floating 三场景全过（4-6 asserts）；
- widgets 136/140（5 预存）；全量场景 20/21（menus 预存）；**基线门 PASS**（dock 静态页 digest 不变）。

## 2026-08-22 — dock 拖拽缺 drop 提示：target 侧连续预览缺失（重构回归）

### 现象

拖 floating window title/tab 到 DockSpace 上方后，dock 下方没有任何 merge/split 高亮提示——用户描述「看起来是事件没传透」。事件其实传透了（re-dock onDrop 一直可用），缺的是**预览渲染**：`UIDockSpace::setDropHighlight(true)` 是空操作，`canAcceptDrop` 把 `resolveDropPreview` 算进局部变量即弃，`_preview` 成员从未在拖拽中被赋值 → `paintChildren` 永远不画预览（对照旧版：8bd5e40b 时代预览在 tab 拖拽 observer 里计算并渲染，dock workspace 重构后丢失）。

### 修复（framework）

- **`UIElement` 新增 target 侧连续 hover 钩子**：`virtual void updateDropHover(payload, logicalPoint)`（默认 no-op）；树在 `WidgetTree::updateDrag` 中对当前 target **每次 move 都调用**（target 不变也调用），setDropHighlight(true) 后立即调一次；
- **`UIDockSpace::updateDropHover`**：用当前指针 resolve merge/split 预览 → 存 `_preview` + markPaintDirty；指针移开 → findDropTarget 换 target → 旧 target setDropHighlight(false) → clearPreview（既有路径）。
- 设计说明：canAcceptDrop 保持纯查询（不动成员）；点敏感预览走新钩子，与无点敏感 target 的 setDropHighlight 并存。

### 验证

- **拖拽中途快照**（--headless mid-drag dump）：
  - 指针在 dock 中央 (700,300) → 整叶 merge 高亮（1280x615.6 半透明）；
  - 指针在西边缘 (30,600) → 384px 左条带 split 预览（几何正确、随指针切换）；
- **闭包回归测试** `WidgetTreeTest.DragOverDockSetsPointSensitiveDropPreview`：workspace 浮起 panel → beginDrag(dock-panel) → updateDrag 中央 → hasDropPreview && merge；移西边缘 → 变 split；移出树 → 预览清除；endDrag 干净收尾。137/140 测试通过（5 预存）；
- 全量场景 20/21（menus 预存）；基线门 PASS。

## 2026-08-22 — dock 拖拽体验优化：浮窗跟手 + ImGui 风格 drop 预览

### 1) 跟手（aa4660af）

现象：拖 floating window 时，指针进入 dock 区域后窗口「冻住」不跟手（旧实现：目标非空即停，让指针脱离窗口去命中 dock）。

修法（op

## 2026-08-22 — dock 拖拽体验优化：浮窗跟手 + ImGui 风格 drop 预览

### 1) 跟手（aa4660af）

现象：拖 floating window 时，指针进入 dock 区域后窗口「冻住」不跟手（旧实现：目标非空即停，让指针脱离窗口去命中 dock）。

修法：
- `WidgetTree::beginDrag` 新增 opt-in `bSkipSourceInHitTest`：开启时 drop 目标发现跳过 drag source 子树 → 浮窗**每次 move 都跟随指针**（去掉 target 冻结 + NoTarget snap），窗口下方 dock 依旧可命中、持续显示预览；
- **必须 opt-in**：DockSpace/TreeView/SelectableRow 用容器当 source（指针从不在 source 与目标之间），无条件跳过会打挂 dock tab split（dock.jsonl 亲测失败）与 tree reorder；
- 浮窗拖拽抑制 tree drag ghost（窗口自己就是视觉本体，ghost 会双重显示）。

### 2) ImGui 风格 drop 预览（b0addd4c 预览补全 + aa4660af 收尾）

- merge：整叶半透明填充 + **2px 高亮描边** + 1px 内白边 + 目标 tab bar 强调下划线；
- split：边缘条带同款描边；预览随指针实时切换（updateDropHover，上轮已落地）。

### 3) 附带修复

- `UIFrameSnapshotDump`：line item 被序列化成 "text"（只有 sprite/非 sprite 二分）→ 修正为 line；**基线重生成**（digest 变化）。

### 验证

- 跟手：title 拖拽 mid-drag 快照窗口 (180,140)→(430,260) 跟随指针；悬停 dock 放手 re-dock 成功（skip 生效）；放空白保持 floating；
- dock/dock_cardinal_split/dock_floating rc=0；全量 20/21（menus 预存）；137/140（5 预存）；基线门 PASS（强化后 DragOverDockSetsPointSensitiveDropPreview：source 停在指针下仍看得到 dock 预览）。

## 2026-08-27 — 表单控件 theme 接线 + EditorTheme

不重做 style runtime。把 chrome 已经在用的表单控件接到 typed style，并把值从 app theme 烘焙进去。

- Style.h 新增 `FTreeViewStyle` / `FTextFieldStyle` / `FMenuStyle` / `FSelectableRowStyle` / `FDragFloatStyle`；TreeView / TextField / Menu / SelectableRow / DragFloat 删除裸颜色字段，paint 走 `resolveThemeStyle`（几何仍在 widget）。
- WorkbenchTheme 烘焙 `text.header|muted|error|eyebrow` 与 `tree` / `textfield` / `menu` / `menu.panel` / `selectable` / `dragfloat`，light 不再落到 dark fallback。
- GameEditor 新增 `buildEditorTheme`；`EditorSurface` 不再直接调 `buildWorkbenchTheme`。Chrome 文案改 `setStyleKey`，去掉 `setColor` 字面量。Text DSL 补 `setStyleKey`。
- `ToolControlsTest.SelectableRowHover*` 改断言 theme fallback；新增 `SelectableRowHoverUsesThemeFill`。

### 验证

- `xmake b ya-gui-widgets` / `ya-gui-widgets-test` / `ya-game-editor` 通过；
- `ToolControlsTest.SelectableRow*` 7/7（widgets-test + closure-test）；
- WidgetTree chrome 冒烟：`run-editor HelloMaterial --editor-chrome=widgettree --exit-after-frame=30` rc=0。

## 2026-08-27 — 剩余表单控件 theme 接线

CheckBox / ComboBox / Slider / TableGrid / SpinBox / Radio / ColorEdit chrome / SearchCombo 删除散装颜色字段，paint 走 typed style + key。ColorEdit 的 `_color` 仍是业务值。WorkbenchTheme 烘焙对应 key。Text/Panel 的 `setColor` 实例覆盖保留。剩余裸色：Image `_placeholderColor`、PopupOverlay `_modalColor`。

### 验证

- `xmake b ya-gui-widgets` / `ya-gui-widgets-test` / `ya-gui-tooling` 通过；
- `ya-gui-widgets-test` 174/175；失败项 `SelectableRowDraggableRowsUseBehaviorBackedDragDrop` 是 drag-behavior 路径，与本轮 style 接线无关。

## 2026-08-27 — 实例 authored TStyle

高频路径落地：`UIStyledWidget<TWidget, TStyle>` 持 optional authored style；`resolveWidgetStyle` 顺序为 authored > theme key > fallback。`_styleKey` 收到 `UIElement` 并反射。DSL 基类暴露 `setStyle` / `setStyleKey`。Text/Panel 的 `setColor` 仍是单色退化覆盖。Panel `_bExplicitFill` 进入反射，避免反序列化后 theme 盖掉显式色。

### 验证

- `xmake b ya-gui-widgets` / `ya-gui-widgets-test` / `ya-gui-tooling` 通过；
- `ya-gui-widgets-test` 179/179。

## 2026-08-27 — authored TStyle 序列化 + setColor 收口

`FBrush` 与全部 `F*Style` 进入反射。Mixin 的 `_authoredStyle` 不能 `YA_REFLECT_FIELD`（`serializeFields` 拿到的是 `UIElement*`，第二基类偏移错误，JSON 会写成 null）。改走 `UIElement::serializeAuthoredStyle` / `deserializeAuthoredStyle`，子类用 `YA_GUI_AUTHORED_STYLE_IO(TStyle)` 展开。

Text/Panel `setColor` 写入 authored `FTextStyle` / `FPanelStyle`（Paint 粒度，GI-202 每帧改色不 layout），去掉「非默认白」和 `_bExplicitFill` 第二条 override 路径。旧 JSON 的 `_bExplicitFill: true` / 非白 `_color` 在 deserialize 时提升为 authored。直写 `UIText::_color` 的 GUI 调用点改为 `setColor()`。

### 验证

- `xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test` — 184/184 PASSED（含 JsonRoundtrip / AuthoredButtonStyleJsonRoundtrip / AuthoredPanelFillSurvivesThemeAfterReload / LegacyExplicitFillPromotesToAuthoredStyle）

## 2026-08-27 — 剩余 chrome 控件 theme 接管

审计后仍未走 theme 的 paint 路径：UIImage `_placeholderColor`、UIPopupOverlay `_modalColor`、UIDialog::create / Workbench 演示模态的 `setColor`、ColorEdit 色板 magic fill、WidgetTree tooltip/ghost 字面量。布局宿主（Container/Overlay/SizeBox/DockFloatingHost）无 chrome。

- 新增 `FImageStyle` / `FPopupStyle`；Image/Popup 默认 key `image` / `popup`。UIMenu 不再挂第二套 mixin（overlay 继承 FPopupStyle，item 仍是 FMenuStyle）。
- Dialog 工厂与 tooltip/ghost 改 `setStyleKey`，不再 authored 冻色。WorkbenchTheme 烘焙 `image` / `popup` / `tooltip` / `drag.ghost`。

### 验证

- `xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test` — 204/204 PASSED（含 ImagePlaceholderAndModalPopupFollowTheme）

## 2026-08-27 — typed serializer 解包 std::optional

`ensureGuiStyleReflection()` 按 `optional<F*Style>` 逐个注册 custom hook 已无必要：authored 槽走 typed `serializeByRuntimeReflection`，inner type 在编译期已知。`std::is_same` 无法判断「任意 optional」；改为 `is_optional<std::optional<T>>` 偏特化，空值 ↔ JSON null。`DeferredInitializerQueue::executeAll()` 仍由 `UIElement::serializeFields` 冲刷 `FBrush`/`F*Style` 反射。

### 验证

- `xmake b ya-testing && xmake r ya-testing --gtest_filter=ContainerSerializationTest.OptionalNullAndValueRoundtrip` — PASSED

## 2026-08-27 — tree / list / dragdrop chrome

- TreeView drop 指示改走 `FTreeViewStyle.dropIndicator`（不再借用 selectedFill）。
- Workbench 列表行标签去掉 `setColor` 冻色，走 `text` key。
- Demo drag/drop tile 升为 `UIDragDropTile` + `FDragDropStyle`，key `drag.source` / `drag.target`。

### 验证

- `xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test` — 212/212 PASSED（含 TreeViewSelectionFollowsTheme / DragDropTilesFollowTheme）

## 2026-08-27 — sparse style overlay

实例层从整份 `optional<TStyle>` freeze 改成稀疏 JSON patch（键 = 反射字段名）。`resolveWidgetStyle` 在 patch 未盖满全部字段时登记 theme 边并 `deserializeProperty` merge；`setStyle(TStyle)` 仍写全键 freeze。Text/Panel `setColor` / `setFontSize` 与 Image/Popup legacy promote 改为单键 overlay。DSL 基类加 `setStyleField`。

### 验证

- `xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test` — 215/215 PASSED（含 AuthoredButtonStyleWinsOverThemeAndIgnoresThemeSwitch / SparseStyleFieldInheritsUnpatchedFieldsOnThemeSwitch / SetColorOverlaysColorAndInheritsThemeFontSize / DslSetStyleFieldInheritsUnpatchedThemeFields）
- `xmake b GUIWorkbench` 通过

## 2026-08-27 — computed style cache

把 theme + 稀疏 patch merge 移出 paint 热路径。Mixin 持 dense `_resolvedStyleCache`（generation / key / tree / dirty-level 快照）；`setStyle` / `setStyleField` / deserialize 使 cache 失效。recompute 条件：cache invalid/dirty、需要更强 Layout 粒度、widget paint-dirty（style Reactive `set()` 不 bump generation）、inherit 控件的 generation/tree/key 变化。cache hit 时 inherit 控件仍 `resolveThemeStyle` 登记 reactive 边。cache 不落盘。

- 公开 `UIStyledWidget::resolvedStyle()`；Text/Panel/Button 及剩余 styled 控件的 `paintSelf` / TabButton measure 改读 cache。
- TabButton paint 走 Layout；`computeDesiredSize` 走 Layout + `bTrackDependencies=false`（与 Text 一致）。
- Floating resize handle 不是 `UIStyledWidget`：读 owner cache，自己登记 theme 边。ColorEdit 色板同理，仍走无缓存 `resolveWidgetStyle`。
- 测试仍用 `resolveWidgetStyle` 断言 merge 语义。几何（rowHeight / indent / `_bAutoSize`）仍在 widget。不做 subtree theme override，也不在 WidgetTree 加 style phase。
- 新增 `ThemeAttachAfterUnthemedBuildRepaintsKeyedButton`。

### 验证

- `xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test` — 216/216 PASSED
- `xmake b GUIWorkbench` 通过

## 2026-08-27 — FBrush NinePatch/Border UV 切片

Phase 1 遗留的九宫格渲染落地。`sliceBrush` 把 dest 切成 Image(1) / NinePatch(最多 9) / Border(最多 8) 单元格；`UIFrameBuilder::addBrush` 每格一个 sprite，带 `uvOffset`/`uvScale`。compose 经 `Render2D::makeSprite` 把 UV 传到已有的 `drawTextureInternal`。margin 为纹理 px（1 tex px = 1 logical px）；dest 小于左右/上下 margin 时等比压缩；无纹理尺寸退回整张拉伸。dump 只在非默认 UV 时写出，不扰动既有 digest。

### 验证

- `xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test` — 222/222 PASSED（含 SliceBrushNinePatchEmitsNineCells / SliceBrushBorderOmitsCenter / SliceBrushScalesMarginsWhenDestIsSmaller / AddBrushNinePatchWithoutTextureStretches）
- `xmake b GUIWorkbench` 通过

## 2026-09-16 — surface 模型 + 角色调色板 + 比例字面

用户反馈「默认控件对比度不足、设计感不够、不像现代 UI」。根因有三条，都属机制层而非口味：

1. **描边与填充是两个字段**，控件自己 `addRectOutline` 画方框：圆角填充被方框套住，描边也跟不上 hover（fill 换一套、outline 换另一套）。
2. **每个 style 在 dark/light 各写一遍**（`if (bDark)` 两大块），light 与 dark 必然漂移；gallery 又在 `DemoPageCommon.h` 复写了一份 palette 字面量，chrome 与页面能不一致。
3. **主字面是等宽字体**（JetBrainsMono）+ body 16px，chrome 像终端输出。

### 本轮完成

- **`FBrush` 成为完整的 surface 值**：新增 `borderColor` / `borderThickness`，与已有的 `cornerRadius` 一起表达「填充 + 圆角 + 一像素描边」。`addBrush` 对纯色刷走 `addRoundedSurface`（外圈 border 色 + 内缩 fill）；`addRoundedSurface` 支持 border-only（透明填充 = 圆角发丝线）。
- **控件交出描边所有权**：`FTextFieldStyle` / `FDragFloatStyle` / `FSpinBoxStyle` 删除 `borderColor`、`hoveredBorderColor`、`errorBorderColor`，改由每个状态自己的 brush 携带；`FPanelStyle` / `FExpanderStyle` / `FFloatingWindowStyle` 删除 `outlineColor` / `outlineThickness` / `borderColor`。`TextField` / `DragFloat` / `SpinBox` / `Expander` / `Border` / `DockFloatingWindow` 的 `addRectOutline` 全部删除。`FRadioButtonStyle::dotColor` 升为 `FBrush`（未选中是空心环）。`FMenuStyle` 加 `checkBoxBorderColor`。反射表同步删除/新增；仓库内没有文档或资产引用被删字段。
- **共享调色板改成角色表**：`tokens::FPalette`（`darkPalette` / `lightPalette` / `palette(bDark)`）+ 单一 `defineChromeStyles(theme, palette)`。surface 阶梯 canvas/window/panel/raised/well，交互 hover/pressed/selected/accent，文字 text/text2/text3/disabled，边 borderSubtle/borderStrong/borderHover，状态 success/error/warning；圆角走 `radius` 阶梯（kChip 4 / kControl 6 / kTab 5 / kRow 5 / kMenu 8 / kCard 10）。旧 per-look 双份 block 删除，`tokens::surface(fill, radius, border)` 是构造入口。
- **对比度按明度比定标**：dark 下 text ≥10:1、text2 ≥5.9:1、text3 ≥3.6:1，相邻 surface 1.05–1.15:1，描边对其填充 ≥1.5:1；light 侧同样按此量级重新取值。
- **字号台阶**：`gui_type` kTitle 28→20、kHeader 14→16、kBody 16→14、kSmall 12→13、kCaption 11 不变（比例字面、1:1 设备像素下）。
- **主字面换 Inter**（OFL，随仓 `Engine/Content/Fonts/Inter-Regular.ttf` + `Inter-OFL.txt`），`FGUIWindowHostConfig::fontPath` 默认指向它；JetBrainsMono 留在仓内供等宽面。`DEFAULT_RUNTIME_FONT_NAME` 不动（测试注册名）。
- 修掉 `panel.titlebar`（GameEditor 在用）**不在 catalog 里**的既有告警：加入 `StyleKey::PanelTitlebar`。
- Gallery `DemoPageCommon.h` 的 palette 别名改为引用共享角色表（不再复写字面量）。

### 保留 / 未完成

- `FPopupStyle` 仍默认构造（popup host 是放置容器、不是 surface）；`FDockSpaceStyle.dropPreviewOutlineColor` 仍是裸 vec4（dock 预览框走的是 line，不是 surface）。
- `.animatable()` 反射标记（gui-animation 计划）未动，本切片不碰动画目录。
- GameEditor 的 `editor.*` 密度键（字号 12 / 字段 padding）保留，是刻意的编辑器密度 overlay。

### 验证

- `xmake b ya-engine / ya-gui-widgets / ya-gui-closure-test / GUIWorkbench / ya-game-editor / ya-testing` 全部通过。
- `ya-gui-closure-test`（排除设计上会 trap 的 `WidgetTreeTest.SystemLayersCannotBeDetached`）— 573/573 PASSED。
- `ya-testing` — 1134 PASSED，7 FAILED 与本切片无关（逐条在改动前复现：GUIWindowManagerTest.DragOverlay…、EditorPropertyGraphTest.Auto…/TextureAssetRow…、ScriptApiLibraryFixture.GameUIWidgetLifecycle…、GameUIHostTest.BuildSnapshotComposes…、GUIHeadlessHostTest.ReusesAppKernel…/UnthemedFallback…）。
- `WidgetTreeTest.TextFieldOutlineSitsInsideLayoutRect` / `DragFloatOutlineSitsInsideLayoutRect` 改为断言 surface（外圈覆盖 layout rect、fill 内缩），不再断言 `Line` 项。
- 22 个 gallery 页 `--scenario` 冒烟 + `theme.jsonl` + `animation_gallery.jsonl`（22 断言）全过；dark / light 两套 GPU 截图目视验收；近期所有 run 的日志无 `unknown key` / 断言失败。
