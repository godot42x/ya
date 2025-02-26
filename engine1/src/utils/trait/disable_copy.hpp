#pragma once


struct disable_copy
{
    disable_copy(const disable_copy &)            = delete;
    disable_copy(disable_copy &&)                 = delete;
    disable_copy &operator=(const disable_copy &) = delete;
    disable_copy &operator=(disable_copy &&)      = delete;
};


struct disable_copy_move
{
    disable_copy_move() = default;

    disable_copy_move(const disable_copy_move &)            = delete;
    disable_copy_move(disable_copy_move &&)                 = delete;
    disable_copy_move &operator=(const disable_copy_move &) = delete;
    disable_copy_move &operator=(disable_copy_move &&)      = delete;
};