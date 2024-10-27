/**
 *  Author: @godot42
 *  Create Time: 2023-11-17 23:45:29
 *  Modified by: @godot42
 *  Modified time: 2024-07-30 18:45:44
 *  Description:
 */



#pragma once


#include <filesystem>
#include <string_view>


#ifndef UTILS_API
    #define UTILS_API
#endif



namespace utils
{

using stdpath = std::filesystem::path;



extern std::string project_root_symbol;
void               SetProjectRootSymbol(std::string symbol);
const std::string &GetProjectRootSymbol();
stdpath            get_runtime_exe_path();

// iterate parent and children recursively
stdpath find_directory_by_file_symbol(stdpath &initial_pos, std::string target_symbol);


const UTILS_API std::filesystem::path &ProjectRoot();
namespace impl
{
struct UTILS_API FPathImpl
{
    explicit FPathImpl(const char *the_path)
    {
        absolute_path = ProjectRoot() / the_path;
    }

    explicit FPathImpl(const std::string &the_path)
    {
        absolute_path = ProjectRoot() / the_path;
    }


    [[nodiscard]] operator std::string() const { return absolute_path.string(); }

    [[nodiscard]] operator std::filesystem::path() const { return absolute_path; }

    [[nodiscard]] stdpath operator/(std::string_view other)
    {
        return this->absolute_path / other;
    }

    stdpath absolute_path;
};
}; // namespace impl



struct UTILS_API Files
{
    static std::string GetFileNameWithoutExtension(const std::string &path);


    template <class FileFilter, class Predicate>
    static void ForeachFileInFolder(std::filesystem::path path, FileFilter file_filter, Predicate &&predicate)
    {
        for (const auto texture : std::filesystem::directory_iterator(path))
        {
            const auto &file_path = texture.path();

            if (file_filter(file_path))
            {
                predicate(file_path);
            }
        }
    }
};

} // namespace utils

using FPath = utils::impl::FPathImpl;
