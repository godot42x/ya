# reflection-runtime — 反射层与脚本层整体规范化

2026-10-08 立项，`showcase-roadmap` U 组，P1，排在 C3 与 G3.2 之前（两者都建在脚本绑定上）。
吸收 `script-native-registry/`（S5）：S5.1 已入库部分保留，S5.2 / S5.3 并入本线 RR8。

## 0. 现状（2026-10-08 盘点）

- **每个消费方各写一份按类型分支的 if 链**（`type_index_v<...>` 出现次数）：`PropertyAccessor.cpp` 35、`ReflectionSerializer.cpp` 31、`ScriptBindings.cpp` 13、`ReflectionCopier.cpp` 6、编辑器 `PropertyGraphBuilder.cpp` 5。加一个新值类型（如 `glm::quat`）要改五处。
- **两套动态值并存**：反射调用 ABI 是 `std::any` + `ArgumentList`；脚本是封闭 variant `ScriptValue`（glm 向量、句柄、`ScriptRect` 写死成分支），两者之间靠 codec / trampoline 桥接。`Function::invoker` 全仓只有 `ScriptBindings` 一个调用方，`ArgumentList` 只出现在 reflects 与脚本层。
- **reflects 没有扩展机制**：S5.1 为挂脚本跳板加了 `Function::payload`（`std::any` 盲盒）；序列化器有半成品钩子 `trySerializeCustomType`。
- **reflects 没有最终成员视图**：继承展开、覆盖规则、owner path 由脚本层 `collectMembers` 自己算。
- **注册表是公开可变容器**：`Class::functions` / `properties` 是 public map，任何系统可直接改，无版本、无通知。
- `reflects-core` 目前零依赖（只有测试依赖 gtest），是要保住的性质。

## 1. 决策（2026-10-08 用户拍板）

| 议题 | 决定 |
| --- | --- |
| 范围 | 反射 + 脚本 + 序列化 + `PropertyAccessor` + 拷贝 + 编辑器检查器，五处类型分支全部收进 TypeOps |
| 值模型 | **开放 MetaValue**：内建标量（空 / bool / 整数 / 浮点 / 字符串 / 对象引用）+ 一个按 `type_index` 标识的开放分支（内联存储，不分配堆），操作从该类型的 TypeOps 行查。与 TypeOps 一起放 `reflects-core`，零依赖。glm、句柄、`ScriptRect` 等由引擎层启动时注册为值类型。有意不同于 Godot 的封闭 Variant：换「加类型不改核心」 |
| 动态注册 | 加载后可注册 / 注销（Godot extension 式）。每次变更提升版本号并发通知；消费方（脚本导出表、编辑器反射缓存、序列化缓存）收到后重建 |
| S5 | 暂停 S5.2；S5 并入本线作为脚本侧迁移步骤（RR8）；S5.1 已入库部分保留 |
| 优先级 | P1，排在 C3 与 G3.2 之前 |

## 2. 目标形态（四层，各管一件事）

1. **reflects（schema）**：类型身份、成员、继承；提供最终成员视图（继承展开、覆盖、owner path 算好）；注册表带版本号与变更通知；对外只读查询，写入走注册 API。不认识任何上层值类型。
2. **TypeOps（每类型行为表，reflects-core）**：每个类型一行，挂各系统的能力（facet）：与 MetaValue 互转、JSON 读写、拷贝、比较、打印、编辑器控件。facet 由拥有它的系统注册（JSON facet 由序列化层注册，编辑器 facet 由编辑器注册）；reflects 只存不解释。加一个新类型 = 在一处注册它的能力。
3. **MetaValue（运行时值 ABI，reflects-core）**：反射动态调用的参数与返回值、脚本中立值、自动化动态面。`Function::invoker` 直接收发 MetaValue；`std::any` / `ArgumentList` 退出外部 ABI。`ScriptValue` 被 MetaValue 取代。
4. **语言后端（Lua / 未来 JS）**：只做投影。内建标量直接映射；开放分支按 `type_index` 查该后端注册的投影器（每类型每后端一次）。删 Lua = 删一个后端目录。

## 3. Checkpoints

每个 checkpoint 结束时全量 `ya-testing` 绿；旧路径在被替代的同一 checkpoint 删除，不留平行接口。

| id | 内容 | 依赖 |
| --- | --- | --- |
| RR0 | 行为基线：五个消费方的「类型 × 系统」矩阵入 `progress.md`；补 characterization 测试锁住现有行为（每种值类型的 JSON 往返、脚本 crossing、accessor 读写、拷贝）；脚本调用微基准（`callMethod` 各签名形态各跑 N 次）记基线 | — |
| RR1 | `reflects-core`：TypeOps 注册表 + facet 机制 + 开放 MetaValue（内建标量 + 内联开放分支）+ 单测。无消费方迁移 | RR0 |
| RR2 | 引擎层注册值类型：glm vec2/3/4、句柄、`ScriptRef`、`ScriptRect` 及其基础 facet（MetaValue 互转、比较、打印）；位置 `Core/Reflection` | RR1 |
| RR3 | 序列化器、`PropertyAccessor`、`ReflectionCopier` 改为查 TypeOps，删除各自 if 链；`trySerializeCustomType` 收编为 JSON facet；RR0 测试原样通过 | RR2 |
| RR4 | 编辑器检查器（`PropertyGraphBuilder` 等）改为查编辑器 facet | RR3 |
| RR5 | 动态调用 ABI：`Function::invoker` 改收发 MetaValue（reflects wrapper 生成器改造），`ArgumentList` 退出外部 ABI，`Function::payload` 删除（native 就是普通 Function，跳板塌缩进 invoker）；`ScriptValue` 被 MetaValue 取代，Lua 后端改为内建标量直映 + 开放分支查投影器；微基准回退 ≤ 5% | RR2 |
| RR6 | 最终成员视图下沉 reflects：继承展开、覆盖规则、owner path 由反射核心提供；`collectMembers` 只剩脚本名裁剪与可跨过滤 | RR5 |
| RR7 | 动态注册：注册表版本号 + 变更通知 + 注销；脚本导出表、编辑器反射缓存、序列化缓存订阅重建；public map 收为只读查询 | RR6 |
| RR8 | 脚本 native 迁移（原 S5.2 / S5.3）：`script::fn` 产出 MetaValue invoker；迁移剩余 9 个注册点（`LuaWidgetHandle` 8 + `GameplayScriptFunctions` 1）与 `Entity:call`；删除 `addNativeMethod`；GameRuntime 集中注册清单；字符串注册路径只剩 `ScriptMethodResolver` 动态场景 | RR7 |

RR3 / RR4 与 RR5 只共享 RR2 的注册结果，文件面不重叠，可在 RR2 后按冲突面安排；默认仍串行。

## 4. 边界

- `reflects-core` 保持零依赖：不 include glm、Core、Log、json。
- 层次原则：reflects 拥有类型身份、成员 schema、TypeOps 与 MetaValue 机制；各系统拥有自己的 facet 实现（JSON、编辑器、脚本投影）；脚本层拥有调用约定（receiver、owner 槽），签名推导不上移 reflects。
- 第一期非目标：容器值（`std::vector<T>` 等）不进 MetaValue，继续走 `ContainerProperty`；不做宏或代码生成之外的新生成链；`Generated/*` 只读。
- 不改场景文件格式：RR3 前后 JSON 字节级等价（RR0 往返测试锁定）。

## 5. 验收

- 五个消费方的按类型 if 链全部删除；新增一个值类型只需一处注册 + 各后端投影器（用一个测试类型走通全链路证明）。
- `std::any` / `ArgumentList` 不再出现在 reflects 的外部 ABI；`ScriptValue` 删除；`Function::payload` 删除。
- 加载后注册 / 注销一个类型的方法，脚本与编辑器在收到通知后看到变更（测试断言）。
- `reflects-core` 零依赖保持；全量 `ya-testing` 绿；脚本调用微基准回退 ≤ 5%；2D RPG / GreedySnake 冒烟正常。
