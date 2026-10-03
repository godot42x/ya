#include "Core/Common/JsonFormat.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ya
{
namespace
{

bool isScalar(const nlohmann::json& value)
{
    return value.is_null() || value.is_boolean() || value.is_number() || value.is_string();
}

int clampedCount(int value)
{
    return value < 0 ? 0 : value;
}

std::string dumpValue(const nlohmann::json& value, int depth, int column, int indent, int wrapColumn);

std::string dumpScalarArray(const nlohmann::json& array, int depth, int column, int indent, int wrapColumn)
{
    std::vector<std::string> elements;
    elements.reserve(array.size());
    for (const auto& element : array) {
        elements.push_back(element.dump());
    }

    std::string oneLine = "[";
    for (size_t index = 0; index < elements.size(); ++index) {
        if (index != 0) {
            oneLine += ", ";
        }
        oneLine += elements[index];
    }
    oneLine += "]";

    const size_t limit = static_cast<size_t>(wrapColumn);
    if (static_cast<size_t>(std::max(column, 0)) + oneLine.size() <= limit) {
        return oneLine;
    }

    const std::string continuation(static_cast<size_t>((depth + 1) * indent), ' ');
    std::string       result;
    std::string       line     = "[";
    int               lineHome = column;
    bool              bLineHasElement = false;

    for (size_t index = 0; index < elements.size();) {
        const bool        bLast  = index + 1 == elements.size();
        const std::string token  = (bLineHasElement ? ", " : std::string{}) + elements[index];
        const size_t      closing = bLast ? 1u : 0u;
        const size_t      width =
            static_cast<size_t>(std::max(lineHome, 0)) + line.size() + token.size() + closing;
        if (bLineHasElement && width > limit) {
            // The comma stays with the elements already on this line.
            line.push_back(',');
            result += line;
            result.push_back('\n');
            line            = continuation;
            lineHome        = 0;
            bLineHasElement = false;
            continue;
        }

        line += token;
        bLineHasElement = true;
        if (bLast) {
            line.push_back(']');
        }
        ++index;
    }

    result += line;
    return result;
}

std::string dumpArray(const nlohmann::json& array, int depth, int column, int indent, int wrapColumn)
{
    if (array.empty()) {
        return "[]";
    }

    bool bAllScalar = true;
    for (const auto& element : array) {
        if (!isScalar(element)) {
            bAllScalar = false;
            break;
        }
    }
    if (bAllScalar) {
        return dumpScalarArray(array, depth, column, indent, wrapColumn);
    }

    const std::string pad(static_cast<size_t>(depth * indent), ' ');
    const std::string inner(static_cast<size_t>((depth + 1) * indent), ' ');
    std::string       out = "[\n";
    bool              bFirst = true;
    for (const auto& element : array) {
        if (!bFirst) {
            out += ",\n";
        }
        bFirst = false;
        out += inner;
        out += dumpValue(element, depth + 1, static_cast<int>(inner.size()), indent, wrapColumn);
    }
    out += '\n';
    out += pad;
    out += ']';
    return out;
}

std::string dumpObject(const nlohmann::json& object, int depth, int indent, int wrapColumn)
{
    if (object.empty()) {
        return "{}";
    }

    const std::string pad(static_cast<size_t>(depth * indent), ' ');
    const std::string inner(static_cast<size_t>((depth + 1) * indent), ' ');
    std::string       out = "{\n";
    bool              bFirst = true;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!bFirst) {
            out += ",\n";
        }
        bFirst = false;
        const std::string prefix = inner + nlohmann::json(it.key()).dump() + ": ";
        out += prefix;
        out += dumpValue(it.value(), depth + 1, static_cast<int>(prefix.size()), indent, wrapColumn);
    }
    out += '\n';
    out += pad;
    out += '}';
    return out;
}

std::string dumpValue(const nlohmann::json& value, int depth, int column, int indent, int wrapColumn)
{
    if (value.is_object()) {
        return dumpObject(value, depth, indent, wrapColumn);
    }
    if (value.is_array()) {
        return dumpArray(value, depth, column, indent, wrapColumn);
    }
    return value.dump();
}

} // namespace

std::string dumpJsonCompactLeaves(const nlohmann::json& value, int indent, int wrapColumn)
{
    return dumpValue(value, 0, 0, clampedCount(indent), clampedCount(wrapColumn));
}

} // namespace ya
