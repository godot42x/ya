#include "Product/Editor/FileExplorerInternal.h"

namespace ya
{

FileExplorer::~FileExplorer()
{
    flushConfig();
}

std::string FileExplorer::makeConfigKey(std::string_view suffix) const
{
    return makeFileExplorerConfigKey(*this, suffix);
}

} // namespace ya
