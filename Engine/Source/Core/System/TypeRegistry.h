#pragma once

#include "Core/Base.h"
#include <any>
#include <functional>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>

namespace ya
{

/**
 * @brief 统一类型注册表 - 管理类型转换、反射、序列化
 * 
 * 职责：
 * 1. 类型转换：C++ std::any ↔ Lua ↔ JSON ↔ 字符串
 * 2. 反射初始化：执行组件的 registerReflection()
 * 3. 类型推断：从 Lua 对象推断类型名
 * 
 * 替代：
 * - ReflectionSystem（已合并到此类）
 * - 原有的硬编码类型转换逻辑
 * 
 * @example 基础类型注册
 * TypeRegistry::get()->registerType<float>("float")
 *     .anyToLua([](const std::any& v, sol::state_view lua) { return sol::make_object(lua, std::any_cast<float>(v)); })
 *     .luaToAny([](const sol::object& obj, std::any& out) { out = obj.as<float>(); return true; })
 *     .anyToJson([](const std::any& v) { return std::any_cast<float>(v); })
 *     .jsonToAny([](const nlohmann::json& j) { return std::any(j.get<float>()); })
 *     .stringToAny([](const std::string& s) { return std::any(std::stof(s)); });
 * 
 * @example 组件反射注册（使用宏）
 * struct TransformComponent {
 *     REFLECT_AUTO_REGISTER(TransformComponent)
 *     static void registerReflection() {
 *         Register<TransformComponent>("TransformComponent")
 *             .property("position", &TransformComponent::_position);
 *     }
 * };
 */
class TypeRegistry
{
  public:
    // ========================================================================
    // 类型转换函数签名
    // ========================================================================
    
    using AnyToLuaFunc    = std::function<sol::object(const std::any&, sol::state_view)>;
    using LuaToAnyFunc    = std::function<bool(const sol::object&, std::any&)>;
    using AnyToJsonFunc   = std::function<nlohmann::json(const std::any&)>;
    using JsonToAnyFunc   = std::function<std::any(const nlohmann::json&)>;
    using StringToAnyFunc = std::function<std::any(const std::string&)>;
    using LuaTypeChecker  = std::function<bool(const sol::object&)>;

    struct TypeInfo
    {
        std::string      typeName;       // "float", "int", "Vec3" 等
        size_t           typeHash;       // typeid(T).hash_code()
        uint32_t         typeIndex;      // refl::type_index_v<T>
        AnyToLuaFunc     anyToLua;       // std::any → sol::object
        LuaToAnyFunc     luaToAny;       // sol::object → std::any
        AnyToJsonFunc    anyToJson;      // std::any → JSON
        JsonToAnyFunc    jsonToAny;      // JSON → std::any
        StringToAnyFunc  stringToAny;    // 字符串 → std::any (用于序列化解析)
        LuaTypeChecker   luaTypeChecker; // 判断 sol::object 是否为该类型
    };

    // ========================================================================
    // 单例访问
    // ========================================================================
    
    static TypeRegistry* get()
    {
        static TypeRegistry instance;
        return &instance;
    }

    // ========================================================================
    // 反射初始化（原 ReflectionSystem 功能）
    // ========================================================================
    
    /**
     * @brief 初始化反射系统
     * - 执行所有组件的 registerReflection()
     * - 必须在使用反射序列化前调用
     */
    void initReflection();

    /**
     * @brief 注册单个反射函数（内部使用，由 REFLECT_AUTO_REGISTER 宏调用）
     */
    void addReflectionRegistrar(std::function<void()> registrar)
    {
        _reflectionRegistrars.push_back(std::move(registrar));
    }

    // ========================================================================
    // 类型注册构建器
    // ========================================================================
    
    class TypeBuilder
    {
      public:
        TypeBuilder(TypeRegistry* registry, TypeInfo info)
            : _registry(registry), _info(std::move(info))
        {
        }

        TypeBuilder& anyToLua(AnyToLuaFunc func)
        {
            _info.anyToLua = std::move(func);
            return *this;
        }

        TypeBuilder& luaToAny(LuaToAnyFunc func)
        {
            _info.luaToAny = std::move(func);
            return *this;
        }

        TypeBuilder& anyToJson(AnyToJsonFunc func)
        {
            _info.anyToJson = std::move(func);
            return *this;
        }

        TypeBuilder& jsonToAny(JsonToAnyFunc func)
        {
            _info.jsonToAny = std::move(func);
            return *this;
        }

        TypeBuilder& stringToAny(StringToAnyFunc func)
        {
            _info.stringToAny = std::move(func);
            return *this;
        }

        TypeBuilder& luaTypeChecker(LuaTypeChecker func)
        {
            _info.luaTypeChecker = std::move(func);
            return *this;
        }

        ~TypeBuilder()
        {
            // 保存类型名（string 复制轻量，避免保存 _info 引用）
            std::string typeName = _info.typeName;
            size_t typeHash = _info.typeHash;
            uint32_t typeIndex = _info.typeIndex;
            
            // 主存储：插入完整 TypeInfo
            _registry->_typesByName.emplace(typeName, std::move(_info));
            
            // 索引存储：存储类型名键（避免指针失效）
            _registry->_typesByHash[typeHash] = typeName;
            _registry->_typesByIndex[typeIndex] = typeName;
        }

      private:
        TypeRegistry* _registry;
        TypeInfo      _info;
    };

    friend struct TypeRegistryInitializer; // 允许初始化器访问私有成员

    // ========================================================================
    // 类型注册接口
    // ========================================================================
    
    template <typename T>
    TypeBuilder registerType(const std::string& typeName)
    {
        TypeInfo info;
        info.typeName  = typeName;
        info.typeHash  = typeid(T).hash_code();
        info.typeIndex = refl::type_index_v<T>;
        return TypeBuilder(this, std::move(info));
    }

    // ========================================================================
    // 类型查询
    // ========================================================================
    
    const TypeInfo* getTypeByName(const std::string& name) const
    {
        auto it = _typesByName.find(name);
        return it != _typesByName.end() ? &it->second : nullptr;
    }

    const TypeInfo* getTypeByHash(size_t hash) const
    {
        auto it = _typesByHash.find(hash);
        if (it != _typesByHash.end()) {
            return getTypeByName(it->second);
        }
        return nullptr;
    }

    const TypeInfo* getTypeByIndex(uint32_t index) const
    {
        auto it = _typesByIndex.find(index);
        if (it != _typesByIndex.end()) {
            return getTypeByName(it->second);
        }
        return nullptr;
    }

    // ========================================================================
    // 类型推断：从 sol::object 推断类型名
    // ========================================================================
    
    std::string inferTypeFromLua(const sol::object& obj) const
    {
        // 遍历所有注册的类型，使用 checker 匹配
        for (const auto& [name, info] : _typesByName)
        {
            if (info.luaTypeChecker && info.luaTypeChecker(obj))
            {
                return name;
            }
        }
        return "unknown";
    }

    // ========================================================================
    // 统一转换接口
    // ========================================================================
    
    // std::any → sol::object
    sol::object anyToLuaObject(const std::any& value, uint32_t typeIndex, sol::state_view lua) const
    {
        const TypeInfo* info = getTypeByIndex(typeIndex);
        if (info && info->anyToLua)
        {
            return info->anyToLua(value, lua);
        }
        YA_CORE_WARN("[TypeRegistry] No anyToLua converter for typeIndex: {}", typeIndex);
        return sol::nil;
    }

    // sol::object → std::any
    bool luaObjectToAny(const sol::object& luaValue, uint32_t typeIndex, std::any& outValue) const
    {
        const TypeInfo* info = getTypeByIndex(typeIndex);
        if (info && info->luaToAny)
        {
            return info->luaToAny(luaValue, outValue);
        }
        YA_CORE_WARN("[TypeRegistry] No luaToAny converter for typeIndex: {}", typeIndex);
        return false;
    }

    // std::any → JSON
    nlohmann::json anyToJson(const std::any& value, uint32_t typeIndex) const
    {
        const TypeInfo* info = getTypeByIndex(typeIndex);
        if (info && info->anyToJson)
        {
            return info->anyToJson(value);
        }
        YA_CORE_WARN("[TypeRegistry] No anyToJson converter for typeIndex: {}", typeIndex);
        return nullptr;
    }

    // JSON → std::any
    std::any jsonToAny(const nlohmann::json& j, size_t typeHash) const
    {
        const TypeInfo* info = getTypeByHash(typeHash);
        if (info && info->jsonToAny)
        {
            return info->jsonToAny(j);
        }
        YA_CORE_WARN("[TypeRegistry] No jsonToAny converter for typeHash: {}", typeHash);
        return std::any();
    }

    // 字符串 → std::any (用于属性反序列化)
    std::any stringToAny(const std::string& str, const std::string& typeName) const
    {
        const TypeInfo* info = getTypeByName(typeName);
        if (info && info->stringToAny)
        {
            return info->stringToAny(str);
        }
        YA_CORE_WARN("[TypeRegistry] No stringToAny converter for type: {}", typeName);
        return std::any();
    }

  private:
    // 主存储：完整的 TypeInfo 对象
    std::unordered_map<std::string, TypeInfo> _typesByName;  // "float" → TypeInfo
    
    // 索引存储：存储类型名键（string 复制开销小，但避免了指针失效）
    std::unordered_map<size_t, std::string>     _typesByHash;  // typeid(T).hash_code() → typeName
    std::unordered_map<uint32_t, std::string>   _typesByIndex; // refl::type_index_v<T> → typeName
    
    // 反射注册函数列表（原 AutoRegisterRegistry 功能）
    std::vector<std::function<void()>> _reflectionRegistrars;
};

} // namespace ya
