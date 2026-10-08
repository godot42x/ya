// The global allocation functions, routed through the engine's allocator
// layer (`ya::memory::processAllocate` / `processDeallocate`).
//
// This file is compiled into EVERY module (see `ya_process_allocator()` in
// Engine/Source/Source.xmake.lua), not just into `ya-foundation-core`. Mach-O
// binds an image's undefined `_Znwm` to that same image, and MSVC gives every
// DLL its own CRT, so a single copy anywhere else would only ever route one
// module's allocations. Each copy forwards to the same Core entry points,
// which is also what keeps a pointer allocated in one module and freed in
// another going through the same allocator.
//
// Every build routes through the layer, not just the measuring ones: one
// allocator that only exists in some builds is not one layer, and having the
// debug gates and the whole test suite walk this path is worth more than the
// nanoseconds it costs. Which backend they route to is a C++ choice
// (`ProcessAllocator`), and only the counting one is profile-specific.

#include "Core/Memory/Allocator.h"

#include <cstddef>
#include <new>

namespace
{

/// `operator new` failing has to throw, and the exception's own allocation
/// may fail too -- in which case the runtime terminates, which is the
/// standard's behaviour for an allocation that cannot be satisfied.
[[noreturn]] void throwBadAlloc()
{
    throw std::bad_alloc();
}

void* allocateOrThrow(std::size_t size, std::size_t alignment)
{
    void* pointer = ya::memory::processAllocate(size, alignment);
    if (pointer == nullptr) {
        throwBadAlloc();
    }
    return pointer;
}

} // namespace

// --- new ---------------------------------------------------------------------

void* operator new(std::size_t size)
{
    return allocateOrThrow(size, 0);
}

void* operator new[](std::size_t size)
{
    return allocateOrThrow(size, 0);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return ya::memory::processAllocate(size, 0);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return ya::memory::processAllocate(size, 0);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocateOrThrow(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocateOrThrow(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return ya::memory::processAllocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return ya::memory::processAllocate(size, static_cast<std::size_t>(alignment));
}

// --- delete ------------------------------------------------------------------

void operator delete(void* pointer) noexcept
{
    ya::memory::processDeallocate(pointer, 0);
}

void operator delete[](void* pointer) noexcept
{
    ya::memory::processDeallocate(pointer, 0);
}

void operator delete(void* pointer, std::size_t size) noexcept
{
    ya::memory::processDeallocate(pointer, size);
}

void operator delete[](void* pointer, std::size_t size) noexcept
{
    ya::memory::processDeallocate(pointer, size);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
    ya::memory::processDeallocate(pointer, 0);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
    ya::memory::processDeallocate(pointer, 0);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
    ya::memory::processDeallocate(pointer, 0);
}

void operator delete[](void* pointer, std::align_val_t) noexcept
{
    ya::memory::processDeallocate(pointer, 0);
}

void operator delete(void* pointer, std::size_t size, std::align_val_t) noexcept
{
    ya::memory::processDeallocate(pointer, size);
}

void operator delete[](void* pointer, std::size_t size, std::align_val_t) noexcept
{
    ya::memory::processDeallocate(pointer, size);
}

void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    ya::memory::processDeallocate(pointer, 0);
}

void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    ya::memory::processDeallocate(pointer, 0);
}