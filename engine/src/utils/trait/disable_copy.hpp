#pragma once


class disable_copy
{
    disable_copy(const disable_copy &)            = delete;
    disable_copy(disable_copy &&)                 = delete;
    disable_copy &operator=(const disable_copy &) = delete;
    disable_copy &operator=(disable_copy &&)      = delete;
};