#include "Core/Reflection/CoreValueTypes.h"

#include "Core/Math/GLM.h"
#include "Core/Reflection/ValueFacets.h"
#include "Core/Scripting/ScriptValue.h"

#include <format>
#include <string>

namespace ya::reflection
{
namespace
{

template <class Vec>
std::string printComponents(const Vec& value, int count)
{
    std::string text = "(";
    for (int index = 0; index < count; ++index) {
        if (index != 0) {
            text += ", ";
        }
        text += std::format("{}", value[index]);
    }
    text += ")";
    return text;
}

template <class Mat>
std::string printMatrix(const Mat& value)
{
    std::string text = "(";
    for (int column = 0; column < value.length(); ++column) {
        if (column != 0) {
            text += ", ";
        }
        text += printComponents(value[column], value[column].length());
    }
    text += ")";
    return text;
}

std::string printVec2(const glm::vec2& value)
{
    return printComponents(value, 2);
}

std::string printVec3(const glm::vec3& value)
{
    return printComponents(value, 3);
}

std::string printVec4(const glm::vec4& value)
{
    return printComponents(value, 4);
}

std::string printQuat(const glm::quat& value)
{
    return printComponents(value, 4);
}

std::string printMat3(const glm::mat3& value)
{
    return printMatrix(value);
}

std::string printMat4(const glm::mat4& value)
{
    return printMatrix(value);
}

std::string printRect(const script::ScriptRect& value)
{
    return std::format("({}, {}, {}, {})", value.x, value.y, value.w, value.h);
}

std::string printRef(const script::ScriptRef& value)
{
    return std::format("ref({}, {}, {})", value.kind, value.a, value.b);
}

std::string printFn(const script::ScriptFnHandle& value)
{
    return std::format("fn({})", value.id);
}

std::string printSelf(const script::ScriptSelfHandle& value)
{
    return std::format("self({})", value.runtimeId);
}

std::string printTypeName(const refl::TypeOps& ops, const void*)
{
    return std::format("<{}>", ops.name);
}

template <class T>
void registerValueType(const char* name, std::string (*print)(const T&))
{
    refl::TypeRegistry::instance().registerType<T>(name);
    registerEqualFacet<T>();
    registerPrintFacet<T>(print);
}

} // namespace

const FPrintFacet& FPrintFacet::fallback()
{
    static const FPrintFacet facet{.print = &printTypeName};
    return facet;
}

void registerCoreValueTypes()
{
    static bool bRegistered = false;
    if (bRegistered) {
        return;
    }

    registerValueType<glm::vec2>("glm::vec2", &printVec2);
    registerValueType<glm::vec3>("glm::vec3", &printVec3);
    registerValueType<glm::vec4>("glm::vec4", &printVec4);
    registerValueType<glm::quat>("glm::quat", &printQuat);
    registerValueType<glm::mat3>("glm::mat3", &printMat3);
    registerValueType<glm::mat4>("glm::mat4", &printMat4);
    registerValueType<script::ScriptRect>("script::ScriptRect", &printRect);
    registerValueType<script::ScriptRef>("script::ScriptRef", &printRef);
    registerValueType<script::ScriptFnHandle>("script::ScriptFnHandle", &printFn);
    registerValueType<script::ScriptSelfHandle>("script::ScriptSelfHandle", &printSelf);

    bRegistered = true;
}

} // namespace ya::reflection
