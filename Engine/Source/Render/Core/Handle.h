#pragma once

// Type-safe handle wrapper for backend-specific handles
#include <cstdint>
template <typename Tag>
struct Handle
{
    void *ptr = nullptr;

    Handle() = default;
    Handle(void *p) : ptr(p) {}

    // Allow assignment from void*
    Handle &operator=(void *p)
    {
        ptr = p;
        return *this;
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