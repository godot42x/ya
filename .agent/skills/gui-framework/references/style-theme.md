## Style / Theme

- 机制在 framework：`UITheme` + `resolveThemeStyle` + generation token。值在 app：
  WorkbenchTheme（demo 壳）/ EditorTheme（GameEditor chrome）。
- **共享 chrome 值只有一个角色调色板**：`DefaultChromeTheme.h` 的 `tokens::FPalette`（`darkPalette(flavor)` / `lightPalette()` / `palette(bDark, flavor)`）+ `defineChromeStyles(theme, palette)`。surface 阶梯是 canvas < window < panel < raised（+ well），交互是 hover/pressed/selected/accent/accentFill，文字是 text/text2/text3/disabled，边是 borderSubtle/borderStrong/borderHover，状态是 success/error/errorFill/warning。加一个 key = 加一个角色，**不要**再写 `if (bDark)` 的第二份 block（旧 builder 每个 style 写两遍，正是 light/dark 漂移的来源）。`gui_chrome::tokens::surface(fill, radius, border)` 是「填充+圆角+描边」的构造入口。
- **flavor 是整张表，不是几个 key 的覆盖**：`EPaletteFlavor{OneDark, Neutral}`，`darkPalette(flavor)` 选 dark 表，`buildDefaultChromeTheme(bDark, flavor)` / `buildWorkbenchTheme` / `buildEditorTheme` 逐层透传。逐个 key 调色是外壳漂向「没人设计过的样子」的原因，而只盖三个 key 的「flavor」无法被当作一个 look 评审。light 只有一个表：flavor 是 dark 轴（one-dark 没有 light 对应物），不要在这里发明第二张 light 表。
- **accent 与 accentFill 是两件事**：`accent` 是**墨**（focus 环、caret、勾、tab 下划线），`accentFill` 是**底**（toggle 按钮、checked box、slider 已填充段）。一个蓝不可能同时干这两件事——在 dark 平面上够亮的蓝托不住白字；`error` / `errorFill` 同理。要写「带文字的强调填充」时用 Fill 那个。
- 圆角走 `tokens::radius`（kChip 3/kControl 4/kTab 3/kRow 4/kMenu 6/kCard 8），不要就地写字面量。斜坡刻意紧：22px 高的输入框配 6px 圆角是**网页**比例，工具 chrome 比网页更硬，同一个框降到 4px 才有边而不是融成胶囊。
- 对比度是契约不是口味，而且**表面和描边要克制**：文字要够（dark 下 text ≥12:1 / text2 ≥6.9:1 / text3 ≥4.4:1，按 canvas/window/panel/raised/well 五个静止平面取最小；hover/pressed 是瞬时冲刷，会更亮），但相邻 chrome surface 只差 ~1.06–1.13:1 —— 层级靠「刚好可辨的一级台阶 + 一条边」，不靠明度差；台阶一大，外壳就变成一堆深浅不一的方块。
- **输入控件是这条「克制台阶」规则的例外**：输入必须读作**下陷**，所以 `well` 比它所在平面低一整档（dark ~1.19:1 / light ~1.14:1），而不是像 chrome 平面那样只差一级。内陷**就是**输入的可供性；没有这个台阶，输入框只是另一个矩形，满屏输入框会显得平。
- dark 平面带真正的**蓝灰味**（蓝 ≈ 红的 1.3 倍），不是中性灰加一点蓝：贴黑的**中性**阶梯没有色相差可分辨，眼睛只剩明度，看起来就是「闷」。light 表相反，是灰底白内容，"raised" 表示 chrome 平面（比白内容**低**一档），不是浮起来。最深的平面别压到近黑，否则 dock/toolbar 像在外壳上挖的洞。
- 描边是**半透明白（light 用黑）**，不是不透明灰：这样一条 token 在任何平面上都成立。分两级，分开才是重点：`borderSubtle` 是 chrome 平面之间的发丝（~1.2:1，**按钮 / tab / 列表与树的选中行 / expander header 完全不画描边** —— 填充台阶已经把它们分开了，处处描边是「线框感」的最大来源）；`borderStrong` / `borderHover` 描的是**可交互**的东西（输入框、check box、popup、floating window），必须真能找到（对填充 ~1.63:1 / ~2.35:1）——**「虚」的观感绝大部分来自一条找不到位置的输入框边**。focus / dropTarget / error 这类瞬时状态留 accent 环。
- 字号走 `gui_type`（kTitle 20 / kHeader 16 / kBody 14 / kSmall 13 / kCaption 11，1:1 设备像素下的比例字体台阶）。atlas 只有一个字重，所以层级**只能**靠尺寸，别指望靠 weight。字体族走 `FTextStyle::fontFamily`（空 = 引擎 UI 主字面；非空 = 另一个已注册 family，如 `MONO_UI_FONT_NAME`），**不要**在控件里硬写 family 名。
- **色板（ColorEdit swatch）的边来自 style，不是控件字面量**：swatch 的 fill 是用户正在编的颜色，theme 无法预测，所以 `FColorEditStyle` 只管它**周围**的东西（`swatchBorderColor` / `swatchHoverBorderColor` / `swatchCornerRadius` / `swatchBorderThickness`）。不要再用「textColor 的半透明白冲洗」当描边：用户恰好选到同色时它什么也不是。走 `addRoundedSurface` 而不是 `addRectOutline`（后者跟不了圆角，会画成方框套圆角）。
- Resolve：稀疏 patch（JSON 键 = 反射字段名）overlay 到 theme 的 dense `TStyle`。`setStyle(TStyle)` 写全字段 = full freeze（不登记 theme 边）；`setStyleField` / Text·Border `setColor` 只盖出现过的键，其余 inherit，**必须**登记 generation + style Reactive。空 / null / `{}` = 无覆盖。
- Field impact：`lookupStyleFieldImpact` / `lookupStylePatchImpact` 用反射分类字段，而不是 per-type 表。`FBrush` → Paint+Resource；`fontSize` / `padding` / `minSize` → Layout；`FScrollBarStyle.width` 是 overlay 绘制厚度，不是 Layout；`UIScrollViewport` 把 children clip/hit 到 gutter 左侧，thumb 在 children 之后画，避免 list hover/selection 盖住滚动条。`setStyle` / `setStyleField` / `clearStyleField` / `clearAuthoredStyle` 默认走 catalog；`UIText::setFontSize` 等仍可显式覆盖。`bResource` 是 metadata；异步就绪不走 `invalidateProperty`。Font 由 `FontManager::resourceRevision()` 在 `WidgetTree::buildSnapshot` 消费：revision 变化则整树 `markLayoutDirty(ResourceReady)`（嵌套 fill 容器不能 skip 过期文字度量）。Texture 走 tree 上的 `FGuiTextureCatalog`（path-keyed `Reactive` revision）：`bind(path)` 订阅当前 paint widget，ready/fail 只 `markPaintDirty` 该 path 的订阅者。`UIFrameBuildContext.generation` 只表示 resolver **身份**被换掉（测试/host 换 source），日常 ready **不** bump 它、也不清全树 paint cache。Host 只 `flushPendingGlyphs`，不要再 `invalidateSubtree`。
- Visual fill matrix：`composeVisualFlags` + `FVisualChrome` + `resolveVisualFill` 是 exclusive 优先级（Disabled > DropTarget > Error > Pressed > Selected+Hovered > Selected > Hovered > Focused > Normal）。`visualChrome(style)` 覆盖 Button / SelectableRow / CheckBox / ComboBox / MenuBar / Tab / MenuItem / TableGrid / TreeView 行填充。CheckBox 的 checked 与 Tab 的 selected 映射为 Selected，且 Selected+Hovered 回落到 Selected（保持原“选中盖住 hover”）。Table/Tree 未选中且未 hover 的 normal 是透明刷，paint 仍按 alpha 跳过。`UITextField` / `UIDragFloat`（含 ColorEdit 通道）不走这套 matrix，但 **必须** 有自己的 `hoveredFill` + inset `borderColor`（self-clip 会吃贴边 1px stroke）。TextField 文字必须 clip 到 padded inner，不要画穿 chrome。
- Dock / floating chrome：`kDockContentInset`（1px）是 leaf `DockContent` 与 floating `_content` 的 padding，给 editor 控件 outline 留边；不要给每个 DragFloat 再加 margin。窗口底边裁切不是再加 padding 能解的：EditorSurface Dock 必须 insets 到 `kChromeTop` + `kChromeInset`，否则 fill dock 比窗口高一段 chrome。
- Key catalog：`YA_GUI_STYLE_CATALOG` / `StyleKey::*` 是 theme key + `TStyle` 的单一词汇。`lookupStyleKey` 校验 `define` / `setStyleKey` / document deserialize；未知 key 与类型不匹配记入 `StyleCatalogDiagnostics` 并 `YA_CORE_WARN`，不拒绝写入。空 key 表示不查 theme。`editor.<key>` 是同一词汇的 GameEditor overlay，不是第二套机制。`canvas` 角色 key 只表示「无 chrome 的 canvas host」，canvas 本身不 paint。
- 控件 paint/layout 读 `UIStyledWidget::resolvedStyle()`（dense cache）。merge（theme base + 稀疏 patch）在 dirty/recompute 时发生，不在每帧 paint 热路径。`resolveWidgetStyle` 仍是无缓存计算路径，给测试断言和非 `UIStyledWidget` 节点（如 ColorEdit 色板）用。cache 不落盘。
- 高频路径是实例 `setStyle` / `setStyleField` / `setStyleKey`（DSL 基类 builder 已暴露）；切 theme 是低频目录切换。未盖满的控件在切皮肤时未覆写字段跟着变。
- `_styleKey` 在 `UIElement` 上反射；稀疏 patch 经 `YA_GUI_AUTHORED_STYLE_IO` 虚函数写入 UIDocument 的 `_authoredStyle`（mixin 字段不能 `YA_REFLECT_FIELD`，MI 偏移不对）。缺键 = inherit；旧文档的全字段对象仍是 freeze。`FBrush`/`F*Style` 走运行时反射，merge 用 `deserializeProperty`。
- `FBrush`：纯色 = 无 resource + tint；Image 整张拉伸；NinePatch/Border 按 `margin`（纹理 px，1 tex px = 1 logical px）切成最多 9/8 个 snapshot sprite，compose 经 `uvScale`/`uvOffset` 透传。无纹理尺寸时退回整张拉伸。`sliceBrush` 是纯函数。
- **Surface 模型（style 的可视单元 = 一个刷子）**：纯色 `FBrush` 还带 `cornerRadius` 与 `borderColor`/`borderThickness`，即「填充 + 圆角 + 一像素描边」是一个值。`addBrush` 对它走 `addRoundedSurface`（外圈 border 色 + 内缩 fill，SDF round-rect 分支），所以一个控件每个状态只挑**一个** brush，hover 抬起填充时描边自动跟着抬。控件**不再**自持 `borderColor`/`outlineColor` 字段，也不再自己 `addRectOutline`（描边跟不了圆角，会画成方框套圆角）。由控件自身尺寸推导的几何（switch knob、radio dot 的圆）由控件自己算（`extent.y*0.5f`），因为 theme 不可能知道控件尺寸。
- 例外：**image / nine-patch / border 刷子忽略 `cornerRadius`/`borderColor`**（形状来自资源）；给它们再叠一圈主题描边就是关于形状的第二个真相。
- `UIBorder` paint 只读 `resolvedStyle().fillColor`（含主题圆角/描边）；`_cornerRadius` 是单实例覆盖（`instanceEditable`），叠在 style 的 radius 之上。无 theme 时 `_color` 是 fillColor fallback（与 `UIText` 的 `_color`/`_fontSize` 相同）。内容图不走 Border 字段：`overlay().child(border()).child(image())`。Widgets 禁止 include / 调用 `AssetManager`，禁止 paint 里 `loadTextureSync`。`ya-gui-closure-test` 必须仍不链 AssetManager。
- 无 theme / 缺纹理 / 延迟就绪的 GPU 输入就是 snapshot：`UIImage` Pending/miss 画 `placeholderFill`，catalog Failed 或 `setResourceMissing(true)`（live RT 缺席）画 `errorFill`。不要为「路径尚未加载」调 `setResourceMissing`。
  产品 host：`tree.setTextureSource(&gameUITextureSource())`（AssetManager lookup + async `loadTexture`，onReady → `catalog.notify(path)`）。Workbench：builtin source（lookup 即 Ready，`requestLoad` 空操作）。
  Widgets 禁止 include / 调用 `AssetManager`，禁止 paint 里 `loadTextureSync`。`ya-gui-closure-test` 必须仍不链 AssetManager。
  Headless host 与 windowed compose 消费同一份 packet；`setDpiScale` 与 `uiScale` 正交折叠（`EditorInputContractTest`）；windowed `--gpu-shot` 像素门禁走 `Script/automation/gui/run_workbench_gpu_parity.py`。
- 族 key：`panel` / `button` / `text` / `menubar` / `tab` / `split` / `scrollbar` /
  `dock` / `floating` / `image` / `popup`。角色 key：`panel.window` / `panel.canvas` / `panel.sidebar` /
  `tab.dock` / `tab.sidebar` / `text.header` / `text.muted` / `text.error` /
  `text.eyebrow` / `text.small` / `text.caption` / `menu.panel` / `tooltip` / `drag.ghost` / `drag.source` /
  `drag.target`。表单 key：`tree` / `textfield` / `textfield.compact` / `menu` /
  `selectable` / `dragfloat` / `checkbox` / `combobox` / `slider` / `table` /
  `spinbox` / `radio` / `coloredit` / `searchcombo`。
- `editor.*` 前缀只用于 GameEditor 显式覆盖，不是第二套词汇。
- Shell 控件（Border/CanvasPanel/Button/Text/MenuBar/Tab/Split/Scroll/Dock/Floating）和已接线的
  表单控件（TreeView/TextField/Menu/SelectableRow/DragFloat/CheckBox/ComboBox/
  Slider/TableGrid/SpinBox/Radio/ColorEdit chrome/SearchCombo）以及 Image 占位 /
  Popup 遮罩 / DragDrop tile paint 时读 `resolvedStyle()`；几何（rowHeight/indent/thumbSize）留在 widget。
  实例覆盖走 `setStyle(TStyle)`（freeze）或 `setStyleField`（单键 inherit）；Text/Border 的 `setColor` 只 overlay 颜色（Paint 粒度），字号等跟 theme。
  列表行标签走 `text` key，不要 `setColor` 冻色。布局宿主（CanvasPanel/Container/Overlay/SizeBox/DockFloatingHost）无 chrome paint。


