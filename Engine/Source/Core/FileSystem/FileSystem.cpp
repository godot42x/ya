#include "FileSystem.h"
#include "Core/Log.h"
#include "utility.cc/file_utils.h"



FileSystem *FileSystem::instance = nullptr;

void FileSystem::init()
{
    instance = new FileSystem();
}


bool FileSystem::readFileToString(std::string_view filepath, std::string &output) const
{
    // default to project root
    std::filesystem::path fullPath = projectRoot / filepath;

    auto opt = ut::file::read_all(fullPath);
    if (!opt) {
        NE_CORE_ERROR("Failed to read file: {}", std::filesystem::absolute(fullPath).string());
        return false;
    }
    output = *opt;
    return true;
}
