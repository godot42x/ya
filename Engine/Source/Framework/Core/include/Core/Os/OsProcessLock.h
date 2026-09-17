#pragma once

#include "Core/Api.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace ya::Os
{

/// Exclusive lock on a name, held for as long as the owning process lives.
///
/// Motivation: an unattended instance (agent-launched editor, smoke run) must
/// not be able to pile up next to another one. Two instances of the same thing
/// contend for the GPU, the automation port and the build outputs, and the
/// second one is usually invisible: it starts, fails to bind its port, and then
/// sits there looking healthy.
///
/// The lock is an OS-level file lock, so it is released by the kernel when the
/// process exits, including on a hard kill. There is no stale-lock case to
/// clean up and no pid file to reason about.
class YA_CORE_API ProcessLock
{
  public:
    ProcessLock() = default;
    ~ProcessLock();

    ProcessLock(const ProcessLock&)            = delete;
    ProcessLock& operator=(const ProcessLock&) = delete;

    ProcessLock(ProcessLock&& other) noexcept;
    ProcessLock& operator=(ProcessLock&& other) noexcept;

    /// Try to take p name for this process without waiting.
    ///
    /// Returns false when a live process already holds it; p ownerPid then
    /// names the holder when the platform could record one (0 when unknown).
    [[nodiscard]] bool tryAcquire(std::string_view name, uint32_t& ownerPid);

    void release();

    [[nodiscard]] bool isHeld() const { return _handle != kInvalidHandle; }

    /// Where the lock for p name lives. Exposed so callers can tell the user
    /// which file to look at when an instance refuses to start.
    [[nodiscard]] static std::filesystem::path resolveLockPath(std::string_view name);

  private:
#if defined(_WIN32)
    using Handle = void*;
    static constexpr Handle kInvalidHandle = nullptr;
#else
    using Handle = int;
    static constexpr Handle kInvalidHandle = -1;
#endif

    Handle                _handle = kInvalidHandle;
    std::filesystem::path _path;
};

} // namespace ya::Os
