#include "Core/Scripting/ScriptValue.h"

#include <reflects-core/lib.h>

#include <cmath>
#include <format>

namespace ya::script
{

const char* scriptValueTypeName(const ScriptValue& value)
{
    static constexpr const char* names[] = {"nil", "boolean", "integer", "number", "string", "vec2", "vec3", "vec4", "object"};
    return names[value.index()];
}

bool scriptToBool(const ScriptValue& value)
{
    if (const bool* v = std::get_if<bool>(&value)) {
        return *v;
    }
    throw ScriptError(std::string("expected a boolean, got ") + scriptValueTypeName(value));
}

int64_t scriptToInteger(const ScriptValue& value)
{
    if (const int64_t* v = std::get_if<int64_t>(&value)) {
        return *v;
    }
    if (const double* v = std::get_if<double>(&value)) {
        if (std::trunc(*v) == *v) {
            return static_cast<int64_t>(*v);
        }
        throw ScriptError(std::format("expected an integer, got {}", *v));
    }
    throw ScriptError(std::string("expected an integer, got ") + scriptValueTypeName(value));
}

double scriptToNumber(const ScriptValue& value)
{
    if (const double* v = std::get_if<double>(&value)) {
        return *v;
    }
    if (const int64_t* v = std::get_if<int64_t>(&value)) {
        return static_cast<double>(*v);
    }
    throw ScriptError(std::string("expected a number, got ") + scriptValueTypeName(value));
}

std::string scriptToString(const ScriptValue& value)
{
    if (const std::string* v = std::get_if<std::string>(&value)) {
        return *v;
    }
    throw ScriptError(std::string("expected a string, got ") + scriptValueTypeName(value));
}

int64_t scriptToEnum(type_index_t enumType, const ScriptValue& value)
{
    const Enum* reflected = EnumRegistry::instance().getEnum(enumType);
    if (const std::string* name = std::get_if<std::string>(&value)) {
        if (!reflected || !reflected->hasName(*name)) {
            throw ScriptError(std::format("unknown enum value '{}'", *name));
        }
        return reflected->getValue(*name);
    }
    const int64_t number = scriptToInteger(value);
    if (reflected && !reflected->hasValue(number)) {
        throw ScriptError(std::format("{} is not a value of enum {}", number, reflected->name));
    }
    return number;
}

} // namespace ya::script
