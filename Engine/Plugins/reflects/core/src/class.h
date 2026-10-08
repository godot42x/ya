#pragma once

#include "function.h"
#include "lib.h"
#include "reflects-core/api.h"
#include <array>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>
#include <ranges>


// ============================================================================
struct Constructor
{
    size_t                                      argCount = 0;
    std::vector<std::string>                    argTypeNames; // 参数类型名称列表
    std::string                                 signature;    // 完整签名用于唯一标识（如 "int,float,string"）
    std::function<void *(std::span<refl::MetaValue>)> factory;

    Constructor() = default;
};

// 辅助函数：生成类型签名
namespace detail
{
template <typename... Args>
std::string makeSignature()
{
    if constexpr (sizeof...(Args) == 0) {
        return "";
    }
    else {
        std::vector<std::string> types = {typeid(Args).name()...};
        std::string              sig;
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) sig += ",";
            sig += types[i];
        }
        return sig;
    }
}
} // namespace detail


template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
std::shared_ptr<T> makePtr(Args &&...args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

/// One property on the final member view of a class. `ownerPath` is the cast
/// chain from the queried object to the subobject that declares `property`:
/// empty when the queried class declares it. A name declared again further
/// down the hierarchy replaces the inherited one.
struct ResolvedProperty
{
    const Property *property = nullptr;
    std::vector<refl::type_index_t> ownerPath;
};

/// One function on the final member view. Same override and owner-path rules
/// as `ResolvedProperty`.
struct ResolvedFunction
{
    const Function *function = nullptr;
    std::vector<refl::type_index_t> ownerPath;
};

struct ResolvedMembers
{
    std::unordered_map<std::string, ResolvedProperty> properties;
    std::unordered_map<std::string, ResolvedFunction> functions;
    // Parent-first. A reflected name declared again is moved to the end, so a
    // consumer that collapses names (script names strip a leading '_') still
    // sees the later declaration last.
    std::vector<std::string> propertyOrder;
    std::vector<std::string> functionOrder;
};

struct Class : public Field
{
    refl::type_index_t typeIndex = 0; // 类型索引，用于快速查找和父类指针转换

    // TODO: 使用一个field 来存所有-> 省内存
    // std::unordered_map<std::string, std::shared_ptr<Field>> fields;
    // 属性和函数存储
    std::unordered_map<std::string, Property> properties;
    std::vector<std::string>                  propertyOrder;
    std::unordered_map<std::string, Function> functions;

    // 构造函数工厂 - 支持多个重载
    // key: 类型签名字符串（如 "int,float,string"），value: 构造函数描述符
    std::unordered_map<std::string, Constructor> constructors;

    std::vector<refl::type_index_t>                   parents;
    std::unordered_map<refl::type_index_t, ptrdiff_t> parentOffsets; // 父类指针偏移量（支持负偏移）

    // 虚继承转换器（fallback 方案）
    // 说明：虚继承（virtual public）在实际项目中极少用到，绝大多数场景只需支持普通继承和接口继承。
    //       本转换器仅为演示/兼容保留，推荐实际项目不处理虚继承，避免运行时性能损耗。
    std::unordered_map<refl::type_index_t, std::function<void *(void *)>> virtualParentConverters;

    // 析构函数 - 用于释放动态创建的实例
    std::function<void(void *)> destructor;

    // 整值拷贝（ReflectionCopier 用）。null = 默认的逐字段递归拷贝。
    // 由 Register::copiesAsValue() 设置：值类型声明“C++ 拷贝赋值即真相”，
    // 拷贝构造维护的派生状态（共享且已绑定的资产句柄）随整体赋值存活，
    // 逐字段拷贝无法表达它。
    bool (*copyAssign)(void *dst, const void *src) = nullptr;

    Class() = default;
    explicit Class(const std::string &inName)
    {
        name = inName;
    }
    virtual ~Class() = default;



  public:
#pragma region Inheritance

    template <typename T, typename Parent>
    constexpr Class &registerParent()
    {
        refl::type_index_t parentTypeId = refl::type_index_v<Parent>;

        if (std::ranges::find(parents, parentTypeId) != parents.end()) {
            return *this;
        }

        // 检测是否为虚继承
        // constexpr bool bVirtualInheritance = detectVirtualInheritance<T, Parent>();
        constexpr bool bVirtualInheritance = false;

        // 虚继承 fallback 仅为演示/兼容保留，实际项目可直接 static_assert(!isVirtual)
        if constexpr (bVirtualInheritance) {
            // [不推荐] 虚继承：使用动态转换器（编译器会生成正确的虚基表查找）
            // 建议实际项目直接 static_assert(!isVirtual, "虚继承不被支持");
            virtualParentConverters[parentTypeId] = [](void *childPtr) -> void * {
                T      *child  = static_cast<T *>(childPtr);
                Parent *parent = static_cast<Parent *>(child); // 编译器处理虚基表
                return static_cast<void *>(parent);
            };
        }
        else {
            // 普通继承：计算静态偏移量
            auto calcOffset = []() {
                alignas(T) char buffer[sizeof(T)];
                T              *child  = reinterpret_cast<T *>(buffer);
                Parent         *parent = static_cast<Parent *>(child);
                return reinterpret_cast<char *>(parent) - reinterpret_cast<char *>(child);
            };

            ptrdiff_t offset            = calcOffset();
            parentOffsets[parentTypeId] = offset;
        }

        parents.push_back(parentTypeId);
        return *this;
    }


    // 检测虚继承（使用 SFINAE 和类型萃取）
    template <typename T, typename Parent>
    static constexpr bool detectVirtualInheritance()
    {
#if defined(__GNUC__) || defined(__clang__)
        // GCC/Clang: 直接返回布尔值
        return __is_virtual_base_of<typename std::remove_cv<T>::type, typename std::remove_cv<Parent>::type>() != 0;
#elif defined(_MSC_VER) && _MSC_VER >= 1900
        // MSVC: 保守策略 - 暂时禁用虚继承检测，统一使用动态转换
        // 原因: __is_virtual_base_of 在 constexpr 上下文中有兼容性问题
        return false; // 改为 false 以使用静态偏移，虚继承需手动标记
#else
        // 其他编译器：保守策略
        return false;
#endif
    }

  public:

    /// Inheritance expanded, derived names winning. Does not filter by language.
    REFLECTS_CORE_API ResolvedMembers resolveMembers() const;

    // 获取父类子对象指针（自动处理虚继承和非虚继承）
    void *getParentPointer(void *childPtr, refl::type_index_t parentTypeId) const
    {
        // 1. 优先尝试静态偏移（高效，适用于普通继承和多重继承）
        auto offsetIt = parentOffsets.find(parentTypeId);
        if (offsetIt != parentOffsets.end()) {
            return static_cast<char *>(childPtr) + offsetIt->second;
        }

        // 2. Fallback 到动态转换器（虚继承、菱形继承）
        auto converterIt = virtualParentConverters.find(parentTypeId);
        if (converterIt != virtualParentConverters.end()) {
            return converterIt->second(childPtr);
        }

        // 3. 未注册的父类
        return nullptr;
    }


    REFLECTS_CORE_API Class *getClassByTypeId(refl::type_index_t typeId) const;

    // 访问所有属性。`recursive` 没有默认值：调用方必须说明是否要走到父类。
    // （默认值曾经是 true，后来为了修正属性遍历顺序被改成 false，于是"名字说
    // 了访问全部、行为却不是"的歧义就留在调用点上。）
    // 非 const 版本（用于修改操作）
    template <typename VisitorFunc>
    void visitAllProperties(void *obj, VisitorFunc &&visitor, bool recursive) const
    {
        if (recursive) {
            for (auto parentTypeId : parents) {
                // 从注册表获取父类 Class
                Class *parentClass = getClassByTypeId(parentTypeId);
                if (parentClass) {
                    void *parentObj = getParentPointer(obj, parentTypeId);
                    if (parentObj) {
                        parentClass->visitAllProperties(parentObj, std::forward<VisitorFunc>(visitor), true);
                    }
                }
            }
        }

        // 访问当前类的属性
        visitOwnProperties([&](const std::string &name, const Property &prop) {
            std::forward<VisitorFunc>(visitor)(name, prop, obj);
        });
    }

    // 访问所有属性。const 版本（用于只读操作，如序列化）
    template <typename VisitorFunc>
    void visitAllProperties(const void *obj, VisitorFunc &&visitor, bool recursive) const
    {
        if (recursive) {

            for (auto parentTypeId : parents) {
                // 从注册表获取父类 Class
                Class *parentClass = getClassByTypeId(parentTypeId);
                if (parentClass) {
                    const void *parentObj = getParentPointer(const_cast<void *>(obj), parentTypeId);
                    if (parentObj) {
                        parentClass->visitAllProperties(parentObj, std::forward<VisitorFunc>(visitor), true);
                    }
                }
            }
        }

        // 2. 访问当前类的属性
        visitOwnProperties([&](const std::string &name, const Property &prop) {
            std::forward<VisitorFunc>(visitor)(name, prop, obj);
        });
    }

    // 按类分组访问属性（用于序列化等需要区分类层次的场景）
    // visitor 签名: void(const Class* classPtr, const std::string& propName, const Property& prop, const void* propObj)
    template <typename VisitorFunc>
    void visitPropertiesByClass(const void *obj, VisitorFunc &&visitor, bool recursive = true) const
    {
        if (recursive) {
            // 1. 递归访问父类
            for (auto parentTypeId : parents) {
                Class *parentClass = getClassByTypeId(parentTypeId);
                if (parentClass) {
                    const void *parentObj = getParentPointer(const_cast<void *>(obj), parentTypeId);
                    if (parentObj) {
                        parentClass->visitPropertiesByClass(parentObj, std::forward<VisitorFunc>(visitor), true);
                    }
                }
            }
        }

        // 2. 访问当前类的属性，传递 Class 指针
        visitOwnProperties([&](const std::string &name, const Property &prop) {
            std::forward<VisitorFunc>(visitor)(this, name, prop, obj);
        });
    }

    // 非 const 版本
    template <typename VisitorFunc>
    void visitPropertiesByClass(void *obj, VisitorFunc &&visitor, bool recursive = true) const
    {
        return visitPropertiesByClass(const_cast<const void *>(obj), std::forward<VisitorFunc>(visitor), recursive);
    }
#pragma endregion

// ------------------------------------------------------------------------
// 字段注册接口全部 public，便于 Register 调用
#pragma region Field-Register

  public:
    // 提取共用逻辑：初始化 Property 的基础字段
    template <typename T>
    static void initPropertyBase(Property &prop, const std::string &inName, bool isConst, bool isStatic)
    {
        prop.name      = inName;
        prop.bConst    = isConst;
        prop.bStatic   = isStatic;
        prop.typeIndex = refl::type_index_v<T>;
        prop.typeName  = detail::getTypeName<T>();

        // Detect pointer types and set pointee type index
        if constexpr (std::is_pointer_v<T>) {
            prop.bPointer         = true;
            using PointeeType     = std::remove_pointer_t<T>;
            using Bare            = std::remove_cv_t<PointeeType>;
            prop.pointeeTypeIndex = refl::type_index_v<PointeeType>;
            prop.pointeeOf        = [](void *member) -> refl::MetaValue {
                auto *slot = static_cast<T *>(member);
                PointeeType *target = *slot;
                return refl::MetaValue::ref(refl::type_index_v<Bare>, const_cast<Bare *>(target), std::is_const_v<PointeeType>);
            };
        }
        // is std::shared_ptr
        else if constexpr (requires(T t) { std::remove_cvref_t<T>::element_type; }) {
            prop.bPointer         = true;
            using Held            = std::remove_cv_t<T>;
            using PointeeType     = typename Held::element_type;
            using Bare            = std::remove_cv_t<PointeeType>;
            prop.pointeeTypeIndex = refl::type_index_v<PointeeType>;
            prop.pointeeOf        = [](void *member) -> refl::MetaValue {
                auto *slot = static_cast<Held *>(member);
                PointeeType *target = slot->get();
                return refl::MetaValue::ref(refl::type_index_v<Bare>, const_cast<Bare *>(target), std::is_const_v<PointeeType>);
            };
        }
        else {
            prop.bPointer         = false;
            prop.pointeeTypeIndex = 0;
            prop.pointeeOf        = nullptr;
        }
    }

    // 共用逻辑：插入或更新 property 并返回引用
    Property &insertProperty(const std::string &inName, Property &&prop)
    {
        const bool bExists = properties.find(inName) != properties.end();
        auto       it      = properties.insert_or_assign(inName, std::move(prop));
        if (!bExists) {
            // TODO: optimize the visitor
            propertyOrder.push_back(inName);
        }
        return it.first->second;
    }

    template <typename VisitorFunc>
    void visitOwnProperties(VisitorFunc &&visitor) const
    {
        for (const auto &name : propertyOrder) {
            auto it = properties.find(name);
            if (it != properties.end()) {
                std::forward<VisitorFunc>(visitor)(name, it->second);
            }
        }
    }

    template <typename VisitorFunc>
    void visitOwnProperties(VisitorFunc &&visitor)
    {
        for (const auto &name : propertyOrder) {
            auto it = properties.find(name);
            if (it != properties.end()) {
                std::forward<VisitorFunc>(visitor)(name, it->second);
            }
        }
    }

    // Register normal member variable (read-write)
    template <typename ClassType, typename T>
    Property &property(const std::string &inName, T ClassType::*member)
    {
        Property prop;
        initPropertyBase<T>(prop, inName, false, false);

        prop.addressGetter = [member](const void *obj) -> const void * {
            return &(static_cast<const ClassType *>(obj)->*member);
        };

        prop.addressGetterMutable = [member](void *obj) -> void * {
            return &(static_cast<ClassType *>(obj)->*member);
        };

        return insertProperty(inName, std::move(prop));
    }

    // Register const member variable (read-only)
    template <typename ClassType, typename T>
    Property &property(const std::string &inName, const T ClassType::*member)
    {
        Property prop;
        initPropertyBase<T>(prop, inName, true, false);

        prop.addressGetter = [member](const void *obj) -> const void * {
            return &(static_cast<const ClassType *>(obj)->*member);
        };

        prop.addressGetterMutable = nullptr;

        return insertProperty(inName, std::move(prop));
    }

    // Register static variable (read-write)
    template <typename T>
    Property &staticProperty(const std::string &inName, T *staticVar)
    {
        Property prop;
        initPropertyBase<T>(prop, inName, false, true);

        prop.addressGetter = [staticVar](const void * /*obj*/) -> const void * {
            return staticVar;
        };
        prop.addressGetterMutable = [staticVar](void * /*obj*/) -> void * {
            return staticVar;
        };

        return insertProperty(inName, std::move(prop));
    }

    // Register const static variable (read-only)
    template <typename T>
    Property &staticProperty(const std::string &inName, const T *staticVar)
    {
        Property prop;
        initPropertyBase<T>(prop, inName, true, true);

        prop.addressGetter = [staticVar](const void * /*obj*/) -> const void * {
            return staticVar;
        };
        prop.addressGetterMutable = nullptr;

        return insertProperty(inName, std::move(prop));
    }



    // 同名先到者胜：已有条目保持不动，重复注册走 duplicateFunctionHook。
    REFLECTS_CORE_API Function &insertFunction(Function function);

    // ------------------------------------------------------------------------
    // 注册普通成员函数
    // ------------------------------------------------------------------------
    template <typename Ret, typename... Args>
    static void fillSignature(Function &f, const std::string &inName, FunctionType kind)
    {
        f.name           = inName;
        f.type           = kind;
        f.argCount       = sizeof...(Args);
        f.returnTypeName = detail::getTypeName<Ret>();
        if constexpr (sizeof...(Args) > 0) {
            (f.argTypeNames.push_back(detail::getTypeName<Args>()), ...);
            f.args = {refl::describeParam<Args>()...};
        }
        f.ret = refl::describeReturn<Ret>();
    }

    template <typename Ret, typename ClassType, typename... Args>
    Function &function(const std::string &inName, Ret (ClassType::*func)(Args...))
    {
        Function f;
        fillSignature<Ret, Args...>(f, inName, FunctionType::MemberFunction);

        f.invoker = [func](void *obj, std::span<refl::MetaValue> args) -> refl::MetaValue {
            ClassType *self = static_cast<ClassType *>(obj);
            return detail::memberFunctionWrapper(self, func, args);
        };

        return insertFunction(std::move(f));
    }

    // ------------------------------------------------------------------------
    // 注册 const 成员函数
    // ------------------------------------------------------------------------
    template <typename Ret, typename ClassType, typename... Args>
    Function &function(const std::string &inName, Ret (ClassType::*func)(Args...) const)
    {
        Function f;
        fillSignature<Ret, Args...>(f, inName, FunctionType::ConstMemberFunction);

        f.invoker = [func](void *obj, std::span<refl::MetaValue> args) -> refl::MetaValue {
            const ClassType *self = static_cast<const ClassType *>(obj);
            return detail::constMemberFunctionWrapper(self, func, args);
        };

        return insertFunction(std::move(f));
    }

    // ------------------------------------------------------------------------
    // 注册静态成员函数
    // ------------------------------------------------------------------------
    template <typename Ret, typename... Args>
    Function &staticFunction(const std::string &inName, Ret (*func)(Args...))
    {
        Function f;
        fillSignature<Ret, Args...>(f, inName, FunctionType::StaticMemberFunction);

        f.invoker = [func](void * /*obj*/, std::span<refl::MetaValue> args) -> refl::MetaValue {
            return detail::staticFunctionWrapper(func, args);
        };

        return insertFunction(std::move(f));
    }

#pragma endregion


#pragma region Invocation
    // ------------------------------------------------------------------------
    // 底层调用。args 可写，输出参数的结果留在调用方的 MetaValue 里。
    // ------------------------------------------------------------------------
    refl::MetaValue invoke(const std::string &inName, void *obj, std::span<refl::MetaValue> args)
    {
        auto it = functions.find(inName);
        if (it == functions.end()) {
            throw std::runtime_error("Function not found: " + inName);
        }

        Function &f = it->second;

        if (args.size() != f.argCount) {
            throw std::runtime_error("Argument count mismatch for function: " + inName);
        }

        if (f.isStatic()) {
            return f.invoker(nullptr, args);
        }
        else {
            if (!obj) {
                throw std::runtime_error("Object pointer is null for member function: " +
                                         inName);
            }
            return f.invoker(obj, args);
        }
    }

    // ------------------------------------------------------------------------
    // 调用静态函数（不需要对象实例）
    // ------------------------------------------------------------------------
    refl::MetaValue invokeStatic(const std::string &inName, std::span<refl::MetaValue> args)
    {
        auto it = functions.find(inName);
        if (it == functions.end()) {
            throw std::runtime_error("Function not found: " + inName);
        }

        Function &f = it->second;

        if (!f.isStatic()) {
            throw std::runtime_error("Function is not static: " + inName);
        }

        if (args.size() != f.argCount) {
            throw std::runtime_error("Argument count mismatch for function: " + inName);
        }

        return f.invoker(nullptr, args);
    }

    // 实参装箱见 refl::boxCallArg：右值 / 按值 → make；非 const 左值 → ref
    // （写入落在调用方对象上）；const 左值 → cref；指针 → 指向 pointee 的 Ref。
    template <typename Ret, typename Self, typename... Args>
    Ret invoke(const std::string &inName, Self *obj, Args &&...args)
    {
        if constexpr (sizeof...(Args) == 0) {
            refl::MetaValue result = invoke(inName, obj, {});
            if constexpr (std::is_void_v<Ret>) {
                return;
            }
            else {
                return refl::takeResult<Ret>(std::move(result));
            }
        }
        else {
            std::array<refl::MetaValue, sizeof...(Args)> boxed{refl::boxCallArg(std::forward<Args>(args))...};
            refl::MetaValue result = invoke(inName, obj, boxed);
            if constexpr (std::is_void_v<Ret>) {
                return;
            }
            else {
                return refl::takeResult<Ret>(std::move(result));
            }
        }
    }

    template <typename Ret, typename... Args>
    Ret invokeStatic(const std::string &inName, Args &&...args)
    {
        if constexpr (sizeof...(Args) == 0) {
            refl::MetaValue result = invokeStatic(inName, {});
            if constexpr (std::is_void_v<Ret>) {
                return;
            }
            else {
                return refl::takeResult<Ret>(std::move(result));
            }
        }
        else {
            std::array<refl::MetaValue, sizeof...(Args)> boxed{refl::boxCallArg(std::forward<Args>(args))...};
            refl::MetaValue result = invokeStatic(inName, boxed);
            if constexpr (std::is_void_v<Ret>) {
                return;
            }
            else {
                return refl::takeResult<Ret>(std::move(result));
            }
        }
    }

#pragma region Query

    bool hasFunction(const std::string &inName) const
    {
        return functions.find(inName) != functions.end();
    }

    const Function *getFunction(const std::string &inName) const
    {
        auto it = functions.find(inName);
        return it != functions.end() ? &it->second : nullptr;
    }

    // ------------------------------------------------------------------------
    // 查询属性信息
    // ------------------------------------------------------------------------
    bool hasProperty(const std::string &inName) const
    {
        return properties.find(inName) != properties.end();
    }

    const Property *getProperty(const std::string &inName) const
    {
        auto it = properties.find(inName);
        return it != properties.end() ? &it->second : nullptr;
    }

    Property *getProperty(const std::string &inName)
    {
        auto it = properties.find(inName);
        return it != properties.end() ? &it->second : nullptr;
    }

    // 递归查找属性（包括父类属性）
    const Property *findPropertyRecursive(const std::string &inName) const
    {
        // 1. 先查找当前类的属性
        auto it = properties.find(inName);
        if (it != properties.end()) {
            return &it->second;
        }

        // 2. 递归查找父类的属性
        for (auto parentTypeId : parents) {
            Class *parentClass = getClassByTypeId(parentTypeId);
            if (parentClass) {
                const Property *prop = parentClass->findPropertyRecursive(inName);
                if (prop) {
                    return prop;
                }
            }
        }

        return nullptr;
    }

    // 递归查找属性（包括父类属性）- 非 const 版本
    Property *findPropertyRecursive(const std::string &inName)
    {
        // 复用 const 版本的实现
        return const_cast<Property *>(
            static_cast<const Class *>(this)->findPropertyRecursive(inName));
    }

    // 查找属性并返回其所属的类和类型索引（用于反序列化等需要知道属性归属的场景）
    // 返回: tuple<Property*, Class*, typeIndex>，如果未找到则返回 {nullptr, nullptr, 0}
    std::tuple<const Property *, const Class *, refl::type_index_t> findPropertyWithOwner(const std::string &inName, refl::type_index_t currentTypeId = 0) const
    {
        // 1. 先查找当前类的属性
        auto it = properties.find(inName);
        if (it != properties.end()) {
            return {&it->second, this, currentTypeId};
        }

        // 2. 递归查找父类的属性
        for (auto parentTypeId : parents) {
            Class *parentClass = getClassByTypeId(parentTypeId);
            if (parentClass) {
                auto [prop, owner, ownerTypeId] = parentClass->findPropertyWithOwner(inName, parentTypeId);
                if (prop) {
                    return {prop, owner, ownerTypeId};
                }
            }
        }

        return {nullptr, nullptr, 0};
    }

    // 非 const 版本
    std::tuple<Property *, Class *, refl::type_index_t> findPropertyWithOwner(const std::string &inName, refl::type_index_t currentTypeId = 0)
    {
        auto [prop, owner, ownerTypeId] = static_cast<const Class *>(this)->findPropertyWithOwner(inName, currentTypeId);
        return {const_cast<Property *>(prop), const_cast<Class *>(owner), ownerTypeId};
    }
#pragma endregion

#pragma region getter-setter
    // ------------------------------------------------------------------------
    // 高级属性访问接口
    // ------------------------------------------------------------------------

    // 获取属性值（通用接口）
    template <typename T, typename Self>
    T getPropertyValue(const std::string &inName, Self *obj)
    {
        auto it = properties.find(inName);
        if (it == properties.end()) {
            throw std::runtime_error("Property not found: " + inName);
        }

        Property &prop = it->second;

        if (prop.bStatic) {
            // 静态属性不需要对象实例
            return prop.getValue<T>(nullptr);
        }
        else {
            if (!obj) {
                throw std::runtime_error("Object pointer is null for member property: " + inName);
            }
            return prop.getValue<T>(obj);
        }
    }

    // 设置属性值（通用接口）
    template <typename T, typename Self>
    void setPropertyValue(const std::string &inName, Self *obj, const T &value)
    {
        auto it = properties.find(inName);
        if (it == properties.end()) {
            throw std::runtime_error("Property not found: " + inName);
        }

        Property &prop = it->second;

        if (prop.bConst) {
            throw std::runtime_error("Cannot modify const property: " + inName);
        }

        if (prop.bStatic) {
            // 静态属性不需要对象实例
            prop.setValue<T>(nullptr, value);
        }
        else {
            if (!obj) {
                throw std::runtime_error("Object pointer is null for member property: " + inName);
            }
            prop.setValue<T>(obj, value);
        }
    }

    // 获取静态属性值
    template <typename T>
    T getStaticPropertyValue(const std::string &inName)
    {
        auto it = properties.find(inName);
        if (it == properties.end()) {
            throw std::runtime_error("Property not found: " + inName);
        }

        Property &prop = it->second;

        if (!prop.bStatic) {
            throw std::runtime_error("Property is not static: " + inName);
        }

        return prop.getValue<T>(nullptr);
    }

    // 设置静态属性值
    template <typename T>
    void setStaticPropertyValue(const std::string &name, const T &value)
    {
        auto it = properties.find(name);
        if (it == properties.end()) {
            throw std::runtime_error("Property not found: " + name);
        }

        Property &prop = it->second;

        if (!prop.bStatic) {
            throw std::runtime_error("Property is not static: " + name);
        }

        if (prop.bConst) {
            throw std::runtime_error("Cannot modify const static property: " + name);
        }

        prop.setValue<T>(nullptr, value);
    }

    const std::string &getName() const { return name; }
#pragma endregion

#pragma region Constructor Registration

    // 注册构造函数（支持0个或多个参数）
    template <typename T, typename... Args>
    void registerConstructor()
    {
        std::string sig = detail::makeSignature<Args...>();

        Constructor ctor;
        ctor.argCount = sizeof...(Args);
        if constexpr (sizeof...(Args) > 0) {
            ctor.argTypeNames = {typeid(Args).name()...};
        }
        ctor.signature = sig;
        ctor.factory   = [](std::span<refl::MetaValue> args) -> void * {
            if (args.size() != sizeof...(Args)) {
                throw std::runtime_error("Constructor argument count mismatch: expected " +
                                         std::to_string(sizeof...(Args)) + ", got " +
                                         std::to_string(args.size()));
            }

            if constexpr (sizeof...(Args) == 0) {
                return static_cast<void *>(new T());
            }
            else {
                auto construct = [&args]<size_t... I>(std::index_sequence<I...>) -> T * {
                    std::tuple<refl::BoundArg<Args>...> bound{refl::bindArg<Args>(args[I])...};
                    return new T(std::get<I>(bound).get()...);
                };
                return static_cast<void *>(construct(std::index_sequence_for<Args...>{}));
            }
        };

        constructors[sig] = ctor;

        // 只设置一次析构函数
        if (!destructor) {
            destructor = [](void *obj) {
                delete static_cast<T *>(obj);
            };
        }
    }
#pragma endregion


#pragma region Instance-Creation
    // 创建实例（无参数）- 自动选择0参数的构造函数
    void *createInstance() const
    {
        std::string sig = detail::makeSignature<>();
        auto        it  = constructors.find(sig);
        if (it == constructors.end()) {
            throw std::runtime_error("No default constructor registered for class: " + name);
        }
        return it->second.factory({});
    }

    // 创建实例（带参数）。装箱约定与 Class::invoke 相同，见 refl::boxCallArg。
    template <typename... Args>
    void *createInstance(Args &&...args) const
    {
        std::string sig = detail::makeSignature<std::decay_t<Args>...>();
        auto        it  = constructors.find(sig);
        if (it == constructors.end()) {
            throw std::runtime_error("No matching constructor for signature '" + sig +
                                     "' registered for class: " + name);
        }
        if constexpr (sizeof...(Args) == 0) {
            return it->second.factory({});
        }
        else {
            std::array<refl::MetaValue, sizeof...(Args)> boxed{refl::boxCallArg(std::forward<Args>(args))...};
            return it->second.factory(boxed);
        }
    }

    // 检查是否可以创建实例（是否有任何构造函数）
    bool canCreateInstance() const
    {
        return !constructors.empty();
    }

    // 检查是否有特定签名的构造函数
    template <typename... Args>
    bool hasConstructor() const
    {
        std::string sig = detail::makeSignature<Args...>();
        return constructors.find(sig) != constructors.end();
    }

  public:
    // 销毁实例
    void destroyInstance(void *obj) const
    {
        if (!destructor) {
            throw std::runtime_error("No destructor registered for class: " + name);
        }
        if (!obj) {
            throw std::runtime_error("Cannot destroy null object");
        }
        destructor(obj);
    }

    // 获取所有已注册构造函数的参数数量列表
    std::vector<size_t> getConstructorArgCounts() const
    {
        std::vector<size_t> counts;
        counts.reserve(constructors.size());
        for (const auto &[sig, ctor] : constructors) {
            counts.push_back(ctor.argCount);
        }
        return counts;
    }

    // 获取所有已注册构造函数的签名列表
    std::vector<std::string> getConstructorSignatures() const
    {
        std::vector<std::string> sigs;
        sigs.reserve(constructors.size());
        for (const auto &[sig, _] : constructors) {
            sigs.push_back(sig);
        }
        return sigs;
    }
#pragma endregion
};
