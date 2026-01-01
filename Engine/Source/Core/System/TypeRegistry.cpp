#include "TypeRegistry.h"
#include <any>
#include <cmath>
#include <limits>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

namespace ya
{

// ============================================================================
// 类型注册初始化 - 静态初始化器
// ============================================================================

struct TypeRegistryInitializer
{
    TypeRegistryInitializer()
    {
        auto* registry = TypeRegistry::get();

        // ====================================================================
        // 基础类型注册
        // ====================================================================

        // int 类型
        registry->registerType<int>("int")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<int>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<int>()) {
                    out = obj.as<int>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                return std::any_cast<int>(v);
            })
            .jsonToAny([](const nlohmann::json& j) {
                return std::any(j.get<int>());
            })
            .stringToAny([](const std::string& s) {
                return std::any(std::stoi(s));
            })
            .luaTypeChecker([](const sol::object& obj) {
                if (!obj.is<double>()) return false;
                double num = obj.as<double>();
                return num == std::floor(num) && num >= INT_MIN && num <= INT_MAX;
            });

        // float 类型
        registry->registerType<float>("float")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<float>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<double>() || obj.is<float>()) {
                    out = obj.as<float>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                return std::any_cast<float>(v);
            })
            .jsonToAny([](const nlohmann::json& j) {
                return std::any(j.get<float>());
            })
            .stringToAny([](const std::string& s) {
                return std::any(std::stof(s));
            })
            .luaTypeChecker([](const sol::object& obj) {
                return obj.is<double>(); // Lua number 默认是 float
            });

        // double 类型
        registry->registerType<double>("double")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<double>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<double>()) {
                    out = obj.as<double>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                return std::any_cast<double>(v);
            })
            .jsonToAny([](const nlohmann::json& j) {
                return std::any(j.get<double>());
            })
            .stringToAny([](const std::string& s) {
                return std::any(std::stod(s));
            })
            .luaTypeChecker([](const sol::object& obj) {
                return obj.is<double>();
            });

        // bool 类型
        registry->registerType<bool>("bool")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<bool>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<bool>()) {
                    out = obj.as<bool>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                return std::any_cast<bool>(v);
            })
            .jsonToAny([](const nlohmann::json& j) {
                return std::any(j.get<bool>());
            })
            .stringToAny([](const std::string& s) {
                return std::any(s == "true" || s == "1");
            })
            .luaTypeChecker([](const sol::object& obj) {
                return obj.is<bool>();
            });

        // string 类型
        registry->registerType<std::string>("string")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<std::string>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<std::string>()) {
                    out = obj.as<std::string>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                return std::any_cast<std::string>(v);
            })
            .jsonToAny([](const nlohmann::json& j) {
                return std::any(j.get<std::string>());
            })
            .stringToAny([](const std::string& s) {
                return std::any(s); // 字符串直接返回
            })
            .luaTypeChecker([](const sol::object& obj) {
                return obj.is<std::string>();
            });

        // ====================================================================
        // GLM 数学类型注册
        // ====================================================================

        // glm::vec2
        registry->registerType<glm::vec2>("Vec2")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<glm::vec2>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<glm::vec2>()) {
                    out = obj.as<glm::vec2>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                const auto& vec = std::any_cast<glm::vec2>(v);
                return nlohmann::json::array({vec.x, vec.y});
            })
            .jsonToAny([](const nlohmann::json& j) {
                return std::any(glm::vec2(j[0].get<float>(), j[1].get<float>()));
            })
            .stringToAny([](const std::string& s) {
                float x, y;
                if (sscanf(s.c_str(), "%f,%f", &x, &y) == 2) {
                    return std::any(glm::vec2(x, y));
                }
                return std::any();
            })
            .luaTypeChecker([](const sol::object& obj) {
                return obj.is<glm::vec2>();
            });

        // glm::vec3
        registry->registerType<glm::vec3>("Vec3")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<glm::vec3>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<glm::vec3>()) {
                    out = obj.as<glm::vec3>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                const auto& vec = std::any_cast<glm::vec3>(v);
                return nlohmann::json::array({vec.x, vec.y, vec.z});
            })
            .jsonToAny([](const nlohmann::json& j) {
                return std::any(glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()));
            })
            .stringToAny([](const std::string& s) {
                float x, y, z;
                if (sscanf(s.c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
                    return std::any(glm::vec3(x, y, z));
                }
                return std::any();
            })
            .luaTypeChecker([](const sol::object& obj) {
                return obj.is<glm::vec3>();
            });

        // glm::vec4
        registry->registerType<glm::vec4>("Vec4")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<glm::vec4>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<glm::vec4>()) {
                    out = obj.as<glm::vec4>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                const auto& vec = std::any_cast<glm::vec4>(v);
                return nlohmann::json::array({vec.x, vec.y, vec.z, vec.w});
            })
            .jsonToAny([](const nlohmann::json& j) {
                return std::any(glm::vec4(j[0].get<float>(), j[1].get<float>(), 
                                          j[2].get<float>(), j[3].get<float>()));
            })
            .stringToAny([](const std::string& s) {
                float x, y, z, w;
                if (sscanf(s.c_str(), "%f,%f,%f,%f", &x, &y, &z, &w) == 4) {
                    return std::any(glm::vec4(x, y, z, w));
                }
                return std::any();
            })
            .luaTypeChecker([](const sol::object& obj) {
                return obj.is<glm::vec4>();
            });

        // glm::mat4
        registry->registerType<glm::mat4>("Mat4")
            .anyToLua([](const std::any& v, sol::state_view lua) {
                return sol::make_object(lua, std::any_cast<glm::mat4>(v));
            })
            .luaToAny([](const sol::object& obj, std::any& out) {
                if (obj.is<glm::mat4>()) {
                    out = obj.as<glm::mat4>();
                    return true;
                }
                return false;
            })
            .anyToJson([](const std::any& v) {
                const auto& mat = std::any_cast<glm::mat4>(v);
                nlohmann::json j = nlohmann::json::array();
                for (int i = 0; i < 4; i++) {
                    for (int j_col = 0; j_col < 4; j_col++) {
                        j.push_back(mat[i][j_col]);
                    }
                }
                return j;
            })
            .jsonToAny([](const nlohmann::json& j) {
                glm::mat4 mat;
                int idx = 0;
                for (int i = 0; i < 4; i++) {
                    for (int j_col = 0; j_col < 4; j_col++) {
                        mat[i][j_col] = j[idx++].get<float>();
                    }
                }
                return std::any(mat);
            })
            .stringToAny([](const std::string& s) {
                // Mat4 的字符串解析较复杂，暂不实现
                YA_CORE_WARN("[TypeRegistry] Mat4 stringToAny not implemented");
                return std::any();
            })
            .luaTypeChecker([](const sol::object& obj) {
                return obj.is<glm::mat4>();
            });

        YA_CORE_INFO("[TypeRegistry] Initialized with {} types", 
                     registry->_typesByName.size());
    }
};

// 静态初始化 - 程序启动时自动执行
static TypeRegistryInitializer g_typeRegistryInitializer;

// ============================================================================
// 反射初始化实现
// ============================================================================

void TypeRegistry::initReflection()
{
    YA_CORE_INFO("[TypeRegistry] Executing {} reflection registrars", _reflectionRegistrars.size());
    
    for (auto& registrar : _reflectionRegistrars)
    {
        registrar();
    }
    
    YA_CORE_INFO("[TypeRegistry] Reflection initialization complete");
}

} // namespace ya
