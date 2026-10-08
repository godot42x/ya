# reflection-runtime — 反射层与脚本层整体规范化

2026-10-08 立项，`showcase-roadmap` U 组，P1，排在 C3 与 G3.2 之前（两者都建在脚本绑定上）。
吸收 `script-native-registry/`（S5）：S5.1 已入库部分保留，S5.2 / S5.3 并入本线 RR8。
吸收 `gui-framework-editor-readiness/` Phase 10F-6 / 10F-7（编辑器能力注册表），实现为 RR4 的编辑器 facet。

## 0. 现状（2026-10-08 盘点，RR0 用全仓搜索复核）

- **各消费方各写一份按类型分支的 if 链**（`type_index_v<...>` 次数）：`PropertyAccessor.cpp` 35、`ReflectionSerializer.cpp` 29（另有 `.h` 5）、编辑器 `EditorAutoPropertySection.cpp` 10、`ScriptBindings.cpp` 7、`ReflectionCopier.cpp` 5（另有 `.h` 5）、`PropertyGraphBuilder.cpp` 5、`PropertyEditorMetadata.cpp` 4。加一个新值类型（如 `glm::quat`）要改七处以上。
- **已有的半成品扩展点**：序列化器 `trySerializeCustomType`；`ECS/ComponentJson` 的按组件 `serializeCustom`（W2）。
- **两套动态值并存**：反射调用 ABI 是 `std::any` + `ArgumentList`；脚本是封闭 variant `ScriptValue`（glm 向量、句柄、`ScriptRect` 写死成分支）。`Function::invoker` 引擎内只有 `ScriptBindings` 调用；`ArgumentList` 另被 `reflects/core-test`、`reflects/generator-test` 使用（不在 `ya-testing` 内）。`ScriptValue` 消费方 25 个文件，含 `LuaScriptLive.cpp`、`ScriptPropertyPreview`。
- **引用语义缺失**：`ArgumentList::get` 对 const `std::any` 做 `any_cast<T>`——非 const 引用参数 `T&` 编译不过，`const T&` 实为拷贝，`T*` 只能当裸指针值；无输出参数。脚本导出跳过全部指针成员（`property.bPointer`）。
- **reflects 无扩展机制**：S5.1 为挂脚本跳板加了 `Function::payload`（`std::any` 盲盒）。
- **reflects 无最终成员视图**：继承展开、覆盖规则、owner path 由脚本层 `collectMembers` 自己算。
- **注册表是公开可变容器**：`Class::functions` / `properties` 为 public map，无版本、无通知；模块卸载无清理（`ModuleManager.cpp:261`：镜像常驻到进程退出，dlclose 前需按 owner 清理注册表）。
- `type_index` 为编译期类型名 FNV 哈希（`type_index.h`）：跨 DLL、跨运行稳定；跨编译器不同（Clang 实测 `glm::vec<3, float>`、`std::string`，MSVC `__FUNCSIG__` 带 `struct`/`class` 前缀且展开默认模板参数）。
- `reflects-core` 是 shared 库，零依赖（只有测试依赖 gtest），要保住。仓内有 Python 生成器 `reflects/generator/`，引擎用宏注册，只有 generator-test 用它。
- `Core/Reflection/README.md` 过时（写的是 `YA_REFLECT(... PROP ...)` / `UnifiedReflection.h`，实际是 `YA_REFLECT_BEGIN`）。

## 1. 决策（2026-10-08 用户拍板）

| 议题 | 决定 |
| --- | --- |
| 范围 | §0 列出的全部消费方：反射、脚本、序列化、`PropertyAccessor`、拷贝、编辑器检查器 |
| 值模型 | 开放 MetaValue，与 TypeOps 一起放 `reflects-core`，零依赖。三种存储形态见 §2.3。有意不同于 Godot 的封闭 Variant：换「加类型不改核心」 |
| 引用语义 | reflects 必须支持指针与引用：非 const 引用参数、输出参数、指针成员的动态访问。经 MetaValue 的借用引用实现 |
| 动态注册 | 加载后可注册 / 注销（Godot extension 式），每次变更提升版本号并发通知，消费方重建缓存 |
| 组件级自定义序列化 | `ComponentJson::serializeCustom` 与 `trySerializeCustomType` 一起收编为 TypeOps 上类类型的 JSON facet |
| 编辑器能力注册表 | GUI 线 10F-6 / 10F-7 并入 RR4，实现为 TypeOps 上的编辑器 facet；10F-1~5 留 GUI 线先做；10F-8 排在 RR4 之后 |
| S5 | 暂停 S5.2；S5 剩余部分并入 RR8；S5.1 保留 |
| 优先级 | P1，排在 C3 与 G3.2 之前 |

## 2. 目标形态

### 2.1 四层

1. **reflects（schema）**：类型身份、成员、继承；完整描述指针 / 引用类型的成员与参数；提供最终成员视图（继承展开、覆盖、owner path 算好）；注册表带版本号、变更通知、按所属模块注销；对外只读查询，写入走注册 API。不认识任何上层值类型。
2. **TypeOps（每类型行为表，reflects-core）**：每个类型一行，挂各系统的能力（facet）：与 MetaValue 互转、JSON 读写、拷贝、比较、打印、编辑器控件。facet 由拥有它的系统注册（JSON 由序列化层，编辑器由编辑器），reflects 只存不解释。加一个新类型 = 在一处注册它的能力。
3. **MetaValue（运行时值 ABI，reflects-core）**：反射动态调用的参数与返回值、脚本中立值、自动化动态面。`Function::invoker` 直接收发它；`std::any` / `ArgumentList` 退出外部 ABI；`ScriptValue` 被它取代。
4. **语言后端（Lua / 未来 JS）**：只做投影。内建种类直接映射；其他类型按 `type_index` 查该后端注册的投影器（每类型每后端一次）。删 Lua = 删一个后端目录。

### 2.2 层次原则

reflects 拥有类型身份、成员 schema、TypeOps 与 MetaValue 机制；各系统拥有自己的 facet 实现（JSON、编辑器、脚本投影）；脚本层拥有调用约定（receiver、owner 槽）与暴露策略，签名推导不上移 reflects。

### 2.3 MetaValue

- **内建种类**：空、bool、整数（int64）、浮点（double）、字符串、借用引用。
- **三种存储形态**：
  1. **内联值**：可平凡拷贝且 ≤ 32 字节的已注册类型（vec / quat / 句柄 / 矩形），拷贝即 memcpy，热路径。
  2. **持有的盒子**：任意已注册类型，堆上存储，拷贝 / 析构经 TypeOps。覆盖按值返回结构体或容器（原 `std::any` 承担的部分）。
  3. **借用引用** `{type_index, void*, bConst}`：指针与引用语义。`T&` / `T*` 参数绑定到调用方对象，写入落在原对象上；支持输出参数；读指针成员得到可为空的借用引用。只在当前调用内有效。
- **安全句柄不是内建**：`ScriptRef`（ref kind 解析）、带代数检查的实体句柄等由引擎层注册为值类型；长期持有引用必须用它们。
- **脚本暴露策略**（脚本层）：裸借用引用不得泄漏进脚本状态，转成安全句柄或拒绝。

### 2.4 facet 缺失策略

按 facet 规定、注册时写明，不留隐式默认：JSON facet 缺失 → 序列化报错（`YA_CORE_ERROR` + 跳过该字段），不静默；编辑器 facet 缺失 → 只读文本显示；脚本投影器缺失 → 该成员不导出（与现在不可跨类型被跳过一致）。

## 3. Checkpoints

每个 checkpoint 结束时全量 `ya-testing`、`reflects-core-test`、`reflects-generator-test` 绿；旧路径在被替代的同一 checkpoint 删除，不留平行接口。

**关键路径**（C3 / G3.2 只依赖这条）：RR0 → RR1 → RR2 → RR5a → RR5b → RR6 → RR8。RR3、RR4、RR7 在关键路径之外，RR2 之后按文件冲突面安排；RR4 还要等 GUI 线 10F-1~5。

| id | 内容 | 依赖 |
| --- | --- | --- |
| RR0 | 行为基线：全仓搜索出「类型 × 系统」矩阵入 `progress.md`；characterization 测试锁住每种值类型的 JSON 往返、脚本 crossing（含 Lua 侧可见形态，如 vec3 的 `.x`）、accessor 读写、拷贝；`Example/` 全部场景与资产元数据重存一遍记基线；脚本调用微基准（固定迭代，`DISABLED_` 前缀手动跑，只输出不断言）记基线；核查现有代码没有持久化 `type_index`；记录 reflects 两个测试 target 的通过数 | — |
| RR1 | `reflects-core`：TypeOps 注册表 + facet 机制（含 §2.4 缺失策略）+ MetaValue（§2.3 内建种类与三种存储形态）+ 注册时哈希碰撞断言（同一哈希对应不同名字）+ `reflects-core-test` 单测。无消费方迁移。**Windows 验证点** | RR0 |
| RR2 | 引擎层注册值类型：glm vec2/3/4、`ScriptRef`、`ScriptFnHandle`、`ScriptSelfHandle`、`ScriptRect` 及基础 facet（MetaValue 互转、比较、打印）；位置 `Core/Reflection`（句柄类由脚本层注册） | RR1 |
| RR3 | 序列化器、`PropertyAccessor`、`ReflectionCopier`（含 `.h` 模板分支）改为查 TypeOps，删除 if 链；`trySerializeCustomType` 与 `ComponentJson::serializeCustom` 收编为 JSON facet；RR0 测试原样通过；`Example/` 全部场景与元数据重存后与基线零 diff | RR2 |
| RR4 | 编辑器 facet（并入 GUI 线 10F-6 / 10F-7）：`EditorAutoPropertySection`、`PropertyEditorMetadata`、`PropertyGraphBuilder` 改查编辑器 facet；颜色 / 操纵 / 校验 / 资产选择器能力，以及相等、整数、枚举、容器 key、资产引用能力注册为 facet | RR2、GUI 10F-1~5 |
| RR5a | 调用 ABI：`Function::invoker` 收发 MetaValue；reflects 包装器经借用引用绑定 `T&` / `T*` / 输出参数；`ArgumentList` 退出外部 ABI；`Function::payload` 删除（native 即普通 Function，跳板塌缩进 invoker）；`reflects-core-test` / `generator-test` 随之更新；`ScriptValue` 与 MetaValue 的转换暂时只在 `ScriptBindings` 内；微基准对照。**开工前定 Python 生成器去留（同步改或退役）** | RR2 |
| RR5b | `ScriptValue` 被 MetaValue 取代（25 个文件，含 `LuaScriptLive`、`ScriptPropertyPreview`）；Lua 后端内建种类直映 + 其他类型查投影器；借用引用不泄漏进脚本的策略；脚本可访问指针成员（经安全句柄）；微基准回退 ≤ 5%。**Windows 验证点** | RR5a |
| RR6 | 最终成员视图下沉 reflects：继承展开、覆盖规则、owner path 由反射核心提供；`collectMembers` 只剩脚本名裁剪与可跨过滤 | RR5b |
| RR8 | 脚本 native 迁移（原 S5.2 / S5.3）：`script::fn` 产出 MetaValue invoker；迁移 `LuaWidgetHandle` 8 + `GameplayScriptFunctions` 1 + `Entity:call`；删除 `addNativeMethod`；GameRuntime 集中注册清单；字符串注册路径只剩 `ScriptMethodResolver` 动态场景。**Windows 验证点** | RR6 |
| RR7 | 动态注册：每条注册记录所属模块；按模块整批注销后通知；注册表版本号；脚本导出表、编辑器反射缓存、序列化缓存订阅重建；public map 收为只读查询 | RR6 |
| RR9 | 文档：重写 `Core/Reflection/README.md`；层次原则、MetaValue 形态、facet 规则、type_index 不持久化写进 skill（按归档规则，吸收后才能归档本线） | RR7、RR8 |

## 4. 边界

- `reflects-core` 保持零依赖：不 include glm、Core、Log、json；xmake 不新增 deps / packages。
- **`type_index` 永不持久化**：场景、缓存、资产元数据只存反射注册的类名；`type_index` 只作进程内运行时身份。
- **线程模型**：注册与注销只在主线程的帧边界进行；通知在主线程派发；后台线程读注册表须读版本快照。
- 第一期非目标：容器值的动态操作继续走 `ContainerProperty`（容器作为整体可装进持有的盒子）；不新增宏或代码生成链；`Generated/*` 只读。
- 不改场景文件格式：RR3 前后 JSON 逐字节等价。
- **Windows 验证点**（RR1、RR5b、RR8）：请用户在 Windows 上跑构建与 `ya-testing`。重点：`long` 为 32 位（LLP64）、`reflects-core` shared 库的导出、designated initializer 顺序。

## 5. 验收

- §0 列出的全部按类型 if 链删除；新增一个值类型只需一处注册 + 各后端投影器（用一个测试类型走通全链路证明）。
- 非 const 引用参数、输出参数、指针成员可经反射动态调用 / 读写，写入落在原对象上（测试断言）。
- `std::any` / `ArgumentList` 不在 reflects 外部 ABI；`ScriptValue`、`Function::payload` 删除。
- 加载后注册一个类型的方法、按模块注销，脚本与编辑器收到通知后看到变更（测试断言）。
- `reflects-core` 零依赖保持；三个测试 target 全绿；`Example/` 场景重存零 diff；微基准回退 ≤ 5%；2D RPG / GreedySnake 冒烟正常；Windows 三个验证点通过。
