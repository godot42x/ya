#pragma once
#include <iostream>
#include <ostream>
#include <source_location>


namespace ut
{


struct log
{
    std::ostream &out = std::cout;

    log(std::source_location loc = std::source_location::current());
    ~log();

    template <typename T>
    log &operator,(const T &msg)
    {
        out << msg << ' ';
        return *this;
    }
};
} // namespace ut