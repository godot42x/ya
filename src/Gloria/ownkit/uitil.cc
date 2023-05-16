#include <Gloria/minimal.h>

#include <Gloria/ownkit/util.h>
#include <filesystem>

void ownkit::CreateDirectoryIfNotExist(const std::string &path)
{
    if (std::filesystem::exists(path)) {
        return;
    }

    assert(!path.empty());
    auto cmd = "mkdir " + path;


    auto ret = std::system(cmd.c_str());
    if (0 != ret) {
        throw std::runtime_error("execute system cmd [mkdir] error");
    }
}
