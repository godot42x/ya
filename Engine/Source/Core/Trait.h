#pragma once

struct disable_copy
{
    disable_copy()                                = default;
    virtual ~disable_copy()                       = default;
    disable_copy(const disable_copy &)            = delete;
    disable_copy &operator=(const disable_copy &) = delete;
    disable_copy(disable_copy &&)                 = default;
    disable_copy &operator=(disable_copy &&)      = default;
};