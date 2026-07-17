

#pragma once

#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <source_location>
#include <string>



template <typename... Ts>
inline void log_impl(const char *msg, std::source_location loc = std::source_location::current())
{
    std::filesystem::path fp(loc.file_name());
    std::string           formatted = std::format("{}:{}: {}\n",
                                        fp.filename().string(),
                                        loc.line(),
                                        msg);
    printf("%s", formatted.c_str());
}

#define YALUA_LOG(fmt, ...) \
    log_impl(std::format(fmt, ##__VA_ARGS__).c_str());


struct debug
{
    std::ostream &out = std::cout;

    template <typename T>
    debug &operator,(T &&t)
    {
        out << std::forward<T>(t);
        return *this;
    }
};