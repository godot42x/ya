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
#include <iostream>
#include <optional>
#include <queue>
#include <set>



static void panic(const std::string &msg, int code = 1)
{
    fprintf(stderr, msg.c_str());
    // PLATFORM_BREAK();
    std::exit(code);
}


#if __linux__
    #include "unistd.h"
#elif _WIN32
    #include <windows.h>
    //
    #include <libloaderapi.h>

#endif

namespace utils
{

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


// folder also be considered as a file
bool is_dir_contain_all_symbols(const std::filesystem::path &path, const std::set<std::string> &target_symbols)
{
    int count = 0;
    for (const auto &file : std::filesystem::directory_iterator(path))
    {
        if (target_symbols.contains(file.path().filename().string())) {
            ++count;
        }
    }
    return count == target_symbols.size();
}



namespace project_locate
{
#if !defined(NDEBUG)
    #define __VALIDATION 1
#endif

static std::filesystem::path project_root_path;
#if __VALIDATION
static bool bProjectRootInitialized = false;
#endif


static bool iterate_parents(const path &init_pos, const std::set<std::string> &target_symbols, path &out_dir)
{
    int  count     = 0;
    path directory = init_pos;
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
            fprintf(stderr, "current directory is already root path: %s\n", directory.string().c_str());
            break;
        }
#endif

        printf("Iterate parent %d times\n", count++);
        directory = directory.parent_path();
        printf("current directory: %s\n", directory.string().c_str());

        if (is_dir_contain_all_symbols(directory, target_symbols)) {
            out_dir = directory;
            return true;
        }
    }
    return false;
}



static bool iterate_children(const path &init_pos, const std::set<std::string> &target_symbols, path &out_dir)
{
    if (!std::filesystem::is_directory(init_pos)) {
        return false;
    }

    int              count = 0;
    std::queue<path> dir_queue;
    dir_queue.push(init_pos);

    while (!dir_queue.empty()) {
        printf("The %dth of child directory\n", count++);
        path current_dir = dir_queue.front();
        dir_queue.pop();

        for (auto &entry : std::filesystem::directory_iterator(current_dir)) {
            if (entry.is_directory()) {
                if (is_dir_contain_all_symbols(entry.path(), target_symbols)) {
                    out_dir = entry.path();
                    return true;
                }
                dir_queue.push(entry.path());
            }
        }
    }
    return false;
}

static std::optional<path> find_directory_by_file_symbols(path &initial_pos, const std::set<std::string> &target_symbols)
{
    // to parent
    if (initial_pos.empty()) {
        panic("initial_pos is empty");
    }

    if (std::filesystem::is_directory(initial_pos) && is_dir_contain_all_symbols(initial_pos, target_symbols)) {
        return initial_pos;
    }

    path target_path;
    if (iterate_parents(initial_pos, target_symbols, target_path))
    {
        return target_path;
    }
    if (iterate_children(initial_pos, target_symbols, target_path))
    {
        return target_path;
    }

    return {};
}



void init(const std::vector<std::string> &symbols)
{
    path exe_path = get_runtime_exe_path();
    printf("exe_path: %ls\n", exe_path.c_str());

    if (symbols.size() == 0) {
        panic("No enough args for project locate process init");
    }
    if (exe_path.empty()) {
        panic("Failed to get runtime path");
    }
    if (std::filesystem::is_directory(exe_path)) {
        panic("TODO and watch it: why a directory it is?");
    }

    printf("symbol args size: %zu\n", symbols.size());
    for (const auto &arg : symbols) {
        printf(" \t%s\n ", arg.c_str());
    }

    std::set<std::string> symbols_set(symbols.begin(), symbols.end());

    std::optional<path> project_root_dir = find_directory_by_file_symbols(exe_path, symbols_set);
    if (!project_root_dir.has_value()) {
        panic("Failed to find project root directory");
    }
    project_root_path = std::filesystem::absolute(project_root_dir.value());

    bProjectRootInitialized = true;
}


const std::filesystem::path &project_root()
{
    return project_root_path;
}

} // namespace project_locate


} // namespace utils
