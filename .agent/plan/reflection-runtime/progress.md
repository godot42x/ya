# Progress

## 2026-10-08 — 立项

- 来源：S5.1 审查与用户复核暴露反射层与脚本层的结构问题（两套动态值、五处类型 if 链、`Function::payload` 盲盒、脚本层自算继承展开、public 可变注册表）。
- 方案问答：范围五处消费方全收；值模型开放 MetaValue 放 `reflects-core`（零依赖）；动态注册走版本号 + 通知；S5 暂停并入 RR8；P1，先于 C3 / G3.2。
- 用户纠正：动态注册是正当需求（Godot extension 加载后注册），不应以「冻结」规避；问题在没有版本与通知，不在动态本身。
- 下一步：RR0。

## 2026-10-08 — 计划 review 修订

- 消费方清单补全：编辑器 `EditorAutoPropertySection`（10）、`PropertyEditorMetadata`（4）、序列化器 / 拷贝器头文件模板分支、`LuaScriptLive` / `ScriptPropertyPreview`（ScriptValue）；RR0 改为全仓搜索出矩阵。
- 验证补 `reflects-core-test`、`reflects-generator-test`（使用 `ArgumentList`，不在 `ya-testing` 内）；Python 生成器去留在 RR5a 开工前定。
- RR5 拆为 RR5a（调用 ABI）/ RR5b（ScriptValue 替换）；关键路径定为 RR0→1→2→5a→5b→6→8，RR3 / RR4 / RR7 在关键路径外，避免拖住 C3 → S4 → G2.3。
- MetaValue（用户拍板）：内建种类含借用引用；三种存储形态（内联值 / 持有的盒子 / 借用引用）；reflects 补齐非 const 引用参数、输出参数、指针成员的动态访问（现状 `ArgumentList::get` 对 const any 做 any_cast，`T&` 编译不过、`const T&` 为拷贝）；安全句柄由引擎注册；裸借用引用不泄漏进脚本。
- `type_index`：实现跨 DLL、跨运行稳定，无需改；跨编译器值不同（Clang 实测 `glm::vec<3, float>` / `std::string`），边界加「永不持久化」，RR0 核查现状。
- 新增：facet 缺失策略、注册时哈希碰撞断言、按模块注销与线程模型（主线程帧边界注册、后台读快照）、`Example/` 场景重存零 diff、微基准不进 gtest 断言、三个 Windows 验证点、RR9 文档与 skill 吸收。
- 用户拍板：`ComponentJson::serializeCustom` 收编为 JSON facet；GUI 线 10F-6 / 10F-7 与 TypeOps 是同一件事，并入 RR4（避免编辑器另造一张平行注册表），10F-1~5 留 GUI 线先做，10F-8 排在 RR4 之后。

## 2026-10-08 — RR0 行为基线

生产代码未改。测试锁住的是现状，含下面「现有行为怪异」。

### 类型 × 系统

列：JSON 序列化 / PropertyAccessor / Copier / 脚本 crossing / 编辑器控件 / 编辑器元数据 / PropertyGraph / ComponentJson 自定义。

「支持」只算真正按值类型分支的路径。PropertyGraph 对每个反射成员都会出节点；下面 Graph 列写的是叶子可编辑，不是「有没有节点」。控件列是 `EditorAutoPropertySection::buildEditor` 的私有 switch（`EditorAutoPropertySection.cpp:898-920`），`ya-testing` 不能直接构造那条路径。

| 类型 | JSON | Accessor | Copier | 脚本 | 控件 | 元数据 | Graph 叶子 | ComponentJson |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| bool | 是 `ReflectionSerializer.cpp:100,170` | 是 `:88` | 是 `ReflectionCopier.cpp:242` | 是 `ScriptValue` variant | 是 `:917` | 否 | 是（叶子） | 是 `LuaScriptComponent.h:87,122` |
| int / int32_t | 是 `:91,158`（本机哈希相同） | 是 `:23,71` | 是 `:230` | 是，codec `:72`，进 Lua 为 int64 | 是 `:913` | 否 | 是 | 写路径只认 int64；读 number 进 int64 |
| uint32 / unsigned int | 是 `:106,178`（本机哈希相同） | 是 `:27,52,73` | 是 `:250` | 是，codec `:73` | 是 `:914` | 否 | 是 | 否 |
| int8 / uint8 / int16 / uint16 | 否，字段变 `null` | 否（`tryGet` 模板仍能读写） | 否，JSON null 后不拷贝 | 是，codec `:68-71`；int8 写入 300 截成 44 | 否 | 否 | 节点在，非叶子 | 否 |
| int64_t | 否，`null` | 否 | 否 | 是，`ScriptValue` 的整数臂 | 否 | 否 | 节点在 | 是 `:83,125` |
| long | 否 | 否 | 否 | 是，codec `:78`；`__PRETTY_FUNCTION__` 拼写与 `int64_t` 不同 | 否 | 否 | 节点在 | 否 |
| unsigned long / long long / unsigned long long | 否 | 否 | 否 | 是，codec `:79-81` | 否 | 否 | 节点在 | 否 |
| float | 是 `:94,162` | 是 `:86` | 是 `:234` | 是，codec `:74`，进 Lua 为 double | 是 `:910` | 是 `PropertyEditorMetadata.cpp:61` | 是 | 读 double；写走 double 臂 |
| double | 是 `:97,166` | 非叶子；`tryGet` 可读，`equals()` 无 double 分支，相等比较为 false | 是 `:238` | 是，`ScriptValue` double 臂 | 否 | 否 | 节点在，非叶子 | 是 `:85,127` |
| std::string | 是 `:103,174` | 是 `:89` | 是 `:246` | 是 | 是 `:920` | 否 | 是 | 是 `:89,129` |
| glm::vec2 / vec3 / vec4 | 是，`GLM.h:148-150` JSON 数组（读也接受 `{x,y,z}`） | 是 `:83-85` | 经 JSON 往返 | 是，Lua userdata `Vec2/3/4`，`.x/.y/.z` | vec2 `:901`，vec3 `:904`，vec4 `:907`；vec3/vec4 带 color 时走 `:898` | 是 `:90-96` | 是 | 是 `:91-96,131-135` |
| glm::quat / glm::mat4 | 否，`null`，反序列化不覆盖目标默认值（mat4 保持单位阵） | 否 | 否 | 否，导出跳过 | 否 | 否 | 节点在，非叶子 | 否 |
| 反射枚举 | 是，名字字符串 `:110-132` | 是，`getEnum` `:91` | 是 `:256` | 是，`EnumRegistry`，名字与整数都能回传 | 是 `:923` | 否 | 是 | 否 |
| vector / set（int、float、double、string、bool、vec3） | 是，元素走标量链 `:405-421,:859-875` | 容器，不是叶子 | 经 JSON | 否，导出跳过 | 否 | 否 | Sequence / 元素行 | 否 |
| map&lt;string,T&gt; / map&lt;int,T&gt; | 是，键只认这两种 `:262-279` | 键字符串化 `:233-239` | 经 JSON | 否 | 否 | 否 | Map + 子行 `:101-104` | 否 |
| map&lt;uint32,T&gt; | 非空时抛 `Unsupported map key type`，属性被省略；空 map 序列化为 `{}` | `mapKeyString` 有分支 `:239`，无调用方 | 经键指针拷贝，能保住 | 否 | 否 | 否 | 只有 Map 头；`getValuePtr`（`ContainerProperty.h:308`）不解析 uint32，子行被丢 | 否 |
| 资产引用（TextureRef 等） | 是，反射类，`{"__base__":{"AssetRefBase":{"_path":...}}}` | 是，`isAssetRefType` `:76` | 路径经 JSON | 否，导出跳过 | 是 `:926` | 否 | 是 | 否 |
| 指针成员（非类 pointee，如 `int*`） | 非空时写出 pointee 的数；反序列化不分配，目标保持 nullptr | 否 | 否 | 否，`collectMembers` 跳过 `bPointer`；`int*` 方法也不导出 | 否 | 否 | 视成员是否被反射 | 否 |
| ScriptRect | 否，`null` | 否 | 否 | 写出 Lua 表 `{x,y,w,h}`；任意表读不回 rect | 否 | 否 | 节点在 | 否 |
| ScriptRef | 不走值 if 链 | 否 | 否 | 是，注册的 ref kind | 否 | 否 | 否 | 否 |
| ScriptFnHandle / ScriptSelfHandle | 否 | 否 | 否 | 空句柄和未知 fn id 推 `nil` | 否 | 否 | 否 | 否 |
| optional&lt;float&gt; | 模板先解开再按类查找（`ReflectionSerializer.h:125`），标量链走不到；写出 `null`，读入数字得到 0 | 否 | 否 | 否 | 否 | 否 | 否 | 否 |

`mapKeyString` 的 bool 键分支（`PropertyAccessor.cpp:242`）没有调用方。PropertyGraphBuilder 的键链是 string / int / uint32（`:101-107`），没有 bool。

ScriptValue 消费文件（不含测试）：`ScriptValue.h`，`ScriptBindings.cpp`，`ScriptBindingsDetail.h`，`LuaScriptBinding.cpp`（`pushLuaValue` / `toScriptValue`），`LuaScriptLive.cpp`，`ScriptPropertyPreview.cpp`，`LuaScriptingSystem.cpp`，`LuaBackend.cpp`。

### `type_index_v<` 计数

排除 `Generated/`、`Engine/Test/`。`Engine/Programs` 为 0。下面是值类型分发；GUI 控件头里的 `getTypeIndex()` 是类身份，不是这张表的 if 链。

| 文件 | 次数 | plan §0 当时的数 |
| --- | --- | --- |
| `PropertyAccessor.cpp` | 38 | 35 |
| `ReflectionSerializer.cpp` | 31 | 29 |
| `ContainerProperty.h` | 16 | — |
| `EditorAutoPropertySection.cpp` | 13 | 10 |
| `ScriptBindings.cpp` | 13 | 7 |
| `ReflectionSerializer.h` | 11 | 5 |
| `PropertyGraphBuilder.cpp` | 6 | 5 |
| `ReflectionCopier.cpp` | 6 | 5 |
| `ReflectionCopier.h` | 6 | 5 |
| `AssetRef.cpp` | 6 | — |
| `PropertyEditorMetadata.cpp` | 4 | 4 |
| `ScriptBindingsDetail.h` | 3 | — |
| `PropertyAccessor.h` | 2 | — |

reflects-core 的 `type_index_v` 只做类 / 函数 / 属性身份：`class.h` 4、`registry.h` 3、`function.h` 1、`property.h` 1。最大的值类型 if 链文件是 `PropertyAccessor.cpp`（38）。

矩阵规模：22 行值类型 × 8 个系统。

### 场景 / 资产重存

哈希是对这段文本做 FNV-1a 64，取 16 位十六进制：读文件 → `SceneSerializer::deserialize` → `serialize()` → `normalizePaths` → `dumpJsonCompactLeaves`。第二轮再解析这段文本，断言幂等。不包含 `saveToFile` 私有的数字舍入。测试只读 Example，不写回。哈希字面量在 `ReflectionSceneResaveBaselineTest.cpp` 的 `kScenes` / `kHeavyScenes` / `kAnimationHashes` / `kUiHashes` / `kTilesetAtlasHashes`。

场景（16 个，全部进默认套件）。单文件两轮都 &lt; 2 s，最慢 `Sprites_5000` 1773 ms，所以没有 `DISABLED_` 场景。

- `Example/2dRpgPrototype/Content/Scenes/` House、PropertyOverride、Town、TownLarge
- `Example/GreedySnake/Content/Scenes/GreedySnake.scene.json`
- `Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json`
- `Example/PerfLab/Content/Scenes/` Lights_50、Lights_200、Lights_500、Meshes_1000、Meshes_5000、Meshes_10000、Sponza、Sprites_500、Sprites_2000、Sprites_5000

经反射重存的资产：

- `.yaanim.json`：Chest、Hero、Npc（`parseSpriteAnimationSetJson` / `serializeSpriteAnimationSetJson`）
- `.yaui.json`：2dRpg Dialogue；GreedySnake GameOver、HUD、Settings；HelloMaterial Panel1、Panel2（`UIDocument::fromJson` → instantiate → `fromWidget` → `toJson`）
- `.yatileset.json`：只哈希 atlas `TextureSlot`（2dRpg town、PerfLab town）。tileWidth / solid 等是手写解析，没有整份 tileset 序列化器

不经 ReflectionSerializer / ComponentJson：

- `.yaproject`：`FProjectDescriptor::load` 手写 JSON
- `.yamodule`：`FModuleManifest::load` 手写 JSON
- 未跟踪的 `Example/2DRpgPrototype/Content/UI/Panel1.yaui.json` 不在列表里

场景反序列化会因 VFS 找不到 `Content/Animations`、`Content/Tilesets` 打 `[Error]`。幂等仍然成立，测试不把这条日志当失败。

### 微基准

debug（`ya.py` 默认，日志里是 `compiling.debug`）。`DISABLED_`，100000 次，只打印。三次取中位数：

| 项 | run1 | run2 | run3 | 中位数 ns/op |
| --- | --- | --- | --- | --- |
| `lua_native_ns` | 861.3 | 748.6 | 757.4 | 757.4 |
| `lua_property_ns` | 568.7 | 565.6 | 564.5 | 565.6 |
| `invoker_ns` | 207.5 | 205.2 | 206.6 | 206.6 |
| `accessor_ns` | 30.0 | 30.6 | 31.6 | 30.6 |

`lua_native_ns` 是注册的 native `FBench:bump`，不是 `Entity:call`。手动：

```
xmake r ya-testing -- --gtest_also_run_disabled_tests --gtest_filter=ReflectionRuntimeMicrobench.*
```

### type_index 持久化

无。哈希不写入 JSON、缓存文件或资产元数据。`ReflectionCache` 只在内存里按 `typeIndex` 索引。场景组件键、UI `typeId`、资产 meta 的 `type` 都是字符串类名。序列化器把 `typeIndex` 当查找键，不放进 JSON。

### 测试通过数

`python3 Script/ya.py test --target ya`（debug）：1735/1735 通过，另有 4 个 `DISABLED_`（微基准，不计入）。上一轮基线 1717，增加的 18 个是 `ReflectionRuntimeBaseline` 10 个 + `ReflectionSceneResave` 8 个。没有旧测试失败。

`reflects-core-test`：43/43 通过（第一次记基线）。

`reflects-generator-test`：1/11 通过（`ReflectorTest.GetNonExistentClass`），10 失败。生成规则在 `generator-test/xmake.lua` 里被关掉（`add_files("./src/*.h", { rules = "reflects_generator" })` 注释掉），`intermediate/generates` 不存在，`Person` / `Vehicle` 没有注册。目标注释写明这些断言在规则恢复前必然失败。RR0 没改 reflects，这是现状基线，不是新回归。

冒烟：`Script/automation/2d-rpg/walk_one_cell.py` 退出码 0，`(0.5, 0.5) -> (1.5, 0.5)`，自动化日志无 `[Error]`。GreedySnake `--exit-after-frame=90` 退出码 0，日志无 `[Error]`。

### 偏离与现有怪异

偏离（不改生产代码）：

- 编辑器控件 switch 是私有的 `buildEditor`，单测覆盖到 PropertyGraph、`isLeafValueType` 和 `PropertyEditorMetadata::validationError`。vec 的 `ManipulateSpec` 不能经 `MetaBuilder::manipulate`（只接受算术类型）挂上；测试在进程内改 `Property.metadata` 才走到 vec 校验。
- 重存哈希不含 `saveToFile` 的私有数字舍入。
- 微基准走注册 native，不走 `Entity:call`。
- tileset 只锁 atlas；`.yaproject` / `.yamodule` 不经反射。

现有行为（测试照现状断言，不修）：

- JSON 标量只有 int、float、double、bool、string、unsigned int。更窄或更宽的整数、quat、mat4、ScriptRect 变成 `null` 并在加载时丢掉。
- 非空 uint32 map 被省略；空 map 写成 `{}`，所以往返 dump 不相等。Copier 仍能拷贝这份 map。
- `optional<float>` 走类查找，不走标量链。
- `null` 不写字段，目标保持默认（mat4 停在单位阵）。
- `PropertyAccessor::equals` 不比较 double。
- 非类指针写出 pointee，反序列化不分配。
- 脚本导出跳过指针、mat4、quat、资产引用、容器。ScriptRect 出去是表，进来被拒。未知 `ScriptFnHandle` id 推 `nil`。
- `ArgumentList::get<const T&>` 绑的是 `std::any` 里的副本；`get<T&>` 不能编译；`T*` 是指针值的副本。
- ComponentJson 对象键按 nlohmann 的有序 map 排列，不是插入序。实测脚本对象是 `enabled, executionOrder, propertyOverrides, scriptPath`。
- 序列化警告里的类型名是 Itanium `typeid().name()`，不是稳定的 `type_index` 拼写。
- 本机 Clang：`int == int32_t`，`unsigned int == uint32_t`，`long != int64_t`。

## 2026-10-08 — RR1 blocked（TypeOps / MetaValue 已进 reflects-core，引擎起不来）

代码在 `reflects-core`，没有改消费方，没有 commit。`reflects-core-test` 60/60（基线 43 + 新增 17）。`sizeof(MetaValue)` = 64，`alignof` = 16。Inline：可平凡拷贝且 `size <= 32`、`align <= 16`、`size > 0`。

`ya-testing` 在进 `main` 之前终止，退出码 255：

```
std::runtime_error: Reflection type name collision on 'CubemapSource'
```

`ClassRegistry::registerClass` 会给该类建一行 TypeOps，名字用类名。TypeRegistry 规定不同 `type` 不能共用一个名字。原先 `ClassRegistry` 对同名是后写覆盖 `classes[name]`，`typeIdMap` 里两份都留着。下面这些嵌套类型本来就共用反射名，静态初始化注册到第二个就抛：

| 名字 | 类型 |
| --- | --- |
| `CubemapSource` | `SkyboxComponent` / `EnvironmentLightingComponent` |
| `CylindricalSource` | 同上 |
| `AuthoringParams` | Phong / Unlit / PBR 的嵌套结构 |
| `SamplerConfig` | `TextureSlot.h` 与 `EditorPropertyGraphTest.cpp` |

场景 JSON 里没有这些字符串。按 RR1「不改引擎」没有改名，也没有把这条抛掉。冒烟没跑：runtime 会在同一处终止。

`reflects-generator-test`：1/11，通过的仍是 `ReflectorTest.GetNonExistentClass`，与 RR0 相同（生成规则休眠）。`xmake run` 退出码 1，来自那 10 个已知失败。

建议（二选一，本轮没做）：

1. 给这四组嵌套类型各起唯一反射名，名字唯一这条就成立。这是引擎改动。
2. 名字唯一只约束 `registerType` / `registerTypeRaw`。`registerClass` 仍建 Object 行，类名已被另一个 type 占用时沿用旧的后写覆盖，不抛。这是对「不同 type 抢同一个名字 → 抛」在 `registerClass` 路径上的放宽。

## 2026-10-08 — RR1 done_pending_windows

反射名唯一是硬规则。不同 type 抢同一个名字，启动仍然抛 `Reflection type name collision`，没有放宽 `registerClass`。引擎里原有的冲突已去掉：

| 原反射名 | 处理 |
| --- | --- |
| `CubemapSource` / `CylindricalSource` | Skybox 与 EnvironmentLighting 两份逐字相同的嵌套类型合成 `namespace ya` 顶层类型（`ECS/Component/3D/EnvironmentSource.h`）。反射名不变，`hasAllFaces` / `hasFile` 各留一份 |
| `AuthoringParams` | 仍嵌套，C++ 名改回 `AuthoringParams`，反射名用 `YA_REFLECT_BEGIN_NAMED` 显式指定（`PhongMaterialComponent::AuthoringParams` 等）。`Example/` 与 `Engine/Content` 没有持久化这个名字 |
| `SamplerConfig` | `EditorPropertyGraphTest` 的局部类型改为 `DeepNestedSamplerConfig`（`TextureSlot.h` 的那个保留） |

全仓 `YA_REFLECT_BEGIN` 再扫，没有别的重名。

验证（macOS debug）：

- `ya-testing` 1735/1735，另有 4 个 `DISABLED_`。`ReflectionSceneResave` 8/8，场景重存哈希未变。
- `reflects-core-test` 60/60。`reflects-generator-test` 1/11，通过的仍是 `ReflectorTest.GetNonExistentClass`。
- 冒烟退出码都是 0，日志没有 `[Error]`：`walk_one_cell.py` 走过一格 `(0.5, 0.5) -> (1.5, 0.5)`；GreedySnake 与 HelloMaterial `--exit-after-frame=90` 各在 90 帧退出。

Windows 验证点未跑。

### RR1 返工（状态仍是 done_pending_windows）

- TypeOps 生命周期只留 `MetaLife life`，并加上 `defaultConstruct`。`size` / `align` 只在 `MetaLife`。删掉顶层重复的函数指针和 `bindLife`，调用方走 `ops.life.*`。
- `convertTo`：`out` 指向已构造对象。同类型只对可平凡拷贝类型 memcpy；非平凡同类型返回 false（调用方用 `tryGet` / `materialize`）。
- `registerClass` 先调 `types.registerType<T>(name)`，不再自己查一遍冲突。抛异常前不改注册表，`classInfo` 仍由 `unique_ptr` 释放。
- 冲突信息带双方编译器类型名。`TypeOps::compilerName` 只用于诊断，由 `registerType<T>` 填，`registerTypeRaw` 可空，不得持久化。
- `-0.0` 转无符号整数视为 0（零一律非负）。
- `MetaValue` 里对平凡联合体的析构再 placement new 改为赋值；`reset()` 两个分支的字段复位合成一处。
- 反射名：`YA_REFLECT_BEGIN_NAMED` 把显式名下传到原来的注册路径（含 `YA_PROFILE_STATIC_INIT` 与 `___YA_REFLECT_EXTENSION`）。三个材质参数的 C++ 名回到 `AuthoringParams`。

RR5a 注意：`toInt64` / `convertTo` 等每次调用都查一次 TypeRegistry 哈希表。接入脚本调用路径前用微基准确认，必要时在 MetaValue 里缓存 TypeOps 指针。

验证（macOS debug，返工后）：

- `ya-testing` 1737/1737，另有 4 个 `DISABLED_`。`ReflectionSceneResave` 8/8，场景重存哈希未变。新增 `ReflectionNamedType` 2 个。
- `reflects-core-test` 61/61。`reflects-generator-test` 1/11，通过的仍是 `ReflectorTest.GetNonExistentClass`。
- 冒烟退出码都是 0，日志没有 `[Error]`：`walk_one_cell.py` 走过一格 `(0.5, 0.5) -> (1.5, 0.5)`；GreedySnake 与 HelloMaterial `--exit-after-frame=90` 各在 90 帧退出。

## 2026-10-08 — RR1 Windows 验证

用户在 Windows 上跑构建、`ya-testing`、`reflects-core-test`，均通过。RR1 done。

## 2026-10-08 — RR2 done

`ya::reflection::registerCoreValueTypes()`（`Core/Reflection/CoreValueTypes.h`）在 `DeferredInitializerQueue::executeAll()` 开头调用，幂等。测试也可直接调。消费方（序列化、PropertyAccessor、脚本、编辑器）没改。

登记名（kind 都是 `Object`）：

| 类型 | 名字 | sizeof | MetaValue |
| --- | --- | --- | --- |
| `glm::vec2` | `glm::vec2` | 8 | Inline |
| `glm::vec3` | `glm::vec3` | 12 | Inline |
| `glm::vec4` | `glm::vec4` | 16 | Inline |
| `glm::quat` | `glm::quat` | 16 | Inline |
| `script::ScriptRect` | `script::ScriptRect` | 16 | Inline |
| `script::ScriptRef` | `script::ScriptRef` | 32 | Inline |
| `script::ScriptFnHandle` | `script::ScriptFnHandle` | 8 | Inline |
| `script::ScriptSelfHandle` | `script::ScriptSelfHandle` | 8 | Inline |
| `glm::mat3` | `glm::mat3` | 36 | Boxed |
| `glm::mat4` | `glm::mat4` | 64 | Boxed |

`ScriptRef` 不是 24 字节：`type_index_t` 是 `uint64`，`kind`（`uint32`）后面补齐到下一个 `uint64`，一共 32。仍 ≤ 32 且可平凡拷贝，所以是 Inline。

facet（`Core/Reflection/ValueFacets.h`，没有 JSON / 编辑器 / 脚本投影）：

- `FEqualFacet`：`equals(const void*, const void*)`，`missing = Skip`。上面每个类型都登记，直接 `operator==`（含 NaN 不相等，不加误差）。
- `FPrintFacet`：`print(const TypeOps&, const void*)`，`missing = Fallback`。`fallback()` 用 `ops.name` 打出 `<TypeName>`。vec / quat / mat / ScriptRect 是括号分量（quat 按 `x,y,z,w`，mat 按列）；句柄是 `ref(kind, a, b)`、`fn(id)`、`self(runtimeId)`。

`glm::vec2/3/4` 与 `YA_REFLECT_BEGIN_EXTERNAL` 同名。`executeAll` 先登记值类型，再跑延迟的类注册；`registerType` 同类型同名返回已有行，类字段还在。quat / mat3 / mat4 没有类反射，名字用以后宏会展开的 `glm::quat` / `glm::mat3` / `glm::mat4`。

启动路径：`ya-runtime` 的 `App::init`、窗口 GUI 的 `GUIWindowHost::init` 都走 `AppBootstrap::initializeProcessCore()` → `executeAll`。`ya-testing` 和各 closure 测试的 `TestEntry` 也走 `executeAll`，测试文件不用额外 include。原先 `GUIHeadlessHost::init` 不刷新队列，现已补上同一次 `executeAll`（不设 Vulkan ICD）。`ShaderCompiler`、`reflects-*-test`、`ya-gui-workbench-workspace-test` 不链 Core 或不做反射。`test/*.cpp` 草稿程序自带 `main`、链 `ya-engine`，本来也不调用 `executeAll`（类反射同样不保证）；没有用静态初始化去绕。

验证（macOS debug）：

- `ya-testing` 1749/1749，另有 4 个 `DISABLED_`。基线 1737，多出的 12 个是 `CoreValueTypes.*`。`ReflectionRuntimeBaseline`、`ReflectionNamedType`、`ReflectionSceneResave` 8/8 原样通过，场景重存哈希未变。
- `reflects-core-test` 61/61。`reflects-generator-test` 1/11，通过的仍是 `ReflectorTest.GetNonExistentClass`。`reflects-core/src` 的 include 仍只有标准库和 reflects 自己的头。
- 冒烟退出码都是 0，日志没有 `[Error]`：`walk_one_cell.py` 走过一格 `(0.5, 0.5) -> (1.5, 0.5)`；GreedySnake 与 HelloMaterial `--exit-after-frame=90` 各在 90 帧退出。HelloMaterial 加载 `PlayerCamera.lua` 时 stdout 有两行既有的 sol2 panic（不是 `[Error]`，脚本随后打印 initialized）；本 checkpoint 没改脚本。

偏离：

- 派发里 ScriptRef 写成 24B，实测 32B，存储形态仍是 Inline。
- `plan.md` RR2 括号写句柄由脚本层注册；本任务指定在 Core 登记，句柄在 Core。
- MetaValue 互转没有单独 facet，走 `MetaValue::make` / `tryGet`。
- `ya-testing` 的 `main` 在用例前就 `executeAll`，真实 `glm::vec3` 的类注册发生在值类型登记之后。终态断言一行、名字 `glm::vec3`、类字段 `x/y/z` 还在。先类后 TypeOps / 先 TypeOps 后类用局部类型 `RR2ClassFirst` / `RR2TypeFirst` 覆盖同一条 `registerType` 契约。
- `registerCoreValueTypes` 第一次做完后直接返回，避免序列化路径上反复 `executeAll` 时重造 facet。

## 2026-10-08 — RR5a done

调用 ABI 收发 `MetaValue`。`ArgumentList` / `argument_list.h` 删除。`Function::payload` 删除。`script::fn` 是普通 `Function`，`callMethod` 走 `invoker`。`addNativeMethod` 和 9 个手写 native 仍走 `ScriptMethod.native`，没迁（RR8）。生成器保持休眠，没改 `generator/`。

### ABI

- `Function::invoker`：`std::function<MetaValue(void* self, std::span<MetaValue> args)>`。span 指向调用方栈上的数组，可写。值返回 `MetaValue::make`；`T&` / `T*` 返回借用引用（const 保留）；void 返回空。
- 形参描述 `ParamDesc`（`args`）取代 `argTypeIndices`。每项：`type`（decay，指针取 pointee）、`bRef`（只表示非 const 引用）、`bPointer`、`bConst`。返回值同样，void 的 `ret.type` 为 0。`const T&` 与按值 const 都是 `bConst`，不设 `bRef`。
- 绑定：按值 / `const T&` 确切类型直接读（`const T&` 不拷贝）；算术类型不匹配走 `convertTo`，越界抛异常。非 const `T&` 要求非 const 的确切值或非 const Ref，const Ref 抛异常。`T*` / `const T*` 接受 Ref（`data()==nullptr` 为空）或空 MetaValue；const 正确传播。个数不匹配抛 `Argument count mismatch`。
- `Class::invoke` / `invokeStatic` 返回 MetaValue。类型化 `invoke<R>` / `invokeStatic<R>` 返回 R（原先的 `call` / `callStatic` 换名，不留平行接口）。装箱：右值 / 按值 → `make`；非 const 左值 → `ref`；const 左值 → `cref`；`T*` → 指向 T 的 Ref。
- 构造：`ctor.factory` 收 `span<MetaValue>`。`createInstance` 内部装箱。
- `Property::ref(void*)` 是成员存储的借用引用。`bPointer` 的 `pointee(void*)` 指向 pointee（空指针 → `data()==nullptr`），写入落在目标对象上。`pointeeTypeIndex` 没改。
- `TypeRegistry` 的 TypeOps 改成 `unique_ptr` 持有，注册表 rehash 不会让缓存的 `TypeOps*` 悬空。

脚本导出：含 `bRef` 或 `bPointer` 形参、或返回非 const 引用 / 指针的方法继续跳过。`const T&` 参数和 `const T&` 返回仍导出，边界上按 `data()` 拷进 `ScriptValue`。`poke` 仍跳过，`observe` / `getPosition` / `getName` 仍导出。

### 行为变更

- 窄化写入越界改为抛 `ScriptError("value is out of range")`，不再截断。基线 `tiny`（int8）写入 300 以前变成 44，现在抛异常且值保持 3。`scriptToInteger` / `scriptToNumber` / `scriptToBool` 仍先跑，类型错误文案不变。
- `const T&` 不再拷贝。非 const `T&`、`T*`、返回引用 / 指针的写入落在原对象上。
- `script::fn` 的脚本调用不再绕过 invoker。替换 `labelOf.invoker` 后 `callMethod` 会抛（以前 payload 跳板仍返回原结果）。非 const 的 `script::fn` 会触发 `afterCall`。生产代码没有 `script::fn`，`addNativeMethod` 的 afterCall 行为不变。

### 微基准

debug，100000 次，3 次取中位数，ns/op。`invoker_ns` 每次新建一个 `MetaValue`。

| | RR0 | RR5a 三次 | 中位数 | 相对 RR0 |
| --- | --- | --- | --- | --- |
| `lua_native_ns` | 757.4 | 801.4 / 805.0 / 813.4 | 805.0 | +6.3% |
| `lua_property_ns` | 565.6 | 580.8 / 586.5 / 586.7 | 586.5 | +3.7% |
| `invoker_ns` | 206.6 | 63.3 / 63.4 / 63.9 | 63.4 | −69% |
| `accessor_ns` | 30.6 | 29.7 / 30.9 / 31.3 | 30.9 | +1.0% |

第一次测量 `lua_native_ns` 中位数 948.1（+25%）。热路径默认构造了 8 个用不到的 MetaValue，并且 int64→int 的窄化每次调用出线 `convertTo`。改成只构造实际参数，窄化在 ScriptBindings 里按 `convertTo` 的 int64 / double 规则内联（越界仍抛，不查 TypeRegistry）。重测后回到 +6.3%，没有再改。

### 验证

- `ya-testing` 1749/1749，另有 4 个 `DISABLED_`。`ReflectionSceneResave` 8/8，场景重存哈希未变。
- `reflects-core-test` 70/70（基线 61，新增 `InvokeAbi` 9 个）。
- `reflects-generator-test` 1/11，通过的仍是 `ReflectorTest.GetNonExistentClass`。只改了 `CallMethod` 的测试源让它能编过。
- `ArgumentList`、`Function::payload` 在 `reflects/core` 和 `Engine/Source` 已删除（剩下的 `.payload` 是纹理 / drag-drop，与反射无关）。`argument_list.h` 不存在。`reflects/core/src` 的 include 仍只有标准库和 reflects 自己的头。
- 冒烟退出码都是 0，日志没有 `[Error]`：`walk_one_cell.py` 走过一格 `(0.5, 0.5) -> (1.5, 0.5)`；GreedySnake 与 HelloMaterial `--exit-after-frame=90` 各在 90 帧退出。HelloMaterial 仍有两行既有的 sol2 panic（不是 `[Error]`）。第一次 `walk_one_cell` 在重建 `ya-runtime` 之前挂了 120 秒（日志为空）；`ya.py run` 重建 runtime 后重跑通过。

偏离：

- `bRef` 只标记非 const 引用。若连 `const T&` 返回也跳过，`getPosition` / `getName` / `getLabel` 会掉出导出集合，RR0 的 findMethod 和 Lua 冒烟会变。const 引用在脚本边界拷贝，借用引用不进脚本。
- 枚举不是 TypeOps。脚本边界用 `Enum::setValue` 写入 8 字节，再 `MetaValue::trivial`；读回用 `Enum::getValue`。
- 脚本窄化的热路径不调用出线的 `convertTo`，规则与 int64 / double 源的 `convertTo` 相同。其它来源的算术转换仍走 `convertTo`。
- 生成器按派发保持休眠，没有恢复。

## 2026-10-09 — RR5b done on macOS, Windows pending

`ScriptValue` 不再是 variant，就是 `refl::MetaValue`。脚本读写、`callMethod`、Lua / JS 边界都直接收发它。`addNativeMethod` 和 9 个手写 native 仍在（RR8）。

### 边界

- 内建种类按 `EValueKind` 直映：空 → nil，Bool → boolean，Int → integer，Float → number，String → string。窄整数和 float 在进出脚本时仍收成 int64 / double，越界抛 `ScriptError`。
- 其它可跨类型走 Lua 投影器表（`type_index` 查一次）：vec2/3/4、`ScriptRef`、`ScriptRect`、函数句柄、self 句柄。JS 仍拒绝后三个，向量打成数组。
- `pushLuaValue` / JS `toJs` 遇到借用引用（`isRef()`）直接拒绝，不把指针推进脚本。字段读取和 `const T&` 返回在边界上拷成自有值后再推。
- 指针成员仍不导出。裸指针没有 `ScriptRef` kind，做不成安全句柄，按「转成句柄或拒绝」里的拒绝处理。`raw` 的 `findField` 仍是空。
- `MetaValue` 增加 `operator==`（同类型、可平凡拷贝比字节，字符串比内容）。

### 微基准

debug，100000 次，3 次，ns/op。对照 RR0。

| | RR0 | RR5b 三次 | 中位数 | 相对 RR0 |
| --- | --- | --- | --- | --- |
| `lua_native_ns` | 757.4 | 793.6 / 838.3 / 795.5 | 795.5 | +5.0% |
| `lua_property_ns` | 565.6 | 577.6 / 584.7 / 588.7 | 584.7 | +3.4% |
| `invoker_ns` | 206.6 | 63.6 / 65.1 / 65.5 | 65.1 | −68% |
| `accessor_ns` | 30.6 | 30.1 / 33.4 / 33.6 | 33.4 | +9.2% |

`lua_native` 从 RR5a 的 805.0 降到 795.5，贴着 5% 线（一次 838 把中位数抬上去）。`accessor` 不经过脚本边界，三次里有 30.1，debug 抖动，没有为它改代码。

### 验证

- `ya-testing` 1749/1749，4 个 `DISABLED_`。
- `reflects-core-test` 70/70。
- Windows 还没跑。重点：`long` 32 位、`reflects-core` 导出、`MetaValue::operator==`、designated initializer 顺序。
