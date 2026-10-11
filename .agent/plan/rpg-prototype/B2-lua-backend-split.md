# B2 — Lua 后端拆成独立 target

只读设计，2026-10-05 按当前源码核对。默认按下面的推荐执行；§6 里标了「开工前要拍板」的点，B2-1～B2-4 不依赖那些答案，B2-5 起依赖。

验收命令（最终，不是每个 checkpoint）：

```bash
rg -n "sol/|sol::" Engine/Source --glob '!**/Script/Lua/**'
```

无输出。GreedySnake / 2DRpgPrototype 冒烟与脚本测试全绿。语义冻结：不改 `world.*` / `ui.*` / `onKey` 的行为；那些语义归 `game-ui-script-framework` S5 / S7，动到同一函数时 S5 先落地（`rpg-prototype/plan.md` §5）。

## 0. 结论

1. **sol 是从头文件漏进引擎的，不是只活在 16 个 cpp 里。** `LuaScriptInstance.h` 直接 `#include <sol/sol.hpp>`，`LuaScriptComponent.h` 又 include 它。场景序列化、编辑器、`HelloMaterial`、`entityAt` 都因此看到 sol。`LuaScriptComponent.h` 里那句注释 `sol::object / sol::function` 自己就会被验收 rg 命中，必须改掉。
2. **新 target 叫 `ya-script-lua`，目录 `Engine/Source/Framework/Script/Lua/`，公开路径 `Script/Lua/...`。** 不进 `ya-engine` 聚合。`ya-ecs-systems` 不再 `add_packages("sol2")`。GameRuntime / GameEditor 的源码不 include sol，也不 `add_deps("ya-script-lua")`。接线只在 `ya-runtime` 的 `Entry.cpp` 和 `ya-testing`。
3. **活的 sol 句柄放进后端侧表，不放进组件的 pimpl。** 组件上只留可序列化的作者数据。`propertyOverrides` 和 `LuaScriptProperty::value` 从 `std::any` 换成 `script::ScriptValue`。编辑器预览根本不把 sol 写到实例上（今天成功路径已经是局部 table）。
4. **`self` 必须仍是 Lua table。** `Class.lua`、脚本自己的字段、`Dialogue:say(lines, onDone)` 的 table / 函数参数都靠它。`ui.get` 在有脚本时返回这张表，所以 `dialogue:busy()` / `:say` 不是 C++ 绑定。中立层要加的是「把这张表当作一个值传出去」的句柄，不是把 `self` 换成 `ScriptRef` userdata。
5. **控件句柄改成和实体一样的 `ScriptRef` + 原生字段/方法。** 现有 `ScriptValue` 装不下 Lua 函数、脚本 self、`rect` 那张 `{x,y,w,h}` 表。要加三个值，外加原生字段和「控件没了返回 nil」的引用种类。`busy()` 不在这张清单里。

## 1. 依赖现状

`rg -l "sol/|sol::" Engine/Source` 共 16 个文件，与开工时一致：

| 文件 | sol 出现在 |
| --- | --- |
| `ECS/Systems/include/ECS/Systems/LuaScriptInstance.h` | 公开头：`sol::table self`，五个 `sol::function`，`capturePropertiesFrom(sol::table)` 等 |
| `ECS/Systems/LuaScriptInstance.cpp` | 实现 |
| `ECS/Systems/include/ECS/Systems/Components/LuaScriptComponent.h` | 不 include sol，但 include 上一行，注释里还有 `sol::` |
| `ECS/Systems/include/ECS/Systems/LuaScriptingSystem.h` | `sol::state`、`ILuaScriptHost::bindSelf(sol::table&)`、`persistentState()` 返回 `sol::table`、`lua()` |
| `ECS/Systems/LuaScriptingSystem.cpp` | 状态、`Input`/`Time`/`Log` usertype、`EKey`/`EMouse`、`CameraProjection`、`Persist` |
| `ECS/Systems/include/ECS/Systems/LuaScriptBinding.h` + `.cpp` | `LuaScriptObject` 的 sol 定制点；Vec2/3/4 usertype；模块函数投影 |
| `ECS/Systems/include/ECS/Systems/LuaEvent.h` + `.cpp` | `LuaListenerScope`、`TLuaEvent::add(sol::object, sol::object)` |
| `GameRuntime/Script/GameplayLua.cpp` | `bindGameplayLua` 里手写 `set_function`（头文件 `GameplayLua.h` **没有** sol） |
| `GameRuntime/Script/LuaWidgetHandle.h` + `.cpp` | 五种 usertype |
| `GameRuntime/Script/LuaWidgetScripts.h` + `.cpp` | include `LuaScriptingSystem.h`；`self` API、计时器、`FWidgetScriptHost` |
| `GameEditor/include/GameEditor/UI/Ops/EditorLuaPreview.h` + `.cpp` | 编辑器自己的 `sol::state` |

不在这 16 个里、但链接了 sol2 包的：`ya-ecs-systems`（`sol2` **public**）、`ya-game-runtime`、`ya-render-3d`。后两个的源码没有 `sol::`。`quickjs` 留在 `ya-ecs-systems`（`JSScriptingSystem` 头已经是 pimpl，不暴露 quickjs）。

### 1.1 谁 include 谁

```text
ya-foundation-core
  Core/Scripting/ScriptValue.h          ScriptValue / ScriptArgs / ScriptRef
  Core/Scripting/ScriptBindings.h       registerModuleFunction / addNativeMethod / findField
        ^
ya-scene-core  Scene/Core/SceneScriptBindings.h
  entityRef / sceneRef / componentRef，ensureSceneScriptBindings()
        ^
ya-ecs-systems   （今天 public 依赖 sol2）
  LuaScriptInstance.h  --include--> sol/sol.hpp
        ^
  LuaScriptComponent.h
        ^-- EditorComponentSectionRegistry.cpp
        ^-- EditorLuaScriptSection.cpp
        ^-- EditorLuaPreview.h
        ^-- GameplayScriptFunctions.cpp     （entityAt 只把组件当「这实体有脚本」的标记）
        ^-- LuaScriptingSystem.cpp
        ^-- Example/HelloMaterial/Source/HelloMaterial.cpp
        ^-- 测试：SceneSerializerDefaultsTest、TickOrderTest、GameUIScriptTest、
            LuaEntityScriptCallTest、TilemapEntityQueryTest、EditorComponentOpsTest

  LuaScriptingSystem.h --include--> LuaScriptInstance.h + sol/sol.hpp
        ^-- AppLifecycle.cpp          new / init / shutdown / onStop / resetPersistentState
        ^-- GameRuntimeTickOrchestrator.cpp   onUpdate、flush 时 onEntityDestroying
        ^-- AppSceneServices.cpp      onStop
        ^-- LuaWidgetScripts.h
        ^-- GameplayLua.cpp
  App.h 只前向声明 LuaScriptingSystem，成员是裸指针 _luaScriptingSystem

  LuaScriptBinding.h    GameplayLua.cpp、LuaScriptingSystem.cpp、测试 LuaScriptBindingTest
  LuaEvent.h            LuaWidgetHandle.cpp（按钮 onClicked）、LuaScriptingSystem.cpp

GameEditor
  EditorLuaScriptSection.h --include--> EditorLuaPreview.h --include--> LuaScriptComponent.h + sol
  EditorLuaScriptSection 自己再 include LuaScriptComponent.h
```

`LuaScriptingSystem` 的运行时消费者只有 GameRuntime 这三处，加上测试里直接构造它的 `LuaScriptHostTest`、`TickOrderTest`、`GameUIScriptTest`、`LuaEntityScriptCallTest`、`SceneTransferTest`。编辑器不跑这个系统。

`JSScriptingSystem` 留在 `ya-ecs-systems`。它在 `init` 时 `script::forEachModuleFunction`，把中立层函数投影成 `ya.<module>.<name>`。Lua 在 `registerLuaScriptBindings` 里投影成全局 `<module>.<name>`。这条已经是对的，B2 不改 JS 的投影方式，只在 `ScriptValue` 变体加臂时补 `toJs` / `fromJs` 的分支。

### 1.2 sol 类型从 `LuaScriptInstance` 漏到哪里

实例上的 sol 成员（`self`、`onInit/onStart/onUpdate/onDestroy/onEnable/onDisable`、`listeners`）被这些非「语言后端」代码碰到：

| 调用方 | 碰到什么 |
| --- | --- |
| `LuaWidgetScripts` | `instance.self`（热重载比较是不是同一张表）、`bindSelf(sol::table&)` |
| `EditorLuaPreview::releaseMatching` | `script.self.lua_state()`、`script.onInit.lua_state()`，用来判断句柄是不是预览状态的 |
| `EditorLuaScriptSection` | 改路径时 `row.releaseLuaHandles()`（约 221、243 行）。不读 sol 字段，但方法签名在公开头上 |
| `LuaScriptComponent::releaseLuaHandles/cleanup` | 转调每个实例 |
| `LuaScriptComponentLifetimeTest` | 直接 `releaseLuaHandles()` |

预览的成功路径不把 sol 留在实例上：`EditorLuaPreview::load` 用局部 `sol::table`，调 `applyPropertyOverridesTo(table, _lua)` 和 `capturePropertiesFrom(table)`，然后表死掉。`releaseMatching` 是防备有人把句柄写到实例上。

作者数据（无 sol，但和 sol 头绑在一起）：`scriptPath`、`enabled`、`executionOrderOverride`、`scriptExecutionOrder`、`properties`、`propertyOverrides`、`bLoaded`、`runtimeId`、两个 preview 标志。场景 JSON 只写 path / enabled / executionOrder / propertyOverrides（`serializeCustom`）。

### 1.3 `propertyOverrides` 今天怎么用

类型是 `std::unordered_map<std::string, std::any>`。读写双方都手写同一份类型阶梯：

- 序列化 / 反序列化（`LuaScriptComponent::serializeCustom` / `deserializeCustom`）：`bool`、`int`、`float`、`double`、`std::string`、`glm::vec2/3/4`。
- 编辑器（`EditorLuaScriptSection::storeOverride`）：按 `LuaScriptProperty::typeHint` 写回 `std::any`。`typeHint` 实际出现的是 `"float"` `"int"` `"bool"` `"string"` `"Vec3"`。`anyAs<T>` 在 int/float/double 之间互转。
- 运行时（`LuaScriptInstance::applyPropertyOverridesTo`）：`makeLuaObject<int, float, double, bool, string, vec2, vec3, vec4>` 推进 Lua 表。

`properties` 是 `_PROPERTIES` 的 C++ 拷贝，给检查器画行。值同样是 `std::any`。克隆（`cloneCustom`）只拷 path、enabled、executionOrderOverride、propertyOverrides，不拷 sol，也不拷 `properties`。

### 1.4 编辑器预览

`EditorLuaPreview` 是检查器区段上的成员（`EditorLuaScriptSection::_preview`）。单独一个 `sol::state`：`IS_EDITOR=true`，只开 base/package/math/string/table，只注册 `Vec3`。不跑 `onInit`。区段在析构和换实体时 `releaseMatching`。设计器预览不走这里（`EditorGameUIPreviewTest.PreviewDoesNotActivateBehaviors`）。

## 2. 目标结构

### 2.1 Charter（先于建 target）

`ya-script-lua` 独立，是因为同时满足拆 target 标准里的四条：可替换（D2，Lua 是插件）、闭合依赖且不反向引用 ECS、能从引擎编译闭包里拿掉 sol2、有自己的生命周期（一个 `lua_State`）。

- 对外提供：把中立层投影进一个 Lua 状态；实例的活表和回调；属性预览；函数句柄的调用。
- 禁止依赖：`ya-game-runtime`、`ya-game-editor`、`App`、Render3D。可以依赖 `ya-foundation-core`、`ya-ecs-systems`、`ya-scene-core`（实体宿主和 `entityRef`）、`ya-gui-widgets` **不依赖**。控件类型留在 GameRuntime 的原生方法里。

不建第二个 target。若把仍含 sol 的控件绑定放进 `Applications/GameRuntime/Script/Lua/`，rg 能绿，但 `world.*` / 控件句柄仍是手写 sol，计划正文「迁到中立层登记」不算完成，而且 Framework 不能去依赖 Applications。

### 2.2 目录和公开路径

验收 glob 排除的是路径 `**/Script/Lua/**`。target 根就放在这条路径下，和 `ECS/Systems` 一样：target 目录比公开路径深一层，公开路径相对 `include/`。

```text
Engine/Source/Framework/Script/Lua/
  xmake.lua
  LuaScriptingSystem.cpp
  LuaScriptBinding.cpp
  LuaEvent.cpp
  LuaScriptLive.cpp            # 侧表、属性捕获、覆盖写回
  ScriptPropertyPreview.cpp    # 今天的 EditorLuaPreview
  include/Script/Lua/LuaTesting.h   # 仅测试：暴露 sol::state。公开头里唯一允许 include sol 的
```

运行时公开给引擎的头**不在这里**，而在 ECS，且不含 sol：

```text
Engine/Source/Framework/ECS/Systems/include/ECS/Systems/WorldScriptRuntime.h
Engine/Source/Framework/ECS/Systems/include/ECS/Systems/ScriptInstance.h
  # 由今天的 LuaScriptInstance 改名搬来；LuaScriptComponent.h 改 include 这一份
Engine/Source/Framework/ECS/Systems/include/ECS/Systems/Components/LuaScriptComponent.h
  # 类名不改。场景 JSON 的类型名是反射名 LuaScriptComponent
```

`Script/Lua/LuaTesting.h` 给 `LuaScriptBindingTest` 这类要拿 `sol::state` 的测试用。产品代码不 include 它。因此 `add_packages("sol2", "lua")` 设成 **public**：只有 `add_deps("ya-script-lua")` 的目标（`ya-runtime`、`ya-testing`）才看得到 sol 头。`ya-game-runtime` 不依赖它，就看不见。

xmake 形状：

```lua
target("ya-script-lua")
    set_kind(ya_target_kind())
    ya_std_module("YA_SCRIPT_LUA_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_deps("ya-foundation-core", "ya-ecs-systems", "ya-scene-core")
    add_packages("sol2", "lua", { public = true })
```

`Engine/Source/Source.xmake.lua` 在 `ya-ecs-systems` 之后 `includes("./Framework/Script/Lua/xmake.lua")`。`ya_engine_defines()` **不加** `YA_SCRIPT_LUA_API`：这个 target 不进聚合，聚合的预编译头也不解析它的头。宏由 `ya_std_module` 注入到本 target 和它的依赖方。

### 2.3 依赖方向

```text
ya-foundation-core
    ScriptValue、ScriptBindings、IScriptCallbacks
        ^
ya-ecs-systems                         无 sol2
    ScriptInstance、LuaScriptComponent、IWorldScriptRuntime
    WorldScriptRuntimeFactory（函数指针，状态在这个 DLL）
    JSScriptingSystem（quickjs 留下）
        ^
ya-scene-core   entityRef 等
        ^
ya-script-lua                          唯一持有 sol 的引擎 target
    实现 IWorldScriptRuntime
    installLuaScriptBackend() 写入 ECS 里的工厂
        ^
ya-runtime (Entry.cpp) 与 ya-testing
    add_deps("ya-script-lua")，main 里显式调用 install

ya-game-runtime / ya-game-editor
    只调用 IWorldScriptRuntime 和 script::registerModuleFunction
    不 add_deps ya-script-lua，不 add_packages sol2

ya-engine 聚合
    不加入 ya-script-lua
    从 ya-ecs-systems 去掉 public sol2 之后，聚合不再传递 sol
```

没有环：`ya-script-lua` → `ya-ecs-systems` → core。GameRuntime 不指回 Lua target。`ya-scene-2d` 今天依赖 `ya-ecs-systems`，Lua target 不依赖 scene-2d（`spawnSprite` 留在 GameRuntime）。

`ya-render-3d` 的 `add_packages("sol2")` 一并删掉，源码没有引用。

### 2.4 组件上的 sol 怎么去掉

**侧表，不用 pimpl。** 原因：同一进程有两个 Lua 状态（玩法、编辑器预览）；组件在 EnTT 存储里被移动；`cloneCustom` 不能把活句柄拷进副本；预览成功路径本来就不持有句柄。pimpl 嵌在实例上会把「句柄属于哪个状态」塞回组件。

`ScriptInstance`（仍由 `LuaScriptComponent::scripts` 持有，序列化布局不变）：

```cpp
struct ScriptInstance
{
    std::string scriptPath;
    bool        bLoaded                    = false;
    bool        bAuthoringPreviewAttempted = false;
    bool        bAuthoringPreviewLoaded    = false;
    uint64_t    runtimeId                  = 0; // 0 = 没有活表

    std::vector<ScriptProperty>                    properties;
    std::unordered_map<std::string, script::ScriptValue> propertyOverrides;

    bool enabled = true;
    int  scriptExecutionOrder = 0;
    std::optional<int> executionOrderOverride;

    [[nodiscard]] int executionOrder() const;
    static std::string normalizeScriptPath(std::string_view path);
};

struct ScriptProperty
{
    std::string       name;
    script::ScriptValue value;   // 不再是 std::any
    std::string       typeHint;  // 检查器仍靠它选控件，不靠 variant 判别 int/float
    float min = 0.0f, max = 100.0f;
    std::string tooltip;
};
```

`int` 写入 `int64_t`，`float` 写入 `double`。JSON 仍按 `is_number_integer` / `is_number_float` 区分，和今天的文件兼容。`typeHint == "int"` 的拖动条用 `scriptToInteger`，`"float"` / `"Vec3"` 用 `scriptToNumber` / `std::get<glm::vec3>`。`anyAs` 删除。

侧表在 `ya-script-lua` 的 cpp 里，按 `(lua_State*, runtimeId)` 索引：

```cpp
struct LuaInstanceLive
{
    sol::table    self;
    sol::function onInit, onStart, onUpdate, onDestroy, onEnable, onDisable;
    std::shared_ptr<LuaListenerScope> listeners;
};
```

`destroy` / 热重载 / `onStop` 在状态还活着时摘掉条目。`ScriptInstance` 上不再有 `releaseLuaHandles()`。编辑器改路径时若 `runtimeId != 0`，调 `IWorldScriptRuntime::dropInstance(uint64_t)`；预览没发过 id，调用是空操作。

`ILuaScriptHost::bindSelf(sol::table&)` 从公开头删除。宿主接口改成无 sol 的，放在 `WorldScriptRuntime.h`：

```cpp
struct ScriptSelfBuilder
{
    void field(std::string name, script::ScriptValue value);
    void method(std::string name, script::ScriptFunction fn);
};

struct IScriptInstanceHost
{
    virtual ~IScriptInstanceHost() = default;
    virtual ScriptInstance* resolve(uint64_t instanceId) = 0;
    /// 后端拿到描述后，自己把字段和方法 rawset 到那张 Lua self 上。
    virtual void contributeSelf(ScriptSelfBuilder& out) = 0;
};
```

实体宿主（今天的 `FEntityScriptHost`）留在 Lua target 的 cpp：`contributeSelf` 只写 `field("entity", entityRef(...))`。控件宿主留在 `LuaWidgetScripts.cpp`，见 §3.3。

属性预览注册在 ECS（状态只有一份，DLL 归属 `ya-ecs-systems`）：

```cpp
struct IScriptPropertyPreview
{
    virtual ~IScriptPropertyPreview() = default;
    /// 读脚本、把 _PROPERTIES 填进 instance.properties，应用 propertyOverrides。
    /// 不在 instance 上留下任何活句柄。
    virtual bool load(ScriptInstance& instance) = 0;
};

YA_ECS_SYSTEMS_API void setScriptPropertyPreview(IScriptPropertyPreview* preview);
YA_ECS_SYSTEMS_API IScriptPropertyPreview* scriptPropertyPreview();
```

`ScriptPropertyPreview.cpp` 在 `installLuaScriptBackend()` 里把自己注册进去。`EditorLuaScriptSection` 删掉 `_preview` 成员，改叫 `scriptPropertyPreview()->load`。预览实现内部复用和玩法状态同一套 Vec 注册，不再单独 `new_usertype<glm::vec3>`。没装 Lua 后端时指针为空，检查器显示「没有属性」，不崩。

`LuaScriptComponent` 的反射注册、`useReflectionSerialization() == false`、JSON 字段名都不改。

### 2.5 引擎看到的运行时

`App::_luaScriptingSystem` 改成 `IWorldScriptRuntime*`。接口在 `WorldScriptRuntime.h`，导出宏 `YA_ECS_SYSTEMS_API`，析构是虚函数（GameRuntime 跨 DLL `delete` 基类指针）：

```cpp
struct WorldScriptServices
{
    const InputManager* input = nullptr;
    std::function<bool()>       isMouseCaptured;
    std::function<double()>     elapsedSeconds;
    std::function<uint64_t()>   frameIndex;
    std::function<Scene*()>     activeScene;
    std::function<bool(const std::string& path, std::string& out)> readScript;
};

struct YA_ECS_SYSTEMS_API IWorldScriptRuntime
{
    virtual ~IWorldScriptRuntime() = default;
    virtual void setServices(WorldScriptServices services) = 0;
    virtual void init() = 0;
    virtual void shutdown() = 0;
    virtual void onUpdate(float deltaTime) = 0;
    virtual void onStop() = 0;
    virtual void onEntityDestroying(Entity& entity) = 0;
    virtual void resetPersistentState() = 0;
    virtual void dropInstance(uint64_t runtimeId) = 0;
};

using WorldScriptRuntimeFactory = IWorldScriptRuntime* (*)();
YA_ECS_SYSTEMS_API void setWorldScriptRuntimeFactory(WorldScriptRuntimeFactory factory);
YA_ECS_SYSTEMS_API IWorldScriptRuntime* createWorldScriptRuntime();
```

`LuaScriptingSystem` 实现这个接口，类本身只在 `Script/Lua/*.cpp` 里出现。`persistentState()` 继续返回内部的 `sol::table`，但那是 cpp 私有的；接口上只有 `resetPersistentState()`。`Persist` 全局仍是那张表。

安装点不放在 GameRuntime，否则 GameRuntime 就得 include Lua 头：

```cpp
// Script/Lua 里，YA_SCRIPT_LUA_API
void installLuaScriptBackend();
// 做两件事：setWorldScriptRuntimeFactory(...)，setScriptPropertyPreview(...)
```

`Engine/Programs/YARuntime/Source/Entry.cpp` 在 `App` 构造之前调用。`Engine/Test` 的 runner 同样调用。静态初始化不可靠：monolith 下没人引用的 .o 会被链接器丢掉（`windows_dll_boundary`：状态入口必须有明确的调用，不能靠静态局部自己冒出来）。工厂没装时 `App` init 打 `YA_CORE_ERROR` 并继续跑无脚本模式，不静默。

`Input` / `Time` / `Log` / `EKey` / `EMouse` / `CameraProjection` 的手写 usertype 随 `LuaScriptingSystem.cpp` 搬进 `Script/Lua/`，本轮不改成中立层。它们不在 §3 的三个文件里，JS 也不走这套名字。`CameraProjection` 的通用反射导出仍是 B1 明确推迟的。

## 3. 中立层缺口

下面每条都是今天手写 sol 的注册。迁完之后这些 cpp 里不再出现 `sol::`。

### 3.1 `GameplayLua.cpp`

| 名字 | 今天的签名 | 迁法 |
| --- | --- | --- |
| `Input.setKeyHandler` | `(self, handler)`，`handler` 是函数或空。空则清掉。回调 `(key:int, pressed:bool, repeat:bool) -> bool`，返回 true 表示消费 | 中立层函数，见下面的 `ScriptFnHandle`。捕获注入的 `InputRouter&`，不调用 `App::get()`。**不删除。** S5 才会拿 `onKey` 换掉它 |
| `world.spawnSprite` | `(name: string) -> entity 或 nil`。内部仍用探针图 `Engine:Content/TestTextures/sprite2d_probe_64.png`，size `{1,1}` | `registerModuleFunction`。返回 `entityRef` 或 monostate。**不改成** S5 的 `(name, {texture,size,tint})` |
| `world.destroyEntity` | `(entity) -> nil`。`scene->queueDestroyNode` | `registerModuleFunction`，参数是 `ScriptRef` |
| `world.viewAspect` | `() -> number`。转调 `gameplayViewAspect()` | `registerModuleFunction`，和 `viewSize` 共用同一个取景函数。见 §4 |
| `ui.get` | `(entryId) -> self 表，或根控件句柄，或 nil` | `registerModuleFunction`。有已加载的根脚本时返回 `ScriptSelfHandle`（Lua 端推回**那张表**，所以 `:say` / `:busy` / `:find` 仍是表上的方法）。否则返回控件 `ScriptRef` |
| `ui.setText` | `(entryId, widgetName, text) -> bool` | `registerModuleFunction`，体内仍是 `GameUIHost::setMountedText` |
| `ui.setVisible` | `(entryId, widgetName, visible) -> bool` | 同上，`setMountedVisible` |

`bindGameplayLua` 里 `ui.setBehaviorRuntime(make_unique<LuaWidgetScripts>)` 不是 sol 注册，留在 GameRuntime，在 `install` 之后由 App 调一个无 sol 的 `installWidgetScriptRuntime(IWorldScriptRuntime&, GameUIHost&)`。这个函数写在 `LuaWidgetScripts.cpp`，不进 Lua target（它要 `GameUIHost`）。

### 3.2 `LuaWidgetHandle.cpp`

今天 `bindLuaWidgetHandles` 注册五个 usertype。迁完后没有 usertype。句柄是 `ScriptRef`：

- `kind` = GameRuntime 注册的 `"widget"` 种类
- `type` = `widget->getTypeIndex()`（`UIText` / `UIButton` / `UIImage` / `UIBorder` / `UIElement` 已经各自 override）
- `a` = `UIElement::_runtimeId`
- `b` = 0

没有按 runtimeId 找控件的现成表。GameRuntime 加一张 `uint64 → weak_ptr<UIElement>`，铸句柄时写入，`resolve` 时 `lock`，过期条目顺手擦掉。悬空句柄保持今天的契约（`GameUIScriptTest.DanglingHandleIsNoOp`）：读返回 nil，写无操作，每个 id 警告一次。这和实体引用「抛 Lua 错误」不同，所以引用种类要能声明这件事：

```cpp
struct ScriptRefKind
{
    std::string name;
    std::function<void*(const ScriptRef&)> resolve;
    std::function<void(const ScriptRef&, void*)> afterWrite;
    std::function<void(const ScriptRef&, void*)> afterCall;
    /// true：对象没了时读返回 nil、写和调用无操作，不抛 ScriptError。
    bool missingIsNil = false;
};
```

`readField` / `writeField` / `callMethod` 在 `missingIsNil` 且 `tryResolve` 为空时走这条，不调用 `resolve()`。实体种类保持默认 `false`。

字段不是方法。`self.root.visible = true`、`self.body.text = ""` 是赋值。今天的 `ScriptField` 只有反射 `Property*`，`readField` 无条件解引用它。加原生字段：

```cpp
struct ScriptNativeField
{
    std::function<script::ScriptValue(void* self, const script::ScriptRef&)> read;
    std::function<void(void* self, const script::ScriptRef&, const script::ScriptValue&)> write; // 空 = 只读
};

YA_CORE_API void addNativeField(type_index_t type, std::string name, ScriptNativeField field);
```

`ScriptField::property == nullptr` 时走这对函数。反射字段的路径不变。

控件类型没有反射 `Class`，`findMethod` 不会沿基类走。加：

```cpp
/// derived 的查找在自己的表未命中时继续看 base。后注册的同名成员不覆盖先注册的。
YA_CORE_API void addScriptBase(type_index_t derived, type_index_t base);
YA_CORE_API void setScriptTypeName(type_index_t type, std::string name);
```

注册顺序：先给 `UIElement` 挂字段和方法，再 `addScriptBase(UIText, UIElement)` 等。`setScriptTypeName` 让 Lua `__name` 仍是 `Widget` / `Text` / `Button` / `Image` / `Border`。

| 成员 | 种类 | 签名（中立层） | 说明 |
| --- | --- | --- | --- |
| `valid` | 只读字段 | `() -> bool` | `live()`：在树上，或 `GameUIHost::isPendingSpawn` |
| `name` | 只读字段 | `() -> string 或 nil` | |
| `visible` | 字段 | 读 `bool`，写 `bool` | 写走 `setVisibility` |
| `find` | 方法 | `(name) -> widget ScriptRef 或 nil` | 未挂上的用子树；已挂上的用 `entryRootOf` + `findInEntry` |
| `destroy` | 方法 | `() -> nil` | `queueDestroy` |
| `layout` | 方法 | `() -> nil` | `layoutNow` |
| `rect` | 方法 | `() -> ScriptRect 或 nil` | 今天返回 `{x,y,w,h}` 表。`ScriptValue` 没有通用表。加 `struct ScriptRect { float x,y,w,h; }`，Lua 推出同形的表，Lua→C++ 的任意 table 仍然抛错（`say` 的 lines 不经过这里） |
| `text` / `color` / `fontSize` | 字段，仅 `UIText` | `string` / `vec4` / `int64` | `fontSize` 今天是 `uint32`，过边界用 `int64` |
| `onClicked` | 只读字段，仅 `UIButton` | `() -> event ScriptRef` | 见下面的事件 |
| `enabled` | 字段，仅 `UIButton` | `bool` | |
| `texture` | 字段，仅 `UIImage` | `string`（资产路径） | |
| `fill` | 字段，仅 `UIBorder` | `vec4` | `getColor` / `setColor` |

事件。`btn.onClicked:add(self, fn)` 的两个参数今天过不了 `toScriptValue`（table 和 function 都走 `default: break` 然后抛「cannot be passed」）。加：

```cpp
/// 后端保管的 Lua 函数。0 是空。只由 Lua 后端在 toScriptValue(LUA_TFUNCTION) 时铸出。
struct ScriptFnHandle
{
    uint64_t id = 0;
    bool operator==(const ScriptFnHandle&) const = default;
};

/// 某一次 load 产生的那张 self 表。推回 Lua 时推表本身，不推 userdata。
struct ScriptSelfHandle
{
    uint64_t runtimeId = 0;
    bool operator==(const ScriptSelfHandle&) const = default;
};
```

二者都放进 `ScriptValue` 变体。JS 的 `toJs` 对这两臂和 `ScriptRect` 抛 `"not projected to JS"`；`fromJs` 不产生它们。JS 玩法不调用 `ui.*`。变体一改，`JSScriptingSystem.cpp` 的 `std::visit` 和 `LuaScriptBinding.cpp` 的 `pushLuaValue` / `toScriptValue` 必须同一提交改完，否则编不过。

```cpp
/// 实现在 ya-script-lua，注册进 core。GameRuntime 的原生方法用它调用函数，自己不碰 sol。
struct IScriptCallbacks
{
    virtual ~IScriptCallbacks() = default;
    virtual void retain(script::ScriptFnHandle fn) = 0;
    virtual void release(script::ScriptFnHandle fn) = 0;
    /// 调用 fn(self, args...)。self 为空则不传 self。错误记日志，不抛出 Lua 状态。
    virtual void invoke(script::ScriptFnHandle fn, script::ScriptSelfHandle self, script::ScriptArgs args) = 0;
};

YA_CORE_API void setScriptCallbacks(IScriptCallbacks* callbacks);
```

`ButtonClicked` 是一个只有原生方法的类型（没有 C++ 控件类）：

- `add(self: ScriptSelfHandle, fn: ScriptFnHandle) -> int64`（delegate handle 的整数值；今天返回的就是 `DelegateHandle`）
- `remove(handle: int64) -> bool`
- `removeAll(self: ScriptSelfHandle) -> int64`

`add` 的实现在 GameRuntime：`tryResolve` 出 `UIButton`，`onClicked.add` 一个 C++ lambda，lambda 调 `IScriptCallbacks::invoke`。`retain` 挂在 self 的实例上，实例 `destroy` 时 `release`，lambda 发现 handle 已失效就返回。这就是今天 `LuaListenerScope` 的寿命规则，只是 sol 函数改由后端侧表持有。`TLuaEvent` / `makeLuaEvent` / `LuaListenerScope` 在控件迁完、确认没有别的 `add(self, fn)` 调用方之后删除。

### 3.3 `LuaWidgetScripts.cpp`

这些是装到**每张 self 表**上的函数，不是类型方法。用 `ScriptSelfBuilder::method`，后端为每个名字生成一个 Lua 闭包。

| 名字 | 今天 | 中立层 |
| --- | --- | --- |
| `setTickEnabled` | `(self, bool)` | `ScriptFunction`，无参数以外的 `bool`。闭包捕获 behavior，不需要 self 参数 |
| `after` | `(self, seconds, fn) -> timer 或 nil` | 参数 `double` + `ScriptFnHandle`。返回计时器 `ScriptRef` 或 nil |
| `every` | 同上，interval = seconds | 同上 |
| `find` | `(self, name) -> 句柄或 nil` | 在 `entryRoot` 上 `findInEntry` |
| `spawn` | `(self, documentPath, parentHandle) -> 句柄或 nil` | `queueSpawn`。parent 是 widget `ScriptRef` |

self 上的字段，同一处 `contributeSelf`：

| 字段 | 值 |
| --- | --- |
| `widget` | 宿主控件的 `ScriptRef` |
| `root` | `entryRoot` 的 `ScriptRef` |

`FWidgetScriptRef` 这个藏在 `self.__widgetScript` 里的 usertype 删除。方法闭包直接捕获 `weak_ptr<LuaWidgetScriptBehavior>`。

计时器类型 `UITimer`，一个原生方法 `cancel() -> nil`。`a` 里放 `GameUIHost::addTimer` 返回的 id。`cancel` 调 `cancelTimer`。今天的 `FLuaUITimer` usertype 删除。

`scriptSelfOf` 今天返回 `sol::object`（那张 self）。改成返回 `ScriptSelfHandle`，由 `ui.get` 放进 `ScriptValue`。Lua 后端 `pushLuaValue` 看到它就 `lua_rawgeti` 侧表里的 self。这样 `ui.get("Dialogue"):say(lines, onDone)` 仍是 Lua 对 Lua 的调用，`lines` 和 `onDone` **不**经过 `ScriptValue`。

### 3.4 不能直接表达、但不要做成 C++ API 的

| 表面 | 原因 |
| --- | --- |
| `dialogue:busy()` / `:say(lines, onDone)` | `Example/2DRpgPrototype/Content/UI/Dialogue.lua` 里的 Lua 方法。`busy` 没有 C++ 绑定。`ui.get` 必须返回 self 表，这两函数才在 |
| `self.lines`、`self.talking` 等脚本字段 | Lua 表字段。`ScriptRef` 的 `__newindex` 只写已知字段，换掉 self 会拆掉 `Class.lua` |
| `Input.isKeyDown` 等、`EKey`、`time`、`log` | 已经在 `LuaScriptingSystem.cpp`，搬进 `Script/Lua/` 即可，本轮不进中立层 |

`ScriptValue` 加臂之后的完整集合：

```cpp
using ScriptValue = std::variant<
    std::monostate, bool, int64_t, double, std::string,
    glm::vec2, glm::vec3, glm::vec4, ScriptRef,
    ScriptFnHandle, ScriptSelfHandle, ScriptRect>;
```

## 4. 去掉 `App::get()`

`GameplayScriptFunctions.cpp` 和 `GameplayLua.cpp` 里每一处：

| 位置 | 今天 | 换成 |
| --- | --- | --- |
| `find` | `App::get()->getSceneServices().getActiveScene()` | 注册时捕获的 `std::function<Scene*()>`。`LuaRuntimeServices::activeScene` 已经是这个形状，但模块函数不在系统里，所以单独捕获，不从系统里掏 |
| `viewSize` / `currentPresentedView` | `App::get()` 取 `getRenderResolution()` 和主相机 | 捕获 `std::function<glm::vec2()>` 像素尺寸 + 上面的场景函数。主相机仍用 `findPrimaryCamera` + `scriptViewSize` |
| `loadScene` | `App::get()->getSceneServices().requestSceneTransfer` | 捕获 `std::function<void(std::string path, std::string spawn)>` |
| `gameplayViewAspect` | 再调一次 `currentPresentedView` | 删掉这个自由函数。`viewAspect` 与 `viewSize` 都调同一个本地函数，底层仍是 `scriptViewAspect` / `scriptViewSize`（`CameraComponent.cpp`，已经共用 `resolveCameraViewFraming`） |
| `spawnSprite` / `destroySpriteEntity` | 匿名命名空间里的 `activeScene()` → `App::get()` | 与 `find` 同一个场景函数 |
| `Input.setKeyHandler` | lambda 里 `App::get()->getInputRouter()` | `bind` 时捕获 `InputRouter&` |

现在就能换、不必等新变体的：`find`、`viewSize`、`loadScene`、`spawnSprite`、`destroyEntity`、`setKeyHandler` 的 **App 查找**。`setKeyHandler` 的函数参数仍是 sol，直到 §3 的 `ScriptFnHandle` 落地；那一步只换参数类型，不再碰 `App`。

注册签名：

```cpp
struct GameplayScriptContext
{
    std::function<Scene*()> activeScene;
    std::function<glm::vec2()> presentedPixels;          // renderResolution
    std::function<void(std::string path, std::string spawn)> requestSceneTransfer;
    InputRouter* inputRouter = nullptr;                  // setKeyHandler，S5 删除该函数时一起删
};

void registerGameplayScriptFunctions(GameplayScriptContext ctx);
```

`AppLifecycle` 在 `new` 系统之前填这个结构（今天已经是这个顺序：先 `registerGameplayScriptFunctions()`，再 `init()`，注释写明后端建状态时才投影）。lambda 捕获 `&app`，和现有的 `setRuntimeServices` 一样。模块函数不再写「去找单例」。

`viewAspect` 与 `viewSize` 去重指的是**一条取值路径、两个名字**，不是合成一个函数。`Snake.lua` 调 `world.viewAspect()`，`FollowCamera.lua` 调 `world.viewSize()`。pixel perfect 正交时：`viewSize` 是 `(halfWidth, halfHeight)` 世界单位，`viewAspect` 是 `halfWidth/halfHeight`；否则分别是像素尺寸和像素宽高比。两个 `registerModuleFunction` 都调用 `CameraComponent.cpp` 里现成的 `scriptViewSize` / `scriptViewAspect`。删掉只被 sol 包装用的 `gameplayViewAspect()`。

`entityAt` 没有 `App::get()`，它从 `TilemapComponent::getOwner()->getScene()` 拿场景。不动。

## 5. 迁移步骤

每个 checkpoint 一个提交，提交前该目标自己能编译、列出的测试全绿。回退就是 `git revert` 那个提交。B2-4 是文件移动，revert 时整笔 revert，不要和后面的提交混。

测试基线（每个写了「脚本测试」的 checkpoint 都跑；过滤名是 gtest 的并集）：

```bash
export YA_AGENT_NAME=<worker>
Script/agent/ya_lock.sh run -- python3 Script/ya.py test --target ya --filter \
  'ScriptBinding*:LuaScript*:LuaEntity*:GameUIScript*:TickOrder*:SceneTransfer*:ScriptApi*:EditorComponent*'
```

冒烟（最终验收，以及任何改了 `world.*` / `ui.*` / 系统生命周期的 checkpoint）：

```bash
python3 Script/ya.py run --project Example/GreedySnake/GreedySnake.yaproject -- --exit-after-frame=120
python3 Script/ya.py run --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- --exit-after-frame=120
python3 Script/ya.py run-editor --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- --exit-after-frame=30
```

2D 自动化（改了场景切换、对话、行走才需要；它们会起一个带控制端口的进程，跑完必须 `stop`，脚本自己的 `finally` 会停）：

```bash
python3 Script/automation/2d-rpg/walk_one_cell.py
python3 Script/automation/2d-rpg/walk_animation.py
python3 Script/automation/2d-rpg/blocked_by_wall.py
python3 Script/automation/2d-rpg/dialogue_page.py
python3 Script/automation/2d-rpg/scene_load_ticks.py
```

GreedySnake 没有 `Script/automation/greedy-snake/`（那是 S7）。脚本面的回归是上面的 gtest，外加 120 帧冒烟。

### B2-1 — 属性值改成 `ScriptValue`

不移动文件，sol 成员还在。

- 改：`LuaScriptInstance.h` / `.cpp`，`LuaScriptComponent.h`（序列化阶梯 + 删掉会命中 rg 的那句 `sol::` 注释，改写成「活句柄不进反射」），`EditorLuaScriptSection.cpp`（`storeOverride` / `anyAs`），`LuaScriptComponentLifetimeTest.cpp`。
- 验证：`LuaScriptComponentLifetime*`、`SceneSerializerDefaults*`、`EditorComponent*`。
- 回退：单提交。
- 与 B2-2 并行（文件不重叠）。

### B2-2 — 现在就能去掉的 `App::get()`

sol 注册还在，只换捕获的上下文。

- 改：`GameplayScriptFunctions.h` / `.cpp`，`GameplayLua.h` / `.cpp`，`AppLifecycle.cpp`（构造 `GameplayScriptContext`）。
- 不改：`spawnSprite` 的探针图、`setKeyHandler` 的回调语义、`viewAspect` 仍是 sol `set_function`（数值路径改为和 `viewSize` 同一个本地函数）。
- 验证：`ScriptBinding*.ModuleFunctions*`、`ScriptApiLibrary*.ModuleFunctions*`、`LuaScriptBinding*.ModuleFunctions*`、`SceneTransfer*`，加两个 runtime 冒烟。
- 与 B2-1 并行。必须在 B2-6 之前。

### B2-3 — 侧表，公开实例头去掉 sol

- 改：`LuaScriptInstance.h`（删 `#include <sol/sol.hpp>` 和全部 sol 成员 / 方法；本 checkpoint 可以还叫 `LuaScriptInstance`，B2-4 再改名），`LuaScriptInstance.cpp`，`LuaScriptingSystem.h` / `.cpp`（侧表；`ILuaScriptHost::bindSelf` 仍是 sol，因为头还在 ECS），`LuaWidgetScripts.cpp`（`instance.self` 改为向系统要），`EditorLuaPreview.cpp`（不再读 `script.self`），`EditorLuaScriptSection.cpp`（`releaseLuaHandles` 改为 `runtimeId` 路径），`LuaScriptComponent.h` 的 `releaseLuaHandles`。
- 头 `LuaScriptingSystem.h` **仍然 include sol**。rg 还不会绿。
- 验证：`LuaScriptHost*`、`LuaScriptComponentLifetime*`、`TickOrder*`、`GameUIScript*`、`LuaEntity*`。
- 串行于 B2-1 之后。与 B2-2 无文件重叠，但不要和 B2-2 同时改 `LuaWidgetScripts`——B2-2 不碰那个文件，所以仍可与 B2-2 并行。

### B2-4 — 建立 `ya-script-lua`，搬走仍含 sol 的语言后端

- 移到 `Framework/Script/Lua/`：`LuaScriptingSystem.cpp`、`LuaScriptBinding.h/.cpp`、`LuaEvent.h/.cpp`、`LuaScriptInstance.cpp` 里还含 sol 的捕获实现（并进 `LuaScriptLive.cpp`）、`EditorLuaPreview.cpp`（并进 `ScriptPropertyPreview.cpp`）。
- 公开头 `LuaScriptingSystem.h`、`LuaScriptBinding.h`、`LuaEvent.h`、`EditorLuaPreview.h` 从 ECS / GameEditor **删除**。测试改 include `Script/Lua/LuaTesting.h`。
- 留下并改名：`ScriptInstance.h`（无 sol）在 ECS。`LuaScriptComponent.h` 改 include。
- 新文件：`Framework/Script/Lua/xmake.lua`，`Source.xmake.lua` 加 includes。
- `ya-ecs-systems` 的 xmake 去掉 `sol2` 和 `lua` 包。此提交之后 ECS 目标自身能在没有 sol 头的情况下编译。GameRuntime 仍暂时 `add_deps("ya-script-lua")` 并 include `LuaTesting.h` 以外的私有访问——不对。B2-4 结束时 GameRuntime 的 `AppLifecycle.cpp` 仍要创建系统，所以 **本提交允许** `ya-game-runtime` 依赖 `ya-script-lua` 并调用具体类型。把这个依赖拆掉是 B2-5。这样 B2-4 自己能链接、能跑测试。
- 编辑器：`EditorLuaScriptSection.h` 不再 include `EditorLuaPreview.h`。
- 验证：上一档全部脚本测试 + `ya-ecs-systems` 的编译（由 `ya-testing` 带起来）。`rg` 仍会命中 `GameplayLua.cpp`、`LuaWidgetHandle.*`、`LuaWidgetScripts.*`。
- 串行于 B2-3。移动用 `git mv`，不要在同一提交里覆盖已有路径。

B2-4 实现注记（侧表归属）：每张活表在它所属的 `LuaScriptingSystem::_instances` 里，随该对象销毁，且排在 `sol::state` 之前，所以 sol 对象先于 `lua_State` 死。不再有进程级 `liveTables` / `stateById`。`dropScriptInstance(runtimeId)`（组件 `releaseLuaHandles`、编辑器改路径）手里没有系统指针，两个系统的 id 又不能撞，所以 Lua DLL 里留**一张** id → `LuaScriptingSystem*` 的注册表，外加进程级 id 计数器。表里只有裸指针，没有 sol，没有要跟着 `lua_State` 死的静态对象。ECS 侧用 `setDropScriptInstance` 函数指针调用它（状态在 `ya-ecs-systems` 的 cpp，不在头文件里）。属性预览同理：`setScriptPropertyPreview` 的指针在 ECS cpp；对象由 App 显式 create/destroy，不放进程静态。`installLuaScriptBackend` 仍是 B2-5。

### B2-5 — GameRuntime 只剩中立接口；变体加臂

这是 §6 拍板之后的第一个提交。

- 新增 `WorldScriptRuntime.h`（本文 §2.5）、`IScriptCallbacks`、`ScriptFnHandle` / `ScriptSelfHandle` / `ScriptRect`、`addNativeField` / `addScriptBase` / `setScriptTypeName`、`ScriptRefKind::missingIsNil`。
- 改：`ScriptValue.h`，`ScriptBindings.h/.cpp`，`JSScriptingSystem.cpp`（新臂抛「not projected to JS」），`LuaScriptBinding.cpp`（`pushLuaValue` / `toScriptValue`），`App.h`，`AppLifecycle.cpp`，`GameRuntimeTickOrchestrator.cpp`，`AppSceneServices.cpp`，`YARuntime/Entry.cpp`，测试 runner。
- xmake：`ya-game-runtime` 去掉对 `ya-script-lua` 的 deps，去掉 `sol2` / `lua` 包。`ya-runtime` 和 `ya-testing` `add_deps("ya-script-lua")`。`ya-render-3d` 去掉 `sol2` 包。
- `installLuaScriptBackend()` 只从 Entry 和测试 runner 调用。
- 验证：全部脚本测试 + 两个冒烟 + 编辑器 30 帧（预览注册要在进程里）。
- 串行于 B2-4。完成后 B2-6 与 B2-7 才能并行。

### B2-6 — `GameplayLua.cpp` 的手写注册

- 改：`GameplayLua.cpp`（文件可以留在 GameRuntime，里面不再有 sol）、`GameplayScriptFunctions.cpp`（`viewAspect` 登记在这里，删 `gameplayViewAspect`）、`GameplayLua.h`（`bindGameplayLua` 若不再需要就删，调用并进 App 的 widget runtime 安装）。
- 冻结语义，见 §3.1 的表。
- 验证：`GameUIScript*.UiGet*`、`LuaScriptBinding*.ModuleFunctions*`、`SceneTransfer*`、两个冒烟、`dialogue_page.py`、`scene_load_ticks.py`。
- 与 B2-7 并行，前提是 B2-5 已提交。若 S5 正在改 `GameplayLua.cpp`，本 checkpoint 等 S5。

### B2-7 — 控件句柄和 self API

- 改：`LuaWidgetHandle.h/.cpp`（改为原生字段/方法的注册，头不再 include sol；文件留在 GameRuntime）、`LuaWidgetScripts.h/.cpp`（`contributeSelf`，不再 include `LuaScriptingSystem.h`）、widget 引用表（新 cpp，放 GameRuntime，例如 `GameRuntime/Script/WidgetScriptRefs.cpp`）。
- 删：`LuaEvent.h/.cpp` 里不再有调用方的 `TLuaEvent` 模板路径。若 `LuaListenerScope` 的职责已由 `IScriptCallbacks` 覆盖，这两个文件从 `Script/Lua/` 删除。删除前用 `rg "makeLuaEvent|TLuaEvent|LuaListenerScope" Engine/Source` 确认只剩定义。
- 验证：整个 `GameUIScript*`、`LuaScriptHost*`、两个冒烟、`dialogue_page.py`。
- 与 B2-6 并行（不改 `GameplayLua.cpp` / `GameplayScriptFunctions.cpp`）。
- 本提交之后跑最终 `rg`。预期无输出。

### 并行关系

```text
B2-1 属性值 ──┐
              ├──► B2-3 侧表 ──► B2-4 搬 target ──► B2-5 接口与变体 ──┬──► B2-6 world/ui 函数
B2-2 注入上下文 ┘                                                      └──► B2-7 控件句柄
```

B2-1 ∥ B2-2。B2-2 ∥ B2-3。B2-6 ∥ B2-7。其余串行。不要两个 worker 同时改 `AppLifecycle.cpp`、`ScriptValue.h`、`ya-ecs-systems` 的 xmake。

## 6. 风险、未决、要拍板的

### 风险

- **DLL 状态只有一份。** 工厂、`IScriptPropertyPreview`、`IScriptCallbacks`、`registerModuleFunction` 的表都在 `ya-foundation-core` 或 `ya-ecs-systems` 的 cpp 里，导出函数写入。不要做成头文件里的 `inline` 静态局部。`ya-script-lua` 不要再被 `ya-game-editor` 静态链进去一份（monolith 下编辑器模块是 `links = false` + `dynamic_lookup`，保持这样）。
- **导出宏。** 新 target 用 `ya_std_module("YA_SCRIPT_LUA_API")`。跨模块调用的自由函数（`installLuaScriptBackend`）要这个宏。`IWorldScriptRuntime` 的析构在 ECS 侧导出，具体类的析构在 Lua DLL 里，靠虚表，不要把具体类的析构写进被 GameRuntime include 的头。
- **class / struct。** 新类型统一 `struct`，前向声明也写 `struct`。聚合初始化按成员声明顺序（`ScriptProperty`、`WorldScriptServices`、`ScriptRect`）。
- **Windows 链接。** `sol2` 从 `ya-ecs-systems` 的 public 包拿掉之后，任何还 include `<sol/sol.hpp>` 又不依赖 `ya-script-lua` 的目标会直接编不过。这是预期的闸门。`/bigobj` 不用加，这个 target 比 ECS 小。
- **变体是 ABI。** `ScriptValue` 加三个臂，JS 的 `std::visit` 若漏一支就编不过，不会静默。同一提交改完。
- **悬空控件。** 走通用 `resolve()` 会把今天的「nil + 警告一次」变成 Lua 错误，`DanglingHandleIsNoOp` 会红。必须做 `missingIsNil`，不要顺手「统一成抛错」。
- **`self` 表的身份。** `ui.get` 推回原表，脚本才能 `==` 比较、才能走 `Class.lua` 的元表。`ScriptSelfHandle` 在 C++ 侧只比较 `runtimeId`。
- **S5 撞车。** S5 要改 `spawnSprite` 的参数、删除 `setKeyHandler`、可能改 `ui.setText`。B2-6 若和 S5 同时改 `GameplayLua.cpp`，等 S5。B2 不提前实现 S5 的新签名。
- **热重载和计时器。** 今天计时器 lambda 用 `lua_rawequal` 确认 self 还是那一张。侧表换成新 self 之后，`ScriptFnHandle` 必须绑在旧 id 上，旧 id 一释放，隐藏的计时器就停。这是现有契约（`WidgetTimerFiresWhileHiddenAndStopsWithItsScript`），搬的时候带着测。
- **编辑器没装后端。** 预览指针为空时检查器只有路径和启用，没有 `_PROPERTIES` 行。`ya-runtime` 和测试必须调用 `install`。漏调用的症状是玩法脚本全不跑，靠 init 时的 `YA_CORE_ERROR` 暴露。
- **unity build。** 搬文件会改变 `ya-ecs-systems` 的 unity 批次。不要在匿名命名空间里留和别的 cpp 同名的类型（`LuaInputApi` 这类已经在 cpp 内，搬进新 target 后仍放 cpp 匿名命名空间）。

### 要拍板的

1. **`ya-script-lua` 不进 `ya-engine` 聚合，只由 `ya-runtime` 和 `ya-testing` 链接。** 推荐：是。进聚合的话，每个链 `ya-engine` 的目标都会重新看见 sol，和「引擎只依赖中立层」矛盾。代价是忘了 `add_deps` 的新 exe 没有脚本，靠 init 的错误日志发现。
2. **控件走 `ScriptRef` + 原生字段，而不是把 usertype 原样搬进 `Script/Lua/`。** 推荐：走 `ScriptRef`。原样搬能让 rg 变绿，但 GameRuntime 仍通过 Lua 头碰 sol，计划里的「迁到中立层」没做。代价是 §3.2 的 API 增量（原生字段、基类、`missingIsNil`、三个 `ScriptValue` 臂）。
3. **`self` 继续是 Lua 表，`ScriptSelfHandle` 推回那张表。** 推荐：是。把 self 换成 userdata 会拆掉 `Class.lua` 和 `Dialogue:say` 的 table 参数。中立层因此不是「一切皆 `ScriptRef`」。
4. **B2 冻结现有函数语义，等 S5 改 `spawnSprite` / `setKeyHandler`。** 推荐：是，按 `plan.md` §5。B2-6 登记的是今天的 `(name)` 和「一个全局按键回调」。
5. **JS 对本轮新加的三种值直接抛错，不实现控件 / 函数句柄。** 推荐：是。JS 没有 `ui.get` 的调用方。以后 QuickJS 要跑 UI 脚本时再投影。

未决但不阻塞：`rect` 没有脚本调用方。推荐仍用 `ScriptRect` 保住 `{x,y,w,h}`，避免以后有人写了 `.w` 才发现变成了 vec4 的 `.z`。若拍板想少一个变体臂，可以改成返回 `vec4`，本仓库没有要改的 Lua。

## 7. 工作量

| Checkpoint | 文件量（约） | 性质 |
| --- | --- | --- |
| B2-1 | 5 | 类型替换，`std::any` → `ScriptValue` |
| B2-2 | 5 | 注入上下文，去掉 6 处 `App::get()` |
| B2-3 | 8 | 侧表，实例公开头去掉 sol |
| B2-4 | 12 移动 + 4 个 xmake / include | 新 target，ECS 不再链 sol2 |
| B2-5 | 15 | 接口、变体、产品 exe 接线、JS 分支 |
| B2-6 | 4 | `world.*` / `ui.*` / `setKeyHandler` 改登记 |
| B2-7 | 6 | 控件句柄、self API、事件、计时器 |

7 个代码提交。B2-4 和 B2-5 是大头；B2-6 与 B2-7 加起来才是「手写 sol 收口」，而且可以并行。不把「只改文档」或「只搬一半文件但测试不绿」单列成提交。

## 8. 拍板结果（2026-10-05）

1. `ya-script-lua` 不进 `ya-engine` 聚合，只由 `ya-runtime` 与 `ya-testing` 链接。
2. 控件走中立层 `ScriptRef` + 原生字段，不原样搬 sol usertype。
3. `self` 保持 Lua 表。用户补充：实体侧脚本互调要对齐 UI 侧——`ui.get` 返回目标脚本 `self` 表本身，写错方法名当场 `attempt to call nil`；实体侧同理新增 Lua 直达面 `world.scriptOf(entity[, path])`，返回活表（无脚本返回 nil），`call` 只留给协议问询与 JS。登记走中立层的 `ScriptSelfHandle`，并入 B2-6，不新增手写 sol；`Player.lua` 的 `pokeArrival` / `interact` / `blocksEntry` 迁移跟随该 checkpoint。表只在当次使用，不跨帧存（热重载会换整张 self）。
4. B2 冻结 `spawnSprite` / `setKeyHandler` 现有语义。
5. JS 遇到新增三种值直接报错。

执行顺序：B2-1 ∥ B2-2 先行；脚本侧去硬编码（仅 Lua 与场景数据，不碰 C++）并行；InputMap 与 C++ 侧去硬编码（`placePlayerAtSpawn`、`spawnSprite` 贴图）等 B2-2 落地后。
