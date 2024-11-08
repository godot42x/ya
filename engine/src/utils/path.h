/**
 *  Author: @godot42
 *  Create Time: 2023-11-17 23:45:29
 *  Modified by: @godot42
 *  Modified time: 2024-07-30 18:45:44
 *  Description:
 */



#pragma once


#include <filesystem>
#include <set>
#include <string_view>



#ifndef UTILS_API
    #define UTILS_API
#endif


namespace utils
{


extern bool                            is_dir_contain_all_symbols(const std::filesystem::path &path, const std::set<std::string> &target_symbols);
extern std::filesystem::path UTILS_API get_runtime_exe_path();

namespace project_locate
{

// each file/folder of `match_symbols` all  under a directory
// so we consider it is the project root we need
extern void UTILS_API  init(const std::vector<std::string> &match_symbols);
extern const UTILS_API std::filesystem::path &project_root();

}; // namespace project_locate


namespace detail
{
struct UTILS_API FPathImpl
{
    explicit FPathImpl(const char *the_path)
    {
        absolute_path = project_locate::project_root() / the_path;
    }

    explicit FPathImpl(const std::string &the_path)
    {
        absolute_path = project_locate::project_root() / the_path;
    }

    [[nodiscard]] operator std::string() const { return absolute_path.string(); }
    [[nodiscard]] operator std::filesystem::path() const { return absolute_path; }
    [[nodiscard]] std::filesystem::path operator/(std::string_view other)
    {
        return this->absolute_path / other;
    }

    std::filesystem::path absolute_path;
};
}; // namespace detail



} // namespace utils

using FPath = utils::detail::FPathImpl;
