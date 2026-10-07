# script-native-registry — S5 脚本 native 注册重构

2026-10-07 立项，`showcase-roadmap` S5（U 组），P1，波 3 开头、先于 G3.2，与 C3 串行（S5 先）。
消费者驱动：G3.2 物理脚本 API 一次注册几十个函数，先修机制再量产，避免用旧机制返工。

## 0. 背景

- `ScriptNativeFn = std::function<ScriptValue(void*, const ScriptRef&, ScriptArgs)>` 完全擦除，函数体靠 `expectArgs` + `get_if` 手工解析；`argTypes` 注解把函数体里已有的类型信息按位置抄第二遍——双重记账。
- 全仓 13 个注册点散在 4 个文件，全字符串键：`SceneScriptBindings.cpp`×4、`LuaWidgetHandle.cpp`×8、`GameplayScriptFunctions.cpp`×1、`LuaScriptingSystem.cpp`×1（`Entity:call`）。
- `SceneScriptBindings.cpp` 的 `entityMethod(Entity&)` 已自发长出「类型化适配」局部补丁，未进绑定层。
- 反射侧零手工：`Function::argTypeIndices` + `collectMembers` 全自动导出；`ScriptBindings.cpp` 已有 `boxValue`/`unboxValue`(ScriptValue↔any) 完整转换链。
- 用户长期方向：现在 Lua，未来 JS 写编辑器脚本、可能渐除 Lua——注册表与前端必须语言中立，backend 只是投影器。

## 1. SOTA 基准（用户 2026-10-07 问「多语言引擎 SOTA」，结论定路线）

- Godot `ClassDB`：单一注册表 + `bind_method` 模板推导签名 + **双调用约定**（动态调用者走 Variant 装箱；静态调用者走 `ptrcall` 类型化跳板，编译期已知类型不过装箱）。
- UE `UFunction`：单一事实源，Blueprint VM / Python / C++ 全从同一份元数据绑定。
- 铁律三条：① 语言后端永远不拥有注册路径（后端=投影器）；② 唯一事实源是带类型签名的注册表（枚举/文档/多语言绑定全读它）；③ 装箱开销不得焊死在唯一路径上。

## 2. 决策（2026-10-07 用户拍板）

| 议题 | 决定 |
| --- | --- |
| 路线 | SOTA 形态 = **单一注册表条目 + 双调用约定**。native 的类型签名作为标准 `Function` 元数据登记进 `ClassRegistry::functions`（`collectMembers` / `crossesToScripts` / `methodArgumentType` / 枚举全自动统一）；调用体保留 ScriptValue 直通跳板挂在 `ScriptMethod`（现状位置）。backend 动态调用走跳板，热路径不被迫过 any 装箱。`reflects` 插件不进 `ScriptValue` 概念（依赖不倒灌泛型反射层） |
| 注册时序 | 反射注册完成后、首次导出前，GameRuntime **集中一处注册清单**收拢全部 native 注入；W2 的「先到者胜」从「导出时 native 占位」改为「`ClassRegistry` 注入先后」，native 先注入则同名反射注册不覆盖（`try_emplace` 语义保持，插 WARN） |
| 迁移顺序 | 试点先行分批：`SceneScriptBindings`（`entityMethod` 是雏形）→ `LuaWidgetHandle` → `GameplayScriptFunctions` → `Entity:call` 最后（热路径，与 C3 衔接） |
| API 形态 | `script::fn(&f)` 模板前向推导：参数类型 → `type_index_t`，编译期 `static_assert(crossesToScripts)`；owner 位 = 参数类型为 `ScriptSelfHandle` 的槽位（恰一个或零个）；适配器按槽位转换好再交给 body，函数体不再 `expectArgs`/`get_if`。内部仍落擦除 `ScriptNativeFn`，backend / resolver 消费路径不变 |
| 旧 API | `addNativeMethod` 保留到 13 点迁完，随后删除；字符串注册路径退场到只剩 `ScriptMethodResolver` 真动态场景（按名解析、运行时才知道组件集的那类） |
| 编辑器 UI | 本期不做脚本浏览器 UI；枚举能力经 `ClassRegistry` 免费获得（测试断言），UI 留给编辑器线 |

## 3. API 对照

```cpp
// 之前：双重记账（函数体里 get_if 过 ScriptSelfHandle，注解再抄一遍）
script::addNativeMethod(clicked, "removeAll", &removeAllClicked,
                        {type_index_v<script::ScriptSelfHandle>});
// 之后：签名即事实源
script::addMethod(clicked, "removeAll", script::fn(&removeAllClicked));
```

`Entity:call`（热路径）迁移时以基准前后对照验收（frame p50 回退 ≤ ±3%），C3 随后在其上做 Lua 调用路径优化。

## 4. Checkpoints

| id | 内容 | 依赖 |
| --- | --- | --- |
| S5.1 | `script::fn` 模板前端 + `ClassRegistry` Function 登记 + `ScriptMethod` 双调用约定；试点迁移 `SceneScriptBindings` 4 点；契约测试（签名推导、owner 位、`crossesToScripts` 编译期拒绝、注入序先到者胜） | — |
| S5.2 | 迁移 `LuaWidgetHandle` 8 点 + `GameplayScriptFunctions` 1 点 | S5.1 |
| S5.3 | 迁移 `Entity:call`（热路径基准对照）+ 删除 `addNativeMethod` + 全仓字符串注册路径审计（只留 `ScriptMethodResolver` 动态场景）+ 枚举测试 | S5.2 |

## 5. 边界

- 不动 backend 调用路径（`LuaScriptingSystem` 的 Lua 侧优化属 C3）。
- `reflects` 插件不引入 `ScriptValue` / Core 依赖。
- 不做宏、不做代码生成、不平行造 UE 式 UFunction 系统（Rule 5）。
- `Generated/*` 只读。

## 6. 验收

- 13 个注册点全部迁移，`addNativeMethod` 删除。
- 字符串注册路径只剩 `ScriptMethodResolver` 真动态场景。
- 脚本↔native 映射可从 `ClassRegistry` 枚举（测试断言，编辑器/文档可直接消费）。
- 全量 `ya-testing` 绿；`Entity:call` 热路径基准 frame p50 回退 ≤ ±3%。