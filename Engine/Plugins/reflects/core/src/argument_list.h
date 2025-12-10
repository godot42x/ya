#pragma once

#include <any>
#include <stdexcept>
#include <utility>
#include <vector>

// ============================================================================
// MARK: ArgumentList
// - Type-erased argument container
// ============================================================================
struct ArgumentList
{
    std::vector<std::any> args;

    ArgumentList() = default;

    template <typename... Args>
    static ArgumentList make(Args &&...args)
    {
        ArgumentList list;
        list.args.reserve(sizeof...(Args));
        (list.args.push_back(std::any(std::forward<Args>(args))), ...);
        return list;
    }

    template <typename T>
    T get(size_t index) const
    {
        if (index >= args.size()) {
            throw std::out_of_range("Argument index out of range");
        }
        return std::any_cast<T>(args[index]);
    }

    size_t size() const { return args.size(); }

    bool empty() const { return args.empty(); }

    void clear() { args.clear(); }
};
