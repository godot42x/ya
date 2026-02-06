#pragma once

// Type-safe handle wrapper for backend-specific handles
#include <cstdint>
#include <type_traits>

#include "Core/Common/Types.h"

// C++20 Concepts for type checking
template <typename T>
concept PtrHasGetHandle = requires(const T &obj) {
    {
        obj->getHandle()
    };
};

template <typename Tag>
struct Handle
{
    void *ptr = nullptr;

    Handle() = default;
    Handle(std::nullptr_t) {}

    // Constructor 2: Accept objects with getHandle() method - 自动调用 getHandle()
    template <typename T>
        requires(!PtrHasGetHandle<T>)
    Handle(T p) : ptr(p)
    {
    }

    // Constructor 3: Accept other Handle instances
    template <typename OtherTag>
    Handle(const Handle<OtherTag> &other) : ptr(other.ptr)
    {
    }

    template <typename T>
    T as() const { return static_cast<T>(ptr); }

    // Conversion to void* and uintptr_t
    operator void *() const { return ptr; }
    operator uintptr_t() const { return reinterpret_cast<uintptr_t>(ptr); }

    explicit operator bool() const { return ptr != nullptr; }

    // Comparison operators
    bool operator==(const Handle &other) const { return ptr == other.ptr; }
    bool operator!=(const Handle &other) const { return ptr != other.ptr; }
    bool operator==(void *p) const { return ptr == p; }
    bool operator!=(void *p) const { return ptr != p; }
};