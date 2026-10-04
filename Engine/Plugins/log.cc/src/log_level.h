#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "base.h"



TOP_LEVEL_NAMESPACE_BEGIN



namespace LogLevel
{
enum T
{
    Debug = 100,
    Trace = 200,
    Info  = 300,
    Warn  = 400,
    Error = 500,
    Fatal = 600,
};

extern LOG_CC_API std::string_view toString(LogLevel::T level);


enum ETerminalColor
{
    Reset = 0,
    White,
    Green,
    Magenta,
    Cyan,
    Blue,
    Yellow,
    Red,
    ColorCount,
};

LOG_CC_API extern const std::unordered_map<LogLevel::T, std::string>    level2Strings;            // LogLevel::Debug -> "DEBUG"
LOG_CC_API extern const std::unordered_map<LogLevel::T, std::string>    level2CompatLevelStrings; // LogLevel::Debug -> "D"
LOG_CC_API extern const std::unordered_map<ETerminalColor, std::string> color2TerminalColorCode;  // ETerminalColor::Reset -> "\033[0m"
LOG_CC_API extern const std::unordered_map<LogLevel::T, ETerminalColor> level2TerminalColor;      // LogLevel::Debug -> ETerminalColor::Green
LOG_CC_API extern const std::unordered_map<LogLevel::T, std::string>    level2TerminalColorCode;  // LogLevel::Debug -> "\033[32m"


}; // namespace LogLevel

TOP_LEVEL_NAMESPACE_END