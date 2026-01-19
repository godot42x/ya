#pragma once

#include "class.h"
#include "enum.h"
#include "lib.h"
#include "type_index.h"



#include <algorithm>
#include <any>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>


// namespace refl

// MARK: ClassRegistry
struct ClassRegistry
{
    std::unordered_map<std::string, std::shared_ptr<Class>> classes;
    std::unordered_map<uint32_t, std::shared_ptr<Class>>    typeIdMap;

    // parent -> childrens
    std::unordered_map<refl::type_index_t, std::vector<refl::type_index_t>> parentToChildren;

    std::vector<std::function<void()>> postStaticInitializers;

    static ClassRegistry &instance();

    template <typename T>
    std::shared_ptr<Class> registerClass(const std::string &name, Class *classInfo)
    {
        auto ptr      = std::shared_ptr<Class>(classInfo);
        auto id       = refl::type_index_v<T>;
        
        // 设置类型索引
        ptr->typeIndex = id;
        
        classes[name] = ptr;
        typeIdMap[id] = ptr;
        printf("_____ Registered class: %s (typeId: %zu)\n", name.c_str(), (uint64_t)id);

        return classes[name];
    }

    Class *getClass(uint32_t typeId)
    {
        auto it = typeIdMap.find(typeId);
        return it != typeIdMap.end() ? it->second.get() : nullptr;
    }

    Class *getClass(const std::string &name)
    {
        auto it = classes.find(name);
        return it != classes.end() ? it->second.get() : nullptr;
    }

    // 通过类名字符串动态创建实例
    template <typename... Args>
    void *createInstance(const std::string &className, Args &&...args)
    {
        Class *cls = getClass(className);
        if (!cls) {
            throw std::runtime_error("Class not found: " + className);
        }
        return cls->createInstance(std::forward<Args>(args)...);
    }

    // 销毁动态创建的实例
    void destroyInstance(const std::string &className, void *obj)
    {
        Class *cls = getClass(className);
        if (!cls) {
            throw std::runtime_error("Class not found: " + className);
        }
        cls->destroyInstance(obj);
    }

    // MARK: Inheritance
    void registerInheritance(uint32_t childTypeId, uint32_t parentTypeId)
    {
        parentToChildren[parentTypeId].push_back(childTypeId);
    }

    bool isDerivedFrom(uint32_t childTypeId, uint32_t parentTypeId)
    {
        auto it = parentToChildren.find(parentTypeId);
        if (it != parentToChildren.end()) {
            const auto &children = it->second;
            return std::ranges::find(children, childTypeId) != children.end();
        }
        return false;
    }


    // 检查类是否已注册
    bool hasClass(const std::string &name) const
    {
        return classes.find(name) != classes.end();
    }
    bool hasClass(uint32_t typeId) const
    {
        return typeIdMap.find(typeId) != typeIdMap.end();
    }

    void addPostStaticInitializer(std::function<void()> &&initFunc)
    {
        postStaticInitializers.push_back(std::move(initFunc));
    }
    void executeAllPostStaticInitializers()
    {
        for (auto &initFunc : postStaticInitializers) {
            initFunc();
        }
        postStaticInitializers.clear();
    }
};

// ============================================================================
// MARK: Register(Class)
// ============================================================================
template <typename T>
struct Register
{
    Class *classInfo;


    Register(const std::string &className)
    {
        // 自动注册到全局注册表
        classInfo = new Class(className);
        classInfo = ClassRegistry::instance().registerClass<T>(className, classInfo).get();
    }

    template <typename ParentType>
    constexpr Register &parentClass()
    {
        // 如果 ParentType 是 void，则跳过（用于无父类的情况）
        if constexpr (!std::is_same_v<ParentType, void>) {
            static_assert(std::is_base_of_v<ParentType, T>, "ParentType must be base of T");

            refl::type_index_t parentTypeId = refl::type_index_v<ParentType>;
            refl::type_index_t childTypeId  = refl::type_index_v<T>;

            // 注册继承关系到全局注册表
            ClassRegistry::instance().registerInheritance(childTypeId, parentTypeId);
            // 注册到 Class 信息中
            classInfo->registerParent<T, ParentType>();
        }

        return *this;
    }

    // virtual ~Register() {}

    template <typename FuncType>
    Register &function(const std::string &name, FuncType func)
    {
        classInfo->function(name, func);
        return *this;
    }

    // 注册静态函数
    template <typename Ret, typename... Args>
    Register &staticFunction(const std::string &name, Ret (*func)(Args...))
    {
        classInfo->staticFunction(name, func);
        return *this;
    }

    // 统一的property函数 - 通过静态反射自动判断成员类型
    template <typename PropertyType>
    Register &property(const std::string &name, PropertyType prop, Metadata meta = {})
    {
        using DecayedType = std::decay_t<PropertyType>;
        Property *p       = nullptr;

        // 检查是否是成员指针
        if constexpr (std::is_member_object_pointer_v<DecayedType>) {
            // 提取成员指针指向的类型
            using MemberType = typename std::remove_reference_t<
                decltype(std::declval<T>().*prop)>;

            // 禁止引用类型的反射
            static_assert(!std::is_reference_v<MemberType>,
                          "Reference type members are not supported in reflection. "
                          "Use pointer types instead.");

            // 成员指针：可以是 T::* 或 const T::*
            p = &classInfo->property(name, prop);
        }
        // 检查是否是指针类型（静态变量）
        else if constexpr (std::is_pointer_v<DecayedType>) {
            // 提取指针指向的类型
            using PointerType = std::remove_pointer_t<DecayedType>;
            using BaseType    = std::remove_const_t<PointerType>;

            // 禁止引用类型的反射
            static_assert(!std::is_reference_v<BaseType>,
                          "Reference type static variables are not supported in reflection. "
                          "Use pointer types instead.");

            // 静态变量指针：可以是 T* 或 const T*
            p = &classInfo->staticProperty(name, prop);
        }
        else {
            static_assert(std::is_member_object_pointer_v<DecayedType> || std::is_pointer_v<DecayedType>,
                          "Property must be either a member pointer (T::*) or a static variable pointer");
        }
        p->metadata = std::move(meta);

        return *this;
    }

    // 注册构造函数（根据参数类型自动推导）
    template <typename... Args>
    Register &constructor()
    {
        classInfo->registerConstructor<T, Args...>();
        return *this;
    }
};


// ============================================================================
// MARK: EnumRegistry - 全局枚举注册表
// ============================================================================
struct EnumRegistry
{
    std::unordered_map<std::string, Enum> enums;
    std::unordered_map<uint32_t, Enum *>  typeIdMap; // typeIndex -> Enum*

    static EnumRegistry &instance();

    void registerEnum(const std::string &enumName, const Enum &enumInfo, uint32_t typeIndex = 0)
    {
        enums[enumName] = enumInfo;
        if (typeIndex != 0) {
            typeIdMap[typeIndex] = &enums[enumName];
        }
    }

    Enum *getEnum(const std::string &enumName)
    {
        auto it = enums.find(enumName);
        return it != enums.end() ? &it->second : nullptr;
    }

    Enum *getEnum(uint32_t typeIndex)
    {
        auto it = typeIdMap.find(typeIndex);
        return it != typeIdMap.end() ? it->second : nullptr;
    }

    bool hasEnum(const std::string &enumName) const
    {
        return enums.find(enumName) != enums.end();
    }

    bool hasEnum(uint32_t typeIndex) const
    {
        return typeIdMap.find(typeIndex) != typeIdMap.end();
    }
};

// ============================================================================
// MARK: RegisterEnum
// ============================================================================
template <typename EnumType>
struct RegisterEnum
{
    Enum     enumInfo;
    uint32_t typeIndex = 0;

    RegisterEnum(const std::string &enumName, uint32_t inTypeIndex = 0)
    {
        static_assert(std::is_enum_v<EnumType>, "T must be an enum type");
        enumInfo.name           = enumName;
        enumInfo.underlyingSize = sizeof(std::underlying_type_t<EnumType>);
        typeIndex               = inTypeIndex;
    }

    RegisterEnum &value(const std::string &valueName, EnumType val)
    {
        enumInfo.addValue(valueName, static_cast<int64_t>(val));
        return *this;
    }

    ~RegisterEnum()
    {
        EnumRegistry::instance().registerEnum(enumInfo.name, enumInfo, typeIndex);
    }
};