#include "log_level.h"
#include <cassert>


TOP_LEVEL_NAMESPACE_BEGIN

namespace LogLevel
{


std::string_view toString(LogLevel::T level)
{
    switch (level) {
    case Debug:
        return "Debug";
    case Trace:
        return "Trace";
    case Info:
        return "Info";
    case Warn:
        return "Warn";
    case Error:
        return "Error";
    case Fatal:
        return "Fatal";
    }
    assert(false);
    return "Unknown";
}


const std::unordered_map<LogLevel::T, std::string> level2Strings = {
    {LogLevel::Debug, "DEBUG"},
    {LogLevel::Trace, "TRACE"},
    {LogLevel::Info, "Info"},
    {LogLevel::Warn, "WARN"},
    {LogLevel::Error, "ERROR"},
    {LogLevel::Fatal, "FATAL"},
};
const std::unordered_map<LogLevel::T, std::string> level2CompatLevelStrings = {
    {LogLevel::Debug, "D"},
    {LogLevel::Trace, "T"},
    {LogLevel::Info, "I"},
    {LogLevel::Warn, "W"},
    {LogLevel::Error, "E"},
    {LogLevel::Fatal, "F"},
};


const std::unordered_map<ETerminalColor, std::string> color2TerminalColorCode = {
    {ETerminalColor::Reset, "\033[0m"},
    {ETerminalColor::White, "\033[37m"},
    {ETerminalColor::Green, "\033[32m"},
    {ETerminalColor::Magenta, "\033[35m"},
    {ETerminalColor::Cyan, "\033[36m"},
    {ETerminalColor::Blue, "\033[34m"},
    {ETerminalColor::Yellow, "\033[33m"},
    {ETerminalColor::Red, "\033[31m"},
};


const std::unordered_map<LogLevel::T, ETerminalColor> level2TerminalColor = {
    {LogLevel::Debug, ETerminalColor::Cyan},
    {LogLevel::Trace, ETerminalColor::White},
    {LogLevel::Info, ETerminalColor::Green},
    {LogLevel::Warn, ETerminalColor::Yellow},
    {LogLevel::Error, ETerminalColor::Red},
    {LogLevel::Fatal, ETerminalColor::Red},
};

template <typename T, typename K>
const auto &unsafeConstMapGet(const T &from, const K &k)
{
    return from.find(k)->second;
}


const std::unordered_map<LogLevel::T, std::string> level2TerminalColorCode = {
    {LogLevel::Debug, unsafeConstMapGet(color2TerminalColorCode, unsafeConstMapGet(level2TerminalColor, LogLevel::Debug))},
    {LogLevel::Trace, unsafeConstMapGet(color2TerminalColorCode, unsafeConstMapGet(level2TerminalColor, LogLevel::Trace))},
    {LogLevel::Info, unsafeConstMapGet(color2TerminalColorCode, unsafeConstMapGet(level2TerminalColor, LogLevel::Info))},
    {LogLevel::Warn, unsafeConstMapGet(color2TerminalColorCode, unsafeConstMapGet(level2TerminalColor, LogLevel::Warn))},
    {LogLevel::Error, unsafeConstMapGet(color2TerminalColorCode, unsafeConstMapGet(level2TerminalColor, LogLevel::Error))},
    {LogLevel::Fatal, unsafeConstMapGet(color2TerminalColorCode, unsafeConstMapGet(level2TerminalColor, LogLevel::Fatal))},
};

} // namespace LogLevel


TOP_LEVEL_NAMESPACE_END