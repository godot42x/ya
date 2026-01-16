#include "VirtualFileSystem.h"
#include "Core/Log.h"
#include "utility.cc/file_utils.h"



VirtualFileSystem *VirtualFileSystem::instance = nullptr;

void VirtualFileSystem::init()
{
    instance = new VirtualFileSystem();
}

std::vector<uint8_t> VirtualFileSystem::loadFileToMemory(std::string_view filepath) const
{
    std::string fullPath = translatePath(filepath).string();
    auto        ret      = ut::file::read_all(fullPath);
    if (!ret) {
        YA_CORE_ERROR("Failed to read file: {}", fullPath);
        return {};
    }
    std::string &data = ret.value();
    return std::vector<uint8_t>(data.begin(), data.end());
}


bool VirtualFileSystem::readFileToString(std::string_view filepath, std::string &output) const
{
    // default to project root
    std::filesystem::path fullPath = projectRoot / filepath;

    auto opt = ut::file::read_all(fullPath);
    if (!opt) {
        YA_CORE_ERROR("Failed to read file: {}", std::filesystem::absolute(fullPath).string());
        return false;
    }
    output = *opt;
    return true;
}
