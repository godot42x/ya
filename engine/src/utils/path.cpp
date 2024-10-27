/**
 *  Author: @godot42
 *  Create Time: 2024-07-28 20:32:18
 *  Modified by: @godot42
 *  Modified time: 2024-07-30 16:50:25
 *  Description:
 */



// Created by nono on 10/7/23.


#include "path.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#if __linux__
    #include "unistd.h"
#elif _WIN32
    #include <windows.h>
    //
    #include <libloaderapi.h>

#endif


namespace utils
{

std::string        project_root_symbol = std::string(".project-root-symbol");
void               SetProjectRootSymbol(std::string symbol) { project_root_symbol = symbol; }
const std::string &GetProjectRootSymbol()
{
    return project_root_symbol;
}

using std::filesystem::path;


std::filesystem::path get_runtime_exe_path()
{
    // just not support the non-english path
#if _WIN32
    // TODO: let's see what more  microshit problem will be
    std::wstring path_str(4096, '\0');
    int          len = GetModuleFileName(NULL, (LPWSTR)path_str.data(), path_str.size());
    if (len <= 0)
        return {};
    return path_str;

#elif __linux__
    std::string path_str(4096, '\0');
    ssize_t     n = readlink("/proc/self/exe", path_str.data(), path_str.size());
    if (n < 0)
        return {};
#elif __APPLE__
    std::string path_str(4096, '\0');
    int         pid = GetCurrentProcessId();
    if (proc_pidpath(pid, path_str, path_str.size()) <= 0)
        return {};
#endif
    return path_str;
}


static bool is_dir_contain_file_symbol(const std::filesystem::path &the_path, std::string &target_symbol)
{
    for (const auto &file : std::filesystem::directory_iterator(the_path))
    {
        if (file.path().filename() == target_symbol) {
            return true;
        }
    }
    return false;
}

static bool recursive_iterate_parent(const path &init_pos, std::string &target_symbol, path &out_dir)
{
    static int nth       = 1;
    path       directory = init_pos;
    while (true)
    {
        // printf("parent path: %s\n", directory.parent_path().string().c_str());
        // printf("root path: %s\n", directory.root_path().string().c_str());
        // printf("root directory: %s\n", directory.root_directory().string().c_str());
        // printf("root name: %s\n", directory.root_name().string().c_str());

        if (!directory.has_parent_path()) {
            break;
        }
#if __linux__
        if (directory.filename() != "/") {
            break;
        }
#elif _WIN32
        if (directory == directory.root_path()) {
            break;
        }
#endif

        printf("Recursive parent %d times\n", nth++);
        directory = directory.parent_path();
        printf("current directory: %s\n", directory.string().c_str());

        if (is_dir_contain_file_symbol(directory, target_symbol)) {
            out_dir = directory;
            return true;
        }
    }
    return false;
}


static int  nth = 1;
static bool recursive_iterate_children(const path &init_pos, std::string &target_symbol, path &out_dir)
{
    printf("Recursive children %d times\n", nth++);
    if (!std::filesystem::is_directory(init_pos)) {
        return false;
    }

    for (auto &entry : std::filesystem::directory_iterator(init_pos))
    {
        if (entry.is_directory()) {
            if (is_dir_contain_file_symbol(entry.path(), target_symbol)) {
                out_dir = entry.path();
                return true;
            }
            if (recursive_iterate_children(entry.path(), target_symbol, out_dir))
            {
                return true;
            }
        }
    }
    return false;
}

path find_directory_by_file_symbol(path &initial_pos, std::string target_symbol)
{
    // to parent
    if (!initial_pos.empty()) {
        if (is_directory(initial_pos) && is_dir_contain_file_symbol(initial_pos, target_symbol)) {
            return initial_pos;
        }

        path target_path;
        if (recursive_iterate_parent(initial_pos, target_symbol, target_path)) {
            return target_path;
        }
        if (recursive_iterate_children(initial_pos, target_symbol, target_path))
        {
            return target_path;
        }
    }

    throw std::runtime_error("The directory mark by symbol file not found! Symbol file: " + target_symbol);
}


const std::filesystem::path &ProjectRoot()
{
    static bool bInitialized = false;
    static path project_root;

    if (bInitialized) {
        return project_root;
    }

    path exe_path = get_runtime_exe_path();
    printf("exe_path: %ls\n", exe_path.c_str());

    if (exe_path.empty())
    {
        throw std::runtime_error("Failed to get runtime path");
    }
    if (std::filesystem::is_directory(exe_path))
    {
        throw std::runtime_error("TODO and watch it: why a directory it is?");
    }

    // TODO: current identify it by a file, maybe by some certain directory struct further more
    auto target_symbol = GetProjectRootSymbol();
    assert(!target_symbol.empty());
    printf("target symbol: %s\n", target_symbol.c_str());
    path project_root_dir = find_directory_by_file_symbol(exe_path, target_symbol);
    project_root          = std::filesystem::absolute(project_root_dir);
    bInitialized          = true;

    return project_root;
}



std::string Files::GetFileNameWithoutExtension(const std::string &path)
{
    size_t slash_pos = path.find_last_of("/\\");
    size_t dot_pos   = path.find_last_of(".");

    // DEBUG("{}, {}, {}", path, slash_pos, dot_pos);
    auto filename = path.substr(slash_pos + 1, dot_pos - slash_pos - 1);
    // LOG("{}", filename);
    return filename;
};



} // namespace utils
