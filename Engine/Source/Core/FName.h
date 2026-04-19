#pragma once

#include <format>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <stdint.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ya
{


inline constexpr std::string_view INVALID_FNAME_TEXT = "None";

using index_t = uint32_t;

class NameRegistry
{
  public:
    struct StringViewHash
    {
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }

        std::size_t operator()(const std::string& value) const noexcept
        {
            return (*this)(std::string_view(value));
        }

        std::size_t operator()(const char* value) const noexcept
        {
            return (*this)(std::string_view(value));
        }
    };

    struct StringViewEqual
    {
        using is_transparent = void;

        bool operator()(std::string_view lhs, std::string_view rhs) const noexcept
        {
            return lhs == rhs;
        }
    };

  private:
    std::unordered_map<std::string, index_t, StringViewHash, StringViewEqual> _str2Index;
    std::deque<std::string>                                                   _indexToString;
    mutable std::shared_mutex                                                 _mutex;

  public:
    static NameRegistry& get();
    static NameRegistry* getP() { return &get(); }

    index_t           indexing(std::string_view name);
    std::string_view  view(index_t index) const;
        const char*       c_str(index_t index) const { return view(index).data(); }
        std::string       toString(index_t index) const { return std::string(view(index)); }
};

struct FName
{
    index_t _index = 0;

    FName() = default;
    FName(const std::string& name)
        : FName(std::string_view(name))
    {
    }
    FName(const char* name)
        : FName(name ? std::string_view(name) : std::string_view())
    {
    }
    FName(std::string_view name)
    {
        if (name.empty()) {
            return;
        }

        _index = NameRegistry::get().indexing(name);
    }
    ~FName() {}

    std::string      toString() const { return NameRegistry::get().toString(_index); }
    std::string_view view() const { return NameRegistry::get().view(_index); }
    const char*      c_str() const { return NameRegistry::get().c_str(_index); }

    bool isValid() const noexcept { return _index != 0; }
    bool isEmpty() const noexcept { return _index == 0; }

    operator index_t() const noexcept { return _index; }
    operator std::string_view() const noexcept { return view(); }
    operator const char*() const noexcept { return c_str(); }
    operator std::string() const noexcept { return toString(); }

    bool operator==(const FName& other) const noexcept { return _index == other._index; }
    bool operator!=(const FName& other) const noexcept { return _index != other._index; }
    bool operator<(const FName& other) const noexcept { return _index < other._index; }

    index_t identity() const { return _index; }
};

namespace literals
{

inline FName operator""_name(const char* str, size_t len)
{
    return FName(std::string_view(str, len));
}
}; // namespace literals


} // namespace ya

namespace std
{
template <>
struct formatter<ya::FName> : formatter<std::string_view>
{
    auto format(const ya::FName& name, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "{}", name.view());
    }
};

template <>
struct hash<ya::FName>
{
    std::size_t operator()(const ya::FName& name) const
    {
        return name.identity();
    }
};

} // namespace std
