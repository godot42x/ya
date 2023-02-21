#include <assert.h>
#include <ownkit/util.h>


bool ownkit::CreateDirectoryIfNotExist(std::string &path)
{
    assert(!path.empty());

    auto cmd = "mkdir " + path;

    auto ret = std::system(cmd.c_str());
    if (0 != ret) {
        throw std::exception("execute system cmd error");
    }

    return true;
}