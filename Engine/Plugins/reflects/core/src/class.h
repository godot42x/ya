#pragma once

#include "function.h"
#include "lib.h"
#include <memory>


// ============================================================================
struct Constructor
{
    size_t                                      argCount = 0;
    std::vector<std::string>                    argTypeNames; // 参数类型名称列表
    std::string                                 signature;    // 完整签名用于唯一标识（如 "int,float,string"）
    std::function<void *(const ArgumentList &)> factory;

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

struct Class
{
    std::string _name;

    // TODO: 使用一个field 来存所有-> 省内存
    // std::unordered_map<std::string, std::shared_ptr<Field>> fields;
    // 属性和函数存储
    std::unordered_map<std::string, Property> properties;
    std::unordered_map<std::string, Function> functions;

    // 构造函数工厂 - 支持多个重载
    // key: 类型签名字符串（如 "int,float,string"），value: 构造函数描述符
    std::unordered_map<std::string, Constructor> constructors;

    // 析构函数 - 用于释放动态创建的实例
    std::function<void(void *)> destructor;

    Class() = default;
    explicit Class(const std::string &inName) : _name(inName) {}
    virtual ~Class() = default;


#pragma region Field-Register

  private:
    // 提取共用逻辑：初始化 Property 的基础字段
    template <typename T>
    static void initPropertyBase(Property &prop, const std::string &inName, bool isConst, bool isStatic)
    {
        prop.name      = inName;
        prop.bConst    = isConst;
        prop.bStatic   = isStatic;
        prop.typeIndex = TYPE_ID(T);
        prop.typeName  = detail::getTypeName<T>();
    }

    // 共用逻辑：插入或更新 property 并返回引用
    Property &insertProperty(const std::string &inName, Property &&prop)
    {
        auto it = properties.insert_or_assign(inName, std::move(prop));
        return it.first->second;
    }

  public:
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



    // ------------------------------------------------------------------------
    // 注册普通成员函数
    // ------------------------------------------------------------------------
    template <typename Ret, typename ClassType, typename... Args>
    Function &function(const std::string &inName, Ret (ClassType::*func)(Args...))
    {
        Function f;
        f.name           = inName;
        f.type           = FunctionType::MemberFunction;
        f.argCount       = sizeof...(Args);
        f.returnTypeName = detail::getTypeName<Ret>();
        (f.argTypeNames.push_back(detail::getTypeName<Args>()), ...);

        f.invoker = [func](void *obj, const ArgumentList &args) -> std::any {
            ClassType *self = static_cast<ClassType *>(obj);
            return detail::memberFunctionWrapper(self, func, args);
        };

        auto it = functions.insert_or_assign(inName, std::move(f));
        return it.first->second;
    }

    // ------------------------------------------------------------------------
    // 注册 const 成员函数
    // ------------------------------------------------------------------------
    template <typename Ret, typename ClassType, typename... Args>
    Function &function(const std::string &inName, Ret (ClassType::*func)(Args...) const)
    {
        Function f;
        f.name           = inName;
        f.type           = FunctionType::ConstMemberFunction;
        f.argCount       = sizeof...(Args);
        f.returnTypeName = detail::getTypeName<Ret>();
        (f.argTypeNames.push_back(detail::getTypeName<Args>()), ...);

        f.invoker = [func](void *obj, const ArgumentList &args) -> std::any {
            const ClassType *self = static_cast<const ClassType *>(obj);
            return detail::constMemberFunctionWrapper(self, func, args);
        };

        auto it = functions.insert_or_assign(inName, std::move(f));
        return it.first->second;
    }

    // ------------------------------------------------------------------------
    // 注册静态成员函数
    // ------------------------------------------------------------------------
    template <typename Ret, typename... Args>
    Function &staticFunction(const std::string &inName, Ret (*func)(Args...))
    {
        Function f;
        f.name           = inName;
        f.type           = FunctionType::StaticMemberFunction;
        f.argCount       = sizeof...(Args);
        f.returnTypeName = detail::getTypeName<Ret>();
        (f.argTypeNames.push_back(detail::getTypeName<Args>()), ...);

        f.invoker = [func](void * /*obj*/, const ArgumentList &args) -> std::any {
            return detail::staticFunctionWrapper(func, args);
        };

        auto it = functions.insert_or_assign(inName, std::move(f));
        return it.first->second;
    }

#pragma endregion


#pragma region Invocation
    // ------------------------------------------------------------------------
    // 底层调用接口：使用 ArgumentList
    // ------------------------------------------------------------------------
    std::any invoke(const std::string &inName, void *obj, const ArgumentList &args)
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
    std::any invokeStatic(const std::string &inName, const ArgumentList &args)
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

    // ------------------------------------------------------------------------
    // 高层调用接口：类型安全的模板方法
    // ------------------------------------------------------------------------
    template <typename Ret, typename Self, typename... Args>
    Ret call(const std::string &inName, Self *obj, Args &&...args)
    {
        ArgumentList argList = ArgumentList::make(std::forward<Args>(args)...);
        std::any     result  = invoke(inName, obj, argList);

        if constexpr (std::is_void_v<Ret>) {
            return;
        }
        else {
            return std::any_cast<Ret>(result);
        }
    }

    // ------------------------------------------------------------------------
    // 高层调用接口：静态函数调用
    // ------------------------------------------------------------------------
    template <typename Ret, typename... Args>
    Ret callStatic(const std::string &inName, Args &&...args)
    {
        ArgumentList argList = ArgumentList::make(std::forward<Args>(args)...);
        std::any     result  = invokeStatic(inName, argList);

        if constexpr (std::is_void_v<Ret>) {
            return;
        }
        else {
            return std::any_cast<Ret>(result);
        }
    }
#pragma endregion

    // MARK:  Query
    // ------------------------------------------------------------------------
    // 查询函数信息
    // ------------------------------------------------------------------------
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

    // ========================================================================
    // Constructor Registration and Instance Creation
    // ========================================================================

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
        ctor.factory   = [](const ArgumentList &args) -> void   *{
            if (args.size() != sizeof...(Args)) {
                throw std::runtime_error("Constructor argument count mismatch: expected " +
                                         std::to_string(sizeof...(Args)) + ", got " +
                                         std::to_string(args.size()));
            }

            if constexpr (sizeof...(Args) == 0) {
                return static_cast<void *>(new T());
            }
            else {
                auto construct = [&args]<size_t... I>(std::index_sequence<I...>) -> T   *{
                    return new T(args.get<Args>(I)...);
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
            throw std::runtime_error("No default constructor registered for class: " + _name);
        }
        ArgumentList emptyArgs;
        return it->second.factory(emptyArgs);
    }

    void *createInstance(const ArgumentList &&args) const = delete;
    void *createInstance(const ArgumentList &args) const  = delete;
    void *createInstance(const ArgumentList args) const   = delete;
    // 创建实例（带参数）- 根据参数类型签名选择构造函数
    template <typename... Args>
    void *createInstance(Args &&...args) const
    {
        if constexpr (sizeof...(Args) > 0) {
            static_assert(!std::is_same_v<std::tuple_element_t<0, std::tuple<Args...>>, ArgumentList>,
                          "Use ArgumentList to pass arguments directly.");
        }

        std::string sig = detail::makeSignature<std::decay_t<Args>...>();
        auto        it  = constructors.find(sig);
        if (it == constructors.end()) {
            throw std::runtime_error("No matching constructor for signature '" + sig +
                                     "' registered for class: " + _name);
        }
        auto argList = ArgumentList::make(std::forward<Args>(args)...);
        return it->second.factory(argList);
    }


    // 销毁实例
    void destroyInstance(void *obj) const
    {
        if (!destructor) {
            throw std::runtime_error("No destructor registered for class: " + _name);
        }
        if (!obj) {
            throw std::runtime_error("Cannot destroy null object");
        }
        destructor(obj);
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
