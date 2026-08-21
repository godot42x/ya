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
