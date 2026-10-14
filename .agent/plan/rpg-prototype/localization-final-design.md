# YA Engine 本地化最终方案

> 基于 agent_95e13f41 深度调研 + agent_3cf3d092 的 P0-P3 方案
> 
> **核心决策**：方案 A（可本地化文本属性）→ C-lite（编辑器集成），跳过方案 B（FText-like manifest 机制）

---

## 0. 现状盘点（2026-10-14）

### 已落地（commit `eb07437e`）
- ✅ `LocalizationManager`（tr/format/missingKeys/multicast）
- ✅ `LocalizedText`（响应式，语言变化自动重解析）
- ✅ Lua 绑定（`locale.tr/setLanguage/language/languages/loadTable`）
- ✅ 源文本即 key 策略（`"Brass Key": "黄铜钥匙"`，查表失败返回 key）

### 空洞
- ❌ Lua 缺 `locale.format`（P1 插值未完成）
- ❌ `editorTr` 全仓库 **0 调用点**（P2 断链）
- ❌ UI 文档 `_text` 字段烘焙为裸字符串（不可切换语言）
- ❌ 每个显示点需要手写 `Locale.tr(choice.text)`（用户原始痛点）

---

## 1. 最终方案：A + C-lite

### 为什么不做方案 B（FText-like）

**UE FText 的核心价值**：
- 编译期三元组（namespace+key+source）
- gather/manifest 机制，stale 检测
- 跨模块命名空间治理

**为什么不适合 YA**：
1. **Lua 是主要文案载体**（无编译期身份，B 的护城河在 Lua 侧失效）
2. **规模不匹配**：YA 当前 2 语言、单团队、无多模块文案协作
3. **侵入性大**：需要改表格式、UI 文档 schema、反射字段
4. **收益不足**：gather/manifest 只在"多语言 × 多模块 × 多人改文案"时回本

**只借鉴**：`namespace.key` 命名惯例（P0 已采纳：`editor.inspector.empty`）

---

### 方案 A：可本地化文本属性

**核心思路**：让文本自己知道"我是个 key"，订阅语言变化自动重解析。

#### A1 - Lua 侧 `textKey` 字段

**目标**：消除显式 wrapper

**API 设计**：
```lua
-- 当前（痛点）
label.text = Locale.tr(choice.text or "")

-- 方案 A
label.textKey = choice.text       -- 设置后自动订阅语言变化
label.textKey = "Set {0}"         -- 参数化文本
label.textArgs = { tostring(n) }  -- 插值参数
```

**实现路径**（`LuaWidgetHandle.cpp`）：
- 新增 `textKey`/`textArgs` 原生字段（类似现有 `text` 字段，第 469 行附近）
- 内部构造 `LocalizedText`，调用 `UIText::bindText(localizedText.toReactive())`
- 复用现有 `LocalizedText` 的语言变化订阅机制

**验收**：
- `LocalizedTextTest.cpp` 补充"设置 textKey 后切换语言文本自动更新"用例
- `Dialogue.lua` 改用 `textKey`，自动化测试继续通过

---

#### A2 - C++ 声明式构建器 `localize()`

**目标**：编辑器 chrome / C++ UI 代码的简化入口

**API 设计**：
```cpp
// 纯 key
root.child(ui::text("Title").localize("Quest accepted"));

// 带参数
root.child(ui::text("Count").localize("Set {0}", {toStr(n)}));

// 已有等价写法（今天就能用）
root.child(ui::text("Title").bindText(LocalizedText{"Quest accepted"}.toReactive()));
```

**实现路径**（`ControlBuilders.h`）：
- 在 `TextBuilder` 增加 `localize(key)` / `localize(key, args)` 方法
- 内部构造 `LocalizedText` + 调用已有的 `bindText(Reactive<string>)`

---

#### A3 - 闭环 P1（插值系统）

**目标**：Lua 补齐 `locale.format`

**API 设计**：
```lua
local msg = Locale.format("You have {count} items", { count = 5 })
-- 或直接
label.textKey = "You have {count} items"
label.textArgs = { count = 5 }
```

**实现路径**（`GameplayScriptFunctions.cpp`，第 570-631 行）：
- 补充 `locale.format(key, args)` 绑定
- 内部调用 `LocalizationManager::format`（已实现）

---

#### A4 - P2 接线（编辑器 chrome）

**目标**：让 `editorTr` 第一次真正生效

**改动点**：
1. **`EditorActionCatalog.cpp`**：菜单标签从裸字符串改为 `editorTr("editor.action.saveScene")`
2. **`EditorSurface.cpp` / chrome 相关**：所有硬编码文本接 `editorTr`
3. **设置项**：添加"编辑器语言"设置，调用 `setEditorLanguage`（当前 0 调用者）

**验收**：
- `editor.zh.json` 的 key 第一次被消费
- 编辑器启动后切换语言，菜单标签实时更新

---

### 方案 C-lite：编辑器集成（分级推进）

**前提**：A 完成后才能做 C（文本必须先成为可本地化属性）

#### C2 - 伪本地化（Pseudo-localization）★ 最高优先级

**核心价值**：零翻译成本，立刻暴露布局问题

**机制**：
```cpp
// LocalizationManager 增加伪本地化模式
void LocalizationManager::enablePseudoLocalization(bool enable);
// tr() 前置拦截：
// "Save Scene" → "Şävé Šçéñé !!!" （加重音 + 加长 30%）
```

**暴露问题**：
- 布局爆框（按钮宽度不足）
- CJK 缺字（字体 fallback）
- 换行异常（`UIText::_bWrap` / `_maxWrapWidth`）

**实现成本**：**低**（1 天）
- `LocalizationManager` 增加 `_bPseudoLocalization` 开关
- `tr()` 内一个字符串转换函数（[a-zA-Z] → 带重音变体 + 插入填充字符）
- 编辑器增加"Tools > Pseudo-localization"菜单项

**验收**：
- 开启伪本地化，编辑器所有文本变成 `Şävé Šçéñé !!!` 风格
- 观察到至少一个布局爆框案例（如果没有说明布局健壮）

---

#### C1 - 覆盖率叠加层（Coverage Overlay）

**核心价值**：可视化翻译完成度

**机制**：
- UI Designer 增加"本地化视图"模式
- 画布上：已翻译文本正常、缺失 key 高亮红色、过时 key（source 变化）黄色
- 数据源：`LocalizationManager::missingKeys()`（已实现）

**实现路径**（`EditorUIDesignerSession.cpp`）：
- 画布绘制时，查询当前文本的 key 是否在 `missingKeys()` 集合中
- 高亮矩形叠加在文本下方（类似拼写检查下划线）

**实现成本**：**中**（3-5 天）

**验收**：
- 加载 `QuestNotice.yaui`，切到本地化视图，未翻译文本高亮红色
- 删除 `zh.json` 中某个 key，该文本立刻变红

---

#### C3 - 上下文感知 key 生成

**核心价值**：解决"源文本即 key"的可维护性软肋

**机制**：
- UI Designer 中给 `UIText` 输入字面量时，自动生成 `ui.<doc>.<node>` 格式 key
- 把源串存为 `source`，写回 `*.yaui.json`（走 undo 栈）

**API 设计**（文档 schema 扩展）：
```json
{ "typeId": "engine.text",
  "fields": { 
    "_text": { 
      "key": "ui.questnotice.title",
      "source": "Quest accepted" 
    } 
  } 
}
```

**实现成本**：**中**（5-7 天）
- 文档 schema 版本升级（`version: 3`）
- Inspector 编辑 `_text` 时，生成 key + source
- 加载旧文档时，迁移裸字符串为 `{key: text, source: text}`

---

#### C4 - AI 辅助翻译驱动

**核心价值**：YA 相对 UE/Godot 的创新点

**机制**：
```bash
python3 Script/locale_translate.py \
  --source Example/2DRpgPrototype/Content/Locale/en.json \
  --target zh \
  --backend local  # 或 openai/deepseek
```

**工作流**：
1. 脚本读 `en.json` 的所有 key/value
2. 调用本地 LLM（复用 `.agent/skills/image-gen` 的 ComfyUI 模式）或在线 API
3. 产出 `zh.json.draft`
4. 人工在**覆盖率叠加层**（C1）里逐条确认（human-in-the-loop）

**实现成本**：**中**（脚本为主，不改引擎，3-5 天）

**复用资产**：
- image-gen skill 已建立"本地优先 + 在线兜底"后端选择
- `missingKeys()` 诊断可生成待翻译清单

---

## 2. 实施顺序与验收标准

### Phase 1 - A1/A2/A3（核心 API，1 周）

**目标**：消除显式 wrapper，闭环 P1

**改动文件**：
- `LuaWidgetHandle.cpp`（textKey/textArgs 字段）
- `ControlBuilders.h`（localize() 方法）
- `GameplayScriptFunctions.cpp`（locale.format 绑定）
- `Dialogue.lua`（示例改写）

**验收**：
- [ ] `LocalizedTextTest` 补充 textKey 切换语言用例，通过
- [ ] `Dialogue.lua` 改用 `textKey`，自动化测试通过
- [ ] RPG 场景哈希变化在预期内（只改 Lua 调用方式，不改显示结果）

**提交**：`[core/gui/locale] A1-A3: keyed text property + Lua format`

---

### Phase 2 - A4（P2 接线，3 天）

**目标**：编辑器 chrome 第一次真正本地化

**改动文件**：
- `EditorActionCatalog.cpp`（菜单标签接 editorTr）
- `EditorSurface.cpp` 及 chrome 相关（窗口标题、按钮文本）
- 设置项（"编辑器语言"）

**验收**：
- [ ] `editor.zh.json` 的 key 全部被消费（`missingKeys()` 为空）
- [ ] 编辑器启动后切换语言，菜单/标题实时更新
- [ ] `setEditorLanguage` 至少有 1 个调用者

**提交**：`[editor/locale] A4: wire editorTr into chrome + settings`

---

### Phase 3 - C2（伪本地化，1 天）

**目标**：立刻暴露布局问题

**改动文件**：
- `LocalizationManager.cpp`（enablePseudoLocalization）
- `EditorMenus.cpp`（Tools 菜单项）

**验收**：
- [ ] 开启伪本地化，编辑器所有文本变成 `Şävé Šçéñé !!!`
- [ ] 观察到至少一个布局爆框或字体 fallback 案例
- [ ] 可切换回正常模式

**提交**：`[editor/locale] C2: pseudo-localization for layout testing`

---

### Phase 4 - C1（覆盖率叠加，3-5 天）

**目标**：可视化翻译完成度

**改动文件**：
- `EditorUIDesignerSession.cpp`（本地化视图模式）
- `EditorUIDesignerTools.h`（工具栏开关）

**验收**：
- [ ] UI Designer 切到本地化视图，缺失 key 高亮红色
- [ ] 删除 `zh.json` 中 key，相应文本立刻变红
- [ ] 切换回正常视图，高亮消失

**提交**：`[editor/locale] C1: coverage overlay in UI Designer`

---

### Phase 5 - C4（AI 翻译，3-5 天，可选）

**目标**：AI 辅助翻译闭环

**交付物**：
- `Script/locale_translate.py`
- 使用文档（README）

**验收**：
- [ ] 脚本读 `en.json`，产出 `zh.json.draft`
- [ ] 人工在 C1 覆盖率叠加层里确认翻译
- [ ] 至少支持一个本地后端（如 Ollama）+ 一个在线后端

**提交**：`[tools/locale] C4: AI-assisted translation script`

---

### Phase 6 - C3（上下文 key 生成，5-7 天，可选）

**目标**：解决"源文本即 key"的维护性问题

**改动文件**：
- `UIDocument.h`（schema v3）
- `EditorUIDesignerSession.cpp`（自动生成 key）
- 迁移工具（v2 → v3）

**验收**：
- [ ] UI Designer 输入文本时，自动生成 `ui.<doc>.<node>` key
- [ ] 加载旧文档，自动迁移为新 schema
- [ ] 修改源文本，key 不变，翻译不丢失

**提交**：`[editor/locale] C3: context-aware key generation`

---

## 3. 关键文件路径

### 引擎核心
- `Engine/Source/Framework/Core/include/Core/Localization/LocalizationManager.h`
- `Engine/Source/Framework/Core/Localization/LocalizationManager.cpp`
- `Engine/Source/Framework/GUI/Runtime/Binding/include/GUI/Binding/LocalizedText.h`
- `Engine/Source/Framework/GUI/Runtime/Binding/LocalizedText.cpp`

### Lua 绑定
- `Engine/Source/Applications/GameRuntime/Script/GameplayScriptFunctions.cpp`（locale.* 绑定，第 570-631 行）
- `Engine/Source/Applications/GameRuntime/Script/LuaWidgetHandle.cpp`（UIText.text 原生字段，第 469 行）

### 编辑器
- `Engine/Source/Applications/GameEditor/include/GameEditor/Localization/EditorLocalization.h`
- `Engine/Source/Applications/GameEditor/Localization/EditorLocalization.cpp`
- `Engine/Source/Applications/GameEditor/UI/Shell/EditorActionCatalog.cpp`
- `Engine/Source/Applications/GameEditor/EditorUIDesignerSession.cpp`
- `Engine/Source/Applications/GameEditor/include/GameEditor/UI/Tabs/EditorUIDesignerTools.h`

### 声明式构建器
- `Engine/Source/Framework/GUI/Runtime/Declarative/include/GUI/Declarative/ControlBuilders.h`
- `Engine/Source/Framework/GUI/Runtime/Widgets/include/GUI/Widgets/Controls/Text.h`

### 测试
- `Engine/Test/Source/Core/LocalizationManagerTest.cpp`
- `Engine/Test/Source/GuiWidgets/LocalizedTextTest.cpp`

### 翻译表
- `Example/2DRpgPrototype/Content/Locale/zh.json`（游戏）
- `Engine/Content/Locale/editor.zh.json`（编辑器）

---

## 4. 边界与不做清单

### 明确不做
- ❌ **不引入 gather/manifest 管道**（UE FText 的编译期收集）
- ❌ **不改 `_text` 字段为复合反射类型**（C3 是可选扩展，不是默认）
- ❌ **不做全局隐式 Auto-Translate**（劫持 `setText` 有风险）
- ❌ **不做多 namespace 模块治理**（当前规模不需要）

### 保持不变
- ✅ **源文本即 key 策略**（`"Brass Key": "黄铜钥匙"`）
- ✅ **JSON/CSV 表格式**（不升级到 PO/XLIFF）
- ✅ **`tr(key, fallback)` 作为唯一解析原语**
- ✅ **语言变化走 multicast 事件**（已有机制）

---

## 5. 与 P0-P3 方案的关系

| 原方案 | 本方案 | 状态 |
|--------|--------|------|
| P0（基础 tr API） | 已落地（eb07437e） | ✅ 完成 |
| P1（插值系统） | A3 闭环 | 🔄 Phase 1 |
| P2（编辑器 chrome） | A4 接线 | 🔄 Phase 2 |
| P3（热重载） | 保持现有机制 | ✅ 已有 |
| 无 | C2 伪本地化 | 🆕 Phase 3 |
| 无 | C1 覆盖率叠加 | 🆕 Phase 4 |
| 无 | C4 AI 翻译 | 🆕 Phase 5（可选）|
| 无 | C3 key 生成 | 🆕 Phase 6（可选）|

---

## 6. 决策理由（给未来维护者）

### 为什么是"可本地化文本属性"而不是"FText 类型"？

**UE FText 的价值**：编译期身份 + stale 检测 + manifest 治理  
**YA 的现实**：Lua 无编译期 + 2 语言 + 单团队  
**结论**：方案 B 的机器成本 > 收益，且违反"不照抄 UE"

**方案 A 的选择**：
1. 复用已有 `LocalizedText`（响应式，语言变化自动重解析）
2. 暴露为 UI 属性（`textKey`），而不是包装类型
3. 保持 `tr()` 作为唯一解析原语，不引入新的身份层

### 为什么先做 C2（伪本地化）而不是 C1（覆盖率）？

**C2 的价值**：零翻译成本，立刻暴露布局/字体问题  
**C1 的价值**：可视化翻译进度，但需要先有翻译表  
**结论**：C2 是"发现问题"，C1 是"跟踪进度"，先修好房子再装修

### 为什么 C4（AI 翻译）是"创新"而不是"噱头"？

**UE/Godot 现状**：翻译表导出 → 人工/外包翻译 → 导入，无 AI 集成  
**YA 的资产**：已有本地 AI 基建（image-gen skill）+ missingKeys 诊断  
**结论**：C4 是把两个已有能力组合出差异化，符合"不做 Yet Another Game Engine"

---

## 附录：参考 commit 与调研来源

- `eb07437e` - P0 落地（LocalizationManager + LocalizedText）
- agent_3cf3d092 - P0-P3 五阶段方案
- agent_95e13f41 - Godot/UE/Unity API 深度对比 + A/B/C 三方案
- `.agent/plan/rpg-prototype/plan.md` - G1 本地化目标
- `.agent/skills/image-gen/SKILL.md` - 本地 AI 基建先例
