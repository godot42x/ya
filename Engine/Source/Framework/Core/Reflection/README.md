# 反射

给一个 C++ 类型登记成员，使序列化、拷贝、脚本和编辑器能按名字访问它。运行时规则（层次、MetaValue、facet、`type_index` 不落盘）在 `.agent/skills/reflection/SKILL.md`。

头文件：`Core/Reflection/Reflection.h`。

## 自己的类型

宏放在类体内，字段声明之前。

```cpp
struct HealthComponent : public IComponent
{
    YA_REFLECT_BEGIN(HealthComponent, IComponent)
    YA_REFLECT_FIELD(health, .manipulate(0.0f, 100.0f))
    YA_REFLECT_FIELD(cache, .notSerialized())
    YA_REFLECT_METHOD(heal, .tooltip("Add health, clamped to the max"))
    YA_REFLECT_END()

    float health = 100.0f;
    float cache  = 0.0f;
};
```

- `YA_REFLECT_BEGIN(Type)` 或 `YA_REFLECT_BEGIN(Type, Base)`。基类要已经反射。
- `YA_REFLECT_BEGIN_NAMED(Type, "PersistentName")`，或再加基类。反射名是文件里的身份，嵌套类型用它避开重名。
- `YA_REFLECT_FIELD(name)` 收集成员指针。可选的第二段是 `MetaBuilder`：`.color()`、`.tooltip("...")`、`.category("...")`、`.notSerialized()`、`.transient()`、`.manipulate(min, max)`、`.instanceEditable()`。
- `YA_REFLECT_METHOD(name)` 收集成员函数，元数据同上。
- `YA_REFLECT_END()` 之后可以用 `YA_REFLECT_VALUE_STRUCT()`（按字段进脚本）或 `YA_REFLECT_COPIES_AS_VALUE()`（拷贝走 C++ 赋值，不逐字段）。

真正的注册推迟到 `DeferredInitializerQueue::executeAll()`。需要提前触达时调用 `Type::__ensure_reflection_registered()`。

## 改不了的类型

glm 向量、脚本句柄这类类型用外部宏，放在全局作用域：

```cpp
YA_REFLECT_BEGIN_EXTERNAL(glm::vec3)
YA_REFLECT_FIELD(x)
YA_REFLECT_FIELD(y)
YA_REFLECT_FIELD(z)
YA_REFLECT_END_EXTERNAL()
```

枚举：

```cpp
YA_REFLECT_ENUM_BEGIN(EMood)
YA_REFLECT_ENUM_VALUE(Calm)
YA_REFLECT_ENUM_VALUE_NAMED(Angry, "Angry")
YA_REFLECT_ENUM_END()
```

## 查询

```cpp
Class* cls = ClassRegistry::instance().getClass("HealthComponent");
const Property* tint = cls->getProperty("tint");
for (const auto& [name, prop] : cls->properties()) {
    (void)name;
    (void)prop;
}
```

成员表没有公开的可变 map。写入走注册 API。按反射名查找，不把 `type_index` 写进文件。

## 值类型和组件 JSON

新的值类型在 `registerCoreValueTypes` 里 `registerType`，再挂它需要的 facet（相等、打印、JSON）。vec2/3/4 的 JSON 是数组，由 `FJsonFacet` 写出。

组件若要改自己的文档，登记 `FComponentJsonFacet`，不要在 `IComponent` 上加序列化虚函数。没有这份 facet 就是普通反射。Tilemap 在反射之后再改文档；Lua 脚本组件关掉反射，自己写 `scripts`。
