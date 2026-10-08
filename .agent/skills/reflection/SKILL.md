---
name: reflection
description: YA 反射运行时的层次、MetaValue 形态、facet 规则，以及 type_index 不落盘。
---

## 适用场景

- 给类型加 JSON、比较、打印或组件文档行为
- 改序列化、拷贝、脚本值或动态注册
- 判断一个身份该写进场景文件还是只留在进程内

稳定规则在这里。宏的写法见 `Engine/Source/Framework/Core/Reflection/README.md`。

## 四层

1. **reflects（schema）**：类型身份、成员、继承、最终成员视图（父类在前，派生同名覆盖，后登记的父类覆盖同名）。注册表有版本、变更通知、按模块注销。对外只读查询。不认识 glm、JSON、脚本。
2. **TypeOps**：每个类型一行。facet 挂在这一行上。reflects 只存不解释。
3. **MetaValue**：动态调用的参数和返回值，也是脚本中立值。内建种类是空、bool、整数、浮点、字符串、借用引用。保留确切宽度，不把 `int8` 压成 int64。
4. **语言后端**：只做投影。内建种类直接映射。别的类型按 `type_index` 查这个后端登记的投影器。

## 层次

- reflects 拥有类型身份、成员 schema、TypeOps 和 MetaValue 的机制。
- 各系统拥有自己的 facet。JSON 在序列化层，相等和打印在值类型注册处，组件文档在组件自己的翻译单元。
- 脚本层拥有调用约定（receiver、owner 槽）和暴露策略。签名推导不上移到 reflects。
- `reflects-core` 零依赖：不 include glm、Core、Log、json。
- 容器元素的动态操作仍走 `ContainerProperty`。不新增宏，不新增代码生成。引擎用宏注册。Python 生成器休眠，不是注册路径。

## MetaValue 的三种存储

| 形态 | 何时 |
| --- | --- |
| 内联 | 可平凡拷贝，且 ≤ 32 字节、对齐 ≤ 16（vec、quat、句柄、矩形） |
| 持有的盒子 | 其余已注册类型。堆上存，拷贝和析构走 TypeOps |
| 借用引用 | `{type_index, void*, bConst}`。`T&` / `T*` / 输出参数绑到调用方对象，写入落在原对象上。只在当前调用内有效 |

长期持有的引用用引擎登记的安全句柄（`ScriptRef` 等），不用借用引用。裸借用引用不进脚本状态。

## facet

facet 是普通结构体，用 `TypeRegistry::setFacet` 放进类型行。缺失策略写在 facet 类型的 `missing` 上。

| facet | 缺失时 |
| --- | --- |
| `FJsonFacet` | 序列化器先问它，再枚举，再类。都没有则 `YA_CORE_ERROR` 并跳过该字段（写出 null）。标量只在属性路径上走 facet；`serializeByRuntimeReflection` 对标量仍返回 null |
| `FComponentJsonFacet` | 没有记录 = 只做反射。`serialize` 在反射对象之后追加。`bUseReflection = false` 时这份类型自己写整份文档 |
| `FEqualFacet` | 跳过。`PropertyAccessor::equals` 在枚举之后问它，然后才是资产引用的路径比较 |
| `FIntegerFacet` | 跳过。只登记 `int` / `int32_t` 和 `uint32_t`。不要用「4 字节整数」把 Windows 上的 `long` 算进去 |
| `FPrintFacet` | 用 fallback，打印类型名 |
| `FValueStruct` | 跳过。有此标记的结构体按字段进脚本，脚本层不点名这个类型 |

vec2/3/4 的 JSON 是数组 `[x,y,z]`，对象形式只在读入时接受。它们的种类是 `Object`，facet 必须先于类遍历，否则会写成 `{x,y,z}`。

组件上不再用 `useReflectionSerialization` / `serializeCustom` / `deserializeCustom` 虚函数。类型级的 JSON 形状只留 facet。`cloneCustom`、`onEdit`、`onPostSerialize` 仍是实例虚函数。

编辑器的颜色、轴比较、叶子分类还按类型分支，等 RR4。不要提前改成「有相等 facet 就是叶子」。

## 持久身份

- 反射注册名是持久身份，全局唯一。冲突在注册时抛，报错带双方的编译器类型名。嵌套类型用 `YA_REFLECT_BEGIN_NAMED` 给唯一名字。
- **`type_index` 不写入场景、缓存或资产元数据。** 它是编译器类型名的 FNV，进程内稳定，跨编译器不同。文件里只存反射名。
- 类成员表是私有的。读 `functions()` / `properties()` / `propertyOrder()`，改用 `getFunction` / `getProperty`。

## 注册与注销

- `moduleId == 0` 是进程生命周期。`unregisterModule(0)` 什么都不拆。
- 模块加载在 `loadLibrary` 之前绑定 id，卸载时先注销。镜像不 `dlclose`。
- 真正的增删才通知。重复函数名不升版本。监听者只读或丢掉指针，不要往正在通知的那个类里插入。
- `setFacet` 要求类型行已经存在。`YA_REFLECT` 的真正注册推迟到 `DeferredInitializerQueue::executeAll`。组件 facet 排在这次类注册之后。值类型 facet 在 `registerCoreValueTypes` 里登记，它在同一轮 `executeAll` 的开头。
