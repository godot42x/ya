#include "Core/Scripting/ScriptValue.h"

#include <reflects-core/lib.h>

#include <cmath>
#include <format>

namespace ya::script
{
namespace
{

const refl::TypeOps* opsOf(const ScriptValue& value)
{
    if (value.isNull()) {
        return nullptr;
    }
    return refl::TypeRegistry::instance().find(value.type());
}

} // namespace

const char* scriptValueTypeName(const ScriptValue& value)
{
    if (value.isNull()) {
        return "nil";
    }
    if (scriptIf<bool>(value)) {
        return "boolean";
    }
    if (scriptIf<std::string>(value)) {
        return "string";
    }
    if (scriptIf<ScriptRef>(value)) {
        return "object";
    }
    if (scriptIf<ScriptFnHandle>(value)) {
        return "function";
    }
    if (scriptIf<ScriptSelfHandle>(value)) {
        return "self";
    }
    if (scriptIf<ScriptRect>(value)) {
        return "rect";
    }
    if (scriptIf<glm::vec2>(value)) {
        return "vec2";
    }
    if (scriptIf<glm::vec3>(value)) {
        return "vec3";
    }
    if (scriptIf<glm::vec4>(value)) {
        return "vec4";
    }
    if (const refl::TypeOps* ops = opsOf(value)) {
        if (ops->kind == refl::EValueKind::Int) {
            return "integer";
        }
        if (ops->kind == refl::EValueKind::Float) {
            return "number";
        }
    }
    return "value";
}

bool scriptToBool(const ScriptValue& value)
{
    if (const auto flag = value.toBool()) {
        return *flag;
    }
    throw ScriptError(std::string("expected a boolean, got ") + scriptValueTypeName(value));
}

int64_t scriptToInteger(const ScriptValue& value)
{
    if (const auto integer = value.toInt64()) {
        return *integer;
    }
    if (const auto number = value.toDouble()) {
        throw ScriptError(std::format("expected an integer, got {}", *number));
    }
    throw ScriptError(std::string("expected an integer, got ") + scriptValueTypeName(value));
}

double scriptToNumber(const ScriptValue& value)
{
    if (const auto number = value.toDouble()) {
        return *number;
    }
    throw ScriptError(std::string("expected a number, got ") + scriptValueTypeName(value));
}

std::string scriptToString(const ScriptValue& value)
{
    if (const std::string* text = value.asString()) {
        return *text;
    }
    throw ScriptError(std::string("expected a string, got ") + scriptValueTypeName(value));
}

int64_t scriptToEnum(type_index_t enumType, const ScriptValue& value)
{
    const Enum* reflected = EnumRegistry::instance().getEnum(enumType);
    if (const std::string* name = value.asString()) {
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
