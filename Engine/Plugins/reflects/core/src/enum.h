#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

// ============================================================================
// MARK: Enum
// ============================================================================
struct EnumValue
{
    std::string name;
    int64_t     value;
};

struct Enum
{
    std::string                              name;
    std::vector<EnumValue>                   values;
    std::unordered_map<std::string, int64_t> nameToValue;
    std::unordered_map<int64_t, std::string> valueToName;
    size_t                                   underlyingSize = sizeof(int); // Size of underlying type in bytes

    Enum() = default;
    explicit Enum(const std::string &inName) : name(inName) {}

    // 添加枚举值
    void addValue(const std::string &valueName, int64_t val)
    {
        values.push_back({valueName, val});
        nameToValue[valueName] = val;
        valueToName[val]       = valueName;
    }

    // 通过名称获取值
    int64_t getValue(const std::string &valueName) const
    {
        auto it = nameToValue.find(valueName);
        if (it == nameToValue.end()) {
            throw std::runtime_error("Enum value not found: " + valueName);
        }
        return it->second;
    }

    // 通过值获取名称
    std::string getName(int64_t val) const
    {
        auto it = valueToName.find(val);
        if (it == valueToName.end()) {
            throw std::runtime_error("Enum name not found for value: " + std::to_string(val));
        }
        return it->second;
    }

    // 检查是否有某个名称
    bool hasName(const std::string &valueName) const
    {
        return nameToValue.find(valueName) != nameToValue.end();
    }

    // 检查是否有某个值
    bool hasValue(int64_t val) const
    {
        return valueToName.find(val) != valueToName.end();
    }

    // 获取所有枚举值
    const std::vector<EnumValue> &getValues() const
    {
        return values;
    }
};
