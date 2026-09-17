#pragma once

#include "Core/Api.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

/// Process-wide OS / window-system facade. SDL is an implementation detail
/// of `Core/Os` (`Os.h`, `OsEvent.h`, `OsCursor.h`). Window-scoped GPU
/// surfaces stay on `INativeWindow` in RHI.
namespace ya::Os
{

/// Last window-system error string. Empty when the last OS call succeeded.
[[nodiscard]] YA_CORE_API std::string lastError();

[[nodiscard]] YA_CORE_API std::filesystem::path executableBasePath();

YA_CORE_API void sleepMs(uint32_t milliseconds);

YA_CORE_API bool openUrl(std::string_view url);

/// Live modifier bitmask using `EKeyMod` values.
[[nodiscard]] YA_CORE_API uint32_t queryKeyModState();

struct SharedLibrary
{
    void* handle = nullptr;

    [[nodiscard]] bool isValid() const { return handle != nullptr; }
};

[[nodiscard]] YA_CORE_API SharedLibrary loadLibrary(const std::filesystem::path& path);
[[nodiscard]] YA_CORE_API void*         loadSymbol(SharedLibrary library, const char* name);
YA_CORE_API void                        unloadLibrary(SharedLibrary& library);

[[nodiscard]] YA_CORE_API std::string clipboardText();
YA_CORE_API bool                      setClipboardText(std::string_view text);

/// Connected displays. Indices match `INativeWindow::getDisplayIndex()`.
[[nodiscard]] YA_CORE_API int         displayCount();
[[nodiscard]] YA_CORE_API std::string displayName(int index);
/// `usableWorkArea` prefers the work area (taskbar/menu excluded) and falls
/// back to the full display rect when that query fails.
[[nodiscard]] YA_CORE_API bool displayBounds(int  index,
                                             int& x,
                                             int& y,
                                             int& w,
                                             int& h,
                                             bool usableWorkArea);

} // namespace ya::Os
