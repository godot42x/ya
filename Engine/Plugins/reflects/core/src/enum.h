#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "reflects-core/api.h"

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
    bool                                     bUnderlyingSigned = true;    // Sign of the underlying type
    // A value as a std::any of the enum's own type, for Function invokers.
    std::function<std::any(int64_t)>         boxValue;
    std::function<int64_t(const std::any &)> unboxValue;

    Enum() = default;
    explicit Enum(const std::string &inName) : name(inName) {}

    // 添加枚举值
    void addValue(const std::string &valueName, int64_t val)
    {
        values.push_back({.name = valueName, .value = val});
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

    // Read/write honour the declared underlying size/sign: most reflected
    // enums are 1- or 2-byte scoped enums, and the historical 8-byte access
    // picked up neighbouring members' bytes as value noise.
    int64_t getValue(void *ptr) const
    {
        switch (underlyingSize) {
        case 1:
            return bUnderlyingSigned ? static_cast<int64_t>(*static_cast<const int8_t *>(ptr))
                                     : static_cast<int64_t>(*static_cast<const uint8_t *>(ptr));
        case 2:
            return bUnderlyingSigned ? static_cast<int64_t>(*static_cast<const int16_t *>(ptr))
                                     : static_cast<int64_t>(*static_cast<const uint16_t *>(ptr));
        case 4:
            return bUnderlyingSigned ? static_cast<int64_t>(*static_cast<const int32_t *>(ptr))
                                     : static_cast<int64_t>(*static_cast<const uint32_t *>(ptr));
        default:
            return *reinterpret_cast<const int64_t *>(ptr);
        }
    }
    void setValue(void *ptr, int64_t val) const
    {
        switch (underlyingSize) {
        case 1:
            if (bUnderlyingSigned) { *static_cast<int8_t *>(ptr) = static_cast<int8_t>(val); }
            else { *static_cast<uint8_t *>(ptr) = static_cast<uint8_t>(val); }
            break;
        case 2:
            if (bUnderlyingSigned) { *static_cast<int16_t *>(ptr) = static_cast<int16_t>(val); }
            else { *static_cast<uint16_t *>(ptr) = static_cast<uint16_t>(val); }
            break;
        case 4:
            if (bUnderlyingSigned) { *static_cast<int32_t *>(ptr) = static_cast<int32_t>(val); }
            else { *static_cast<uint32_t *>(ptr) = static_cast<uint32_t>(val); }
            break;
        default:
            *reinterpret_cast<int64_t *>(ptr) = val;
            break;
        }
    }
};
