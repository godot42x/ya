#include "Core/Os/OsProcessLock.h"

#include "Core/Log.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>

    #include <process.h>
#else
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/file.h>
    #include <unistd.h>

    #include <cstring>
#endif

namespace ya::Os
{

namespace
{

constexpr uint32_t kUnknownPid = 0;

uint32_t currentProcessId()
{
#if defined(_WIN32)
    return static_cast<uint32_t>(::_getpid());
#else
    return static_cast<uint32_t>(::getpid());
#endif
}

/// Filename-safe key. The hash suffix keeps two different keys that sanitize to
/// the same prefix (paths differing only in separators / case) apart, and bounds
/// the filename length so a long absolute path cannot overflow the limit.
std::string sanitizeName(std::string_view name)
{
    std::string out;
    out.reserve(name.size() + 16);
    for (const char c : name) {
        const bool bKeep = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.';
        out.push_back(bKeep ? c : '_');
    }
    if (out.size() > 96) {
        out.resize(96);
    }
    out.push_back('-');
    out.append(std::to_string(std::hash<std::string>{}(std::string(name))));
    return out;
}

uint32_t readOwnerPid(const std::filesystem::path& path)
{
#if defined(_WIN32)
    // The exclusive share mode already told us someone else owns the file; the
    // pid is a courtesy for the message, so a failure here is not an error.
    HANDLE file = ::CreateFileW(path.wstring().c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return kUnknownPid;
    }

    std::array<char, 32> buffer{};
    DWORD                read = 0;
    const BOOL           bOk  = ::ReadFile(file, buffer.data(),
                                          static_cast<DWORD>(buffer.size() - 1), &read, nullptr);
    ::CloseHandle(file);
    if (!bOk || read == 0) {
        return kUnknownPid;
    }
    buffer[read] = '\0';
    return static_cast<uint32_t>(std::strtoul(buffer.data(), nullptr, 10));
#else
    FILE* file = std::fopen(path.c_str(), "r");
    if (!file) {
        return kUnknownPid;
    }
    unsigned long pid = 0;
    const int     read = std::fscanf(file, "%lu", &pid);
    std::fclose(file);
    return read == 1 ? static_cast<uint32_t>(pid) : kUnknownPid;
#endif
}

void writeOwnerPid(const std::filesystem::path& path)
{
#if defined(_WIN32)
    FILE* file = nullptr;
    if (::_wfopen_s(&file, path.wstring().c_str(), L"w") != 0 || !file) {
        return;
    }
#else
    FILE* file = std::fopen(path.c_str(), "w");
    if (!file) {
        return;
    }
#endif
    std::fprintf(file, "%u\n", currentProcessId());
    std::fclose(file);
}

} // namespace

std::filesystem::path ProcessLock::resolveLockPath(std::string_view name)
{
    std::error_code ec;
    auto            dir = std::filesystem::temp_directory_path(ec);
    if (ec || dir.empty()) {
        dir = std::filesystem::current_path(ec);
    }
    dir /= "ya-instances";
    return dir / (sanitizeName(name) + ".lock");
}

ProcessLock::~ProcessLock()
{
    release();
}

ProcessLock::ProcessLock(ProcessLock&& other) noexcept
    : _handle(other._handle)
    , _path(std::move(other._path))
{
    other._handle = kInvalidHandle;
    other._path.clear();
}

ProcessLock& ProcessLock::operator=(ProcessLock&& other) noexcept
{
    if (this != &other) {
        release();
        _handle       = other._handle;
        _path         = std::move(other._path);
        other._handle = kInvalidHandle;
        other._path.clear();
    }
    return *this;
}

bool ProcessLock::tryAcquire(std::string_view name, uint32_t& ownerPid)
{
    ownerPid = kUnknownPid;
    if (isHeld()) {
        release();
    }
    if (name.empty()) {
        return true;
    }

    const std::filesystem::path path = resolveLockPath(name);
    std::error_code             ec;
    std::filesystem::create_directories(path.parent_path(), ec);

#if defined(_WIN32)
    // No sharing at all: a second opener is refused by the filesystem itself.
    HANDLE file = ::CreateFileW(path.wstring().c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ownerPid = readOwnerPid(path);
        return false;
    }
    _handle = file;
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        YA_CORE_WARN("ProcessLock: cannot open '{}': {}", path.string(), std::strerror(errno));
        // Failing open beats refusing to start over an unwritable temp dir.
        return true;
    }

    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ownerPid = readOwnerPid(path);
        ::close(fd);
        return false;
    }
    _handle = fd;
#endif

    _path = path;
    writeOwnerPid(path);
    return true;
}

void ProcessLock::release()
{
    if (!isHeld()) {
        return;
    }

#if defined(_WIN32)
    ::CloseHandle(_handle);
#else
    ::flock(_handle, LOCK_UN);
    ::close(_handle);
#endif
    _handle = kInvalidHandle;
    _path.clear();
}

} // namespace ya::Os
