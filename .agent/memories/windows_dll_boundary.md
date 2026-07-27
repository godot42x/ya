# Windows DLL Boundary

适用场景：

- Windows 下从 `static lib -> shared lib -> exe/dll` 这条链运行正常编译但运行时行为异常
- 同一套头文件、模板、单例、全局状态在多个 DLL 中各有一份副本
- 典型症状包括：ImGui `GImGui == 0`、反射注册表查不到类型、编辑器面板空白、序列化/复制找不到类、回调跨模块失效

## 结论先行

1. Windows 下 DLL boundary 不是“链接能过就安全”。
2. 只要某个库含有全局状态、单例、静态局部变量、注册表、上下文对象、RTTI 依赖、分配释放跨模块配对，这个库就不能被多个 DLL 各自静态编进去然后期望共享状态。
3. 无状态算法库可以静态链接进 shared library；有状态 runtime 库必须有唯一 owner。
4. YA 当前已经验证过两类高风险对象：
   - ImGui / ImGuizmo 这类上下文型库
   - reflects-core / ECSRegistry / 延迟反射注册 这类注册表型库

## 常见坑点

### 1. 每个 DLL 各自拥有一份全局状态

典型表现：

- `imgui.cpp` 里的 `GImGui` 在 engine DLL 创建了 context，但 editor DLL 里看到的是另一份 `GImGui`
- `ClassRegistry::instance()`、`EnumRegistry::instance()`、`ECSRegistry::get()` 在不同模块中各有一份 map

根因：

- 同一个 static library 被多个 shared library 分别链接进去
- 头文件里的 `inline` / 模板 / `static` 局部单例在多个模块各自实例化

处理办法：

1. 对有状态 runtime，明确唯一 owner。
2. 要么把实现集中进一个 shared library，由其他模块 `dllimport`。
3. 要么根本不跨边界暴露该第三方 API，只通过宿主模块封装接口访问。

### 2. 只导出了类型，没导出状态归属点

典型表现：

- 类声明上加了 `__declspec(dllexport)`，但真正决定状态归属的 `instance()` / `get()` / 静态局部对象仍然在多个模块里各自产生

处理办法：

1. 导出边界要盯住“状态创建点”，不是只盯类声明。
2. 所有单例入口、注册表入口、全局 helper、静态初始化队列都要统一落到唯一 DLL。

### 3. 头文件模板把注册逻辑复制进所有模块

典型表现：

- 反射宏在 engine、editor、project module 里都触发一遍
- 运行时日志里出现重复注册，或同一个类型名在不同模块里注册顺序不一致

处理办法：

1. 接受“注册代码在多模块执行”这件事，但注册表本体必须唯一。
2. 注册 API 必须幂等：重复注册同一 `type_index` 和同名类型时直接复用，不要再创建第二份状态。
3. 若不同名字撞同一 `type_index`，应显式报错，而不是静默覆盖。

### 4. 静态初始化顺序跨 DLL 不可控

典型表现：

- 某模块 `onLoad` 后类型已经存在，但 deferred initializer 还没执行
- 场景反序列化、组件复制、编辑器检查器比注册表初始化更早发生

处理办法：

1. 不依赖跨模块 static initialization 顺序。
2. 用显式的 deferred queue，在 app 生命周期里统一 drain。
3. 队列本身也必须是唯一实例，不能各 DLL 一份。

### 5. 运行时正常但 editor UI 空白

典型表现：

- 组件存在，但 `ClassRegistry::instance().getClass(typeIndex)` 返回空
- `renderReflectedType()` 没东西可画

处理办法：

1. 先区分“组件没注册”还是“类反射没注册”。
2. 看 `ECSRegistry`、`ClassRegistry` 是否属于同一个状态域。
3. 对无字段类型，UI 也要显示明确占位，不要因为 `propertyCount == 0` 就什么都不渲染。

### 6. 第三方 demo / optional symbol 被链接器裁剪

典型表现：

- 只有在某个 DLL 中调用过的符号才会被拉入，别的模块虽然声明了 `dllimport` 但宿主 DLL 并未真正导出实现

处理办法：

1. 优先在 owner DLL 中显式编入需要的源文件，而不是靠伪调用保活。
2. 本仓库里，`imgui_demo.cpp` 现在直接由 `ya-engine` 编译，避免 `ShowDemoWindow()` 依赖链接 hack。

## 这次在 YA 遇到的具体问题

### ImGui / ImGuizmo

症状：

- macOS 正常，Windows editor 启动时 `GImGui != 0` 断言失败

根因：

- `ya-engine.dll` 和 `ya-editor.dll` 同时各自持有一套 ImGui 静态状态

处理：

1. `ya-editor` 不再直接拥有 ImGui 实现对象。
2. Windows 下由 `ya-engine` 导出 `IMGUI_API` / `IMGUI_IMPL_API`。
3. `ya-editor` 改为 `dllimport` 同一套 ImGui 符号。
4. `imgui_demo.cpp` 明确编进 `ya-engine`，不再靠运行时假分支保活。

### 反射注册表 / Inspector fallback

症状：

- `renderReflectedFallback` 路径下组件存在但不显示字段
- 日志出现 `ReflectionSerializer: Class '...' not found in registry`

根因：

- 反射 runtime 原本是 static lib，多模块各有注册表/类型表副本
- 注册逻辑在多个模块都会执行，但 registry 不统一

处理：

1. `reflects-core` 改为 shared library。
2. `ClassRegistry` / `EnumRegistry` 等 runtime 类型走 `REFLECTS_CORE_API`。
3. `ya-engine` 公共依赖 `reflects-core`，项目模块与 editor 统一导入同一份注册表。
4. 注册 API 调整为幂等，允许相同类型重复注册但禁止异名碰撞。
5. `ECSRegistry::registerComponent` 改为幂等，避免重复覆盖和泄漏。
6. `renderReflectedType()` 对“有反射类型但无字段”的情况显示 `[no editable reflected fields]`，避免 Inspector 直接空白。

## 如何判断一条链接链路是否合理

### 合理的情况

- 纯算法库、数学库、header-only 工具库
- 没有全局注册表、没有隐式上下文、没有跨模块 new/delete 配对
- 只作为 `ya-engine.dll` 的内部实现细节，不向其他 DLL 暴露其 ABI

例子：

- glm、部分 utility 库、无状态 helper

### 不合理的情况

- 上层 DLL 和下层 DLL 都会直接调用该库 API
- 库内部含单例 / registry / static local / thread local / global caches
- 库对象在 A 模块创建、在 B 模块销毁
- 库通过模板和 inline 函数把状态散布到多个模块

例子：

- ImGui
- ImGuizmo
- reflects-core
- 任何“注册表 + 宏注册 + 静态初始化”体系

## 推荐处理策略

### 策略 A：唯一 shared owner

适用：

- 第三方库本身就是状态ful runtime

做法：

1. 只让一个 DLL 编译/持有实现。
2. 其他模块统一 `dllimport`。
3. 所有状态入口函数都从该 DLL 导出。

### 策略 B：边界封装，外部不直接碰第三方 ABI

适用：

- 不希望 editor / project module 直接依赖第三方 ABI

做法：

1. 在 engine 内部封装成自己的接口。
2. 外部模块只调用 engine API，不 include 第三方头。

评价：

- 最干净，但改造成本更高。

### 策略 C：幂等注册 + 显式生命周期

适用：

- 宏注册、模板注册无法彻底避免多模块执行

做法：

1. 注册函数对同一 key 幂等。
2. 队列统一 drain。
3. 冲突只对“同 key 异义”报错。

## 排查 checklist

1. 这个库有没有单例、全局变量、静态局部变量、thread_local、registry、context？
2. 它是不是被多个 DLL 同时静态链接？
3. 当前异常是编译期、链接期，还是运行期状态不一致？
4. 真正的状态 owner 在哪一个模块？是否只有一个？
5. `dllexport/dllimport` 是否加在了真正的边界符号上，而不只是类型声明上？
6. 头文件模板/inline 是否在多个模块各自实例化了状态？
7. 静态初始化是否依赖模块加载顺序？
8. 是否需要把“重复注册”从错误改为幂等处理？
9. UI 空白时，是否只是“0 个字段”而不是“没注册”？
10. 链接器是否裁掉了某些只在 editor 中间接使用的目标源文件？

## 当前仓库落地规则

1. `imgui-local` / `imguizmo-local` 只作为 `ya-engine` 的实现依赖，不要再让 `ya-editor` 直接拥有第二份 ImGui runtime。
2. `reflects-core` 保持 shared；不要再退回 static，除非整套反射系统改成明确的宿主封装模型。
3. 涉及注册表、单例、延迟初始化队列的库，优先按“唯一 owner + 幂等注册”设计。
4. 遇到 Windows-only 正常编译、异常运行，优先从 DLL boundary 查，而不是先怀疑业务逻辑。

